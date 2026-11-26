// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team & The Silicon Preservation Society
// SPDX-License-Identifier: GPL-3.0+

#include "CDVDRingSlab.hpp"
#include "IHardwareModeAdapter.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <thread>

#if __has_include(<ax/topology/pinning.hpp>)
#include <ax/topology/pinning.hpp>
#define PCSX2_AX_CDVD_HAS_AX_TOPOLOGY 1
#else
#define PCSX2_AX_CDVD_HAS_AX_TOPOLOGY 0
#endif

namespace pcsx2_ax::vm_bridge {
[[nodiscard]] uint64_t current_epoch() noexcept;
} // namespace pcsx2_ax::vm_bridge

namespace pcsx2_ax::cdvd {

class ICdvdDiscSource {
public:
	virtual ~ICdvdDiscSource() noexcept = default;

	[[nodiscard]] virtual bool is_disc_ready() const noexcept = 0;
	[[nodiscard]] virtual uint32_t sector_count() const noexcept = 0;
	[[nodiscard]] virtual int32_t media_type() const noexcept = 0;
	[[nodiscard]] virtual bool read_sectors_2048(uint32_t lba, uint32_t count, std::span<uint8_t> dst) noexcept = 0;
	[[nodiscard]] virtual bool read_sectors_2352(uint32_t lba, uint32_t count, std::span<uint8_t> dst) noexcept = 0;

	virtual void on_tray_changed(bool tray_open, bool disc_inserted) noexcept = 0;
	virtual void on_refresh_toc() noexcept = 0;
};

namespace {

inline constexpr uint32_t kSectorsPerRead   = 16u;
inline constexpr uint32_t kRawSectorBytes   = 2352u;
inline constexpr uint32_t kBlockRawBytes    = kRawSectorBytes * kSectorsPerRead;
inline constexpr uint32_t kCacheIndexBits   = 12u;
inline constexpr uint32_t kCacheSlotCount   = 1u << kCacheIndexBits;
inline constexpr uint32_t kCacheSlotMask    = kCacheSlotCount - 1u;
inline constexpr uint32_t kReqRingCapacity  = 256u; // Power of two for lock-free ring
inline constexpr uint32_t kReqRingMask      = kReqRingCapacity - 1u;
inline constexpr uint32_t kInvalidLsn       = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t kMaxPrefetchBurst = 16u;

inline constexpr int32_t kMode2048 = 0;
inline constexpr int32_t kMode2328 = 1;
inline constexpr int32_t kMode2340 = 2;

static_assert(kSectorsPerRead > 1u && (kSectorsPerRead & (kSectorsPerRead - 1u)) == 0u,
              "kSectorsPerRead must be a power of two");
static_assert((kReqRingCapacity & (kReqRingCapacity - 1u)) == 0u,
              "kReqRingCapacity must be a power of two");

// 64-byte cache-line-aligned slot to avoid false sharing across EE/VU/GS/Core 5
struct alignas(kCacheLineBytes) CacheBlockSlot {
	std::atomic<uint64_t> epoch{0u};
	std::atomic<uint32_t> lsn{kInvalidLsn};
	uint32_t pad_{0u};
	std::array<uint8_t, kBlockRawBytes> data{};
};

// Lock-free atomic MPSC/SPSC LSN request ring for housekeeping Core 5
class alignas(kCacheLineBytes) AtomicLsnRing {
public:
	bool push(uint32_t lsn) noexcept
	{
		for (;;)
		{
			const uint32_t head = head_.load(std::memory_order_acquire);
			const uint32_t tail = tail_.load(std::memory_order_relaxed);
			const uint32_t next_tail = (tail + 1u) & kReqRingMask;
			if (next_tail == head)
				return false; // Ring full; reader loop will catch up without blocking

			if (tail_.compare_exchange_weak(
				tail_tmp(tail), next_tail, std::memory_order_acq_rel, std::memory_order_relaxed))
			{
				slots_[tail].store(lsn, std::memory_order_release);
				return true;
			}
		}
	}

	bool pop(uint32_t& out_lsn) noexcept
	{
		const uint32_t head = head_.load(std::memory_order_relaxed);
		if (head == tail_.load(std::memory_order_acquire))
			return false;

		out_lsn = slots_[head].load(std::memory_order_acquire);
		head_.store((head + 1u) & kReqRingMask, std::memory_order_release);
		return true;
	}

	void clear() noexcept
	{
		head_.store(tail_.load(std::memory_order_acquire), std::memory_order_release);
	}

private:
	static uint32_t& tail_tmp(uint32_t v) noexcept
	{
		static thread_local uint32_t scratch = 0u;
		scratch = v;
		return scratch;
	}

	alignas(kCacheLineBytes) std::atomic<uint32_t> head_{0u};
	alignas(kCacheLineBytes) std::atomic<uint32_t> tail_{0u};
	std::array<std::atomic<uint32_t>, kReqRingCapacity> slots_{};
};

class DiscThreadRuntime {
public:
	void set_adapter(pcsx2_ax::core::IHardwareModeAdapter* adapter, ICdvdDiscSource* source) noexcept
	{
		adapter_.store(adapter, std::memory_order_release);
		source_.store(source, std::memory_order_release);
		epoch_.store(pcsx2_ax::vm_bridge::current_epoch(), std::memory_order_release);
		req_ring_.clear();
	}

	bool start()
	{
		ensure_cache_allocated();
		reset_cache();
		epoch_.store(pcsx2_ax::vm_bridge::current_epoch(), std::memory_order_release);

		bool expected = false;
		if (thread_open_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
			io_thread_ = std::thread([this]() noexcept { run_io_loop(); });
		return true;
	}

	void stop()
	{
		thread_open_.store(false, std::memory_order_release);
		if (io_thread_.joinable())
			io_thread_.join();
	}

	void request_sector(uint32_t sector, int32_t) noexcept
	{
		ICdvdDiscSource* src = source_.load(std::memory_order_acquire);
		if (src == nullptr || sector >= src->sector_count())
			return;

		const uint32_t aligned_lsn = sector & ~(kSectorsPerRead - 1u);
		const uint64_t ep = pcsx2_ax::vm_bridge::current_epoch();
		if (cache_contains(aligned_lsn, ep))
			return;

		(void)req_ring_.push(aligned_lsn);
	}

	uint8_t* get_sector(uint32_t sector, int32_t mode, std::array<uint8_t, kBlockRawBytes>& tls_block) noexcept
	{
		ICdvdDiscSource* src = source_.load(std::memory_order_acquire);
		if (src == nullptr)
			return tls_block.data();

		const uint64_t ep = pcsx2_ax::vm_bridge::current_epoch();
		const uint32_t sector_block = sector & ~(kSectorsPerRead - 1u);
		if (!cache_fetch(sector_block, ep, tls_block.data()) && read_block_into_cache(sector_block, ep))
			(void)cache_fetch(sector_block, ep, tls_block.data());

		const int32_t media = src->media_type();
		if (media >= 0)
			return tls_block.data() + (2048u * (sector - sector_block));

		uint8_t* frame = tls_block.data() + (kRawSectorBytes * (sector - sector_block));
		switch (mode)
		{
			case kMode2048: return ((frame[15] & 3u) == 2u) ? (frame + 24) : (frame + 16);
			case kMode2328: return frame + 24;
			case kMode2340: return frame + 12;
			default:        return frame;
		}
	}

	int32_t direct_read_sector(uint32_t sector, int32_t mode, uint8_t* out_buffer,
	                           std::array<uint8_t, kBlockRawBytes>& tls_block) noexcept
	{
		ICdvdDiscSource* src = source_.load(std::memory_order_acquire);
		if (src == nullptr || out_buffer == nullptr || sector >= src->sector_count())
			return -1;

		const uint64_t ep = pcsx2_ax::vm_bridge::current_epoch();
		const uint32_t sector_block = sector & ~(kSectorsPerRead - 1u);
		if (!cache_fetch(sector_block, ep, tls_block.data()) && read_block_into_cache(sector_block, ep))
			(void)cache_fetch(sector_block, ep, tls_block.data());

		const int32_t media = src->media_type();
		if (media >= 0)
		{
			std::memcpy(out_buffer, tls_block.data() + (2048u * (sector - sector_block)), 2048u);
			return 0;
		}

		const uint8_t* frame = tls_block.data() + (kRawSectorBytes * (sector - sector_block));
		switch (mode)
		{
			case kMode2048:
				std::memcpy(out_buffer, ((frame[15] & 3u) == 2u) ? (frame + 24) : (frame + 16), 2048u);
				return 0;
			case kMode2328:
				std::memcpy(out_buffer, frame + 24, 2328u);
				return 0;
			case kMode2340:
				std::memcpy(out_buffer, frame + 12, 2340u);
				return 0;
			default:
				std::memcpy(out_buffer, frame, kRawSectorBytes);
				return 0;
		}
	}

	void refresh_data() noexcept
	{
		if (ICdvdDiscSource* src = source_.load(std::memory_order_acquire); src != nullptr)
			src->on_refresh_toc();
		epoch_.store(pcsx2_ax::vm_bridge::current_epoch(), std::memory_order_release);
		reset_cache();
	}

private:
	static constexpr uint32_t sector_hash(uint32_t lsn) noexcept
	{
		uint32_t hash = 0u;
		int shift = 32;
		while (shift >= 0)
		{
			hash ^= (lsn & kCacheSlotMask);
			lsn >>= kCacheIndexBits;
			shift -= static_cast<int>(kCacheIndexBits);
		}
		return hash & kCacheSlotMask;
	}

	void ensure_cache_allocated() noexcept
	{
		if (cache_slots_ != nullptr)
			return;
		const std::size_t bytes = sizeof(CacheBlockSlot) * kCacheSlotCount;
		if (cache_slab_.allocate(bytes))
		{
			cache_slots_ = new (cache_slab_.data()) CacheBlockSlot[kCacheSlotCount];
		}
		else
		{
			heap_fallback_slots_ = std::make_unique<CacheBlockSlot[]>(kCacheSlotCount);
			cache_slots_ = heap_fallback_slots_.get();
		}
	}

	void reset_cache() noexcept
	{
		if (cache_slots_ == nullptr)
			return;
		for (uint32_t i = 0; i < kCacheSlotCount; ++i)
		{
			cache_slots_[i].lsn.store(kInvalidLsn, std::memory_order_relaxed);
			cache_slots_[i].epoch.store(0u, std::memory_order_relaxed);
		}
		std::atomic_thread_fence(std::memory_order_release);
	}

	[[nodiscard]] bool cache_contains(uint32_t lsn, uint64_t ep) const noexcept
	{
		if (cache_slots_ == nullptr)
			return false;
		CacheBlockSlot& slot = cache_slots_[sector_hash(lsn)];
		return slot.epoch.load(std::memory_order_acquire) == ep &&
		       slot.lsn.load(std::memory_order_acquire) == lsn;
	}

	// Read directly into target cache slot and validate epoch before publishing
	[[nodiscard]] bool read_block_into_cache(uint32_t sector, uint64_t expected_epoch) noexcept
	{
		ICdvdDiscSource* src = source_.load(std::memory_order_acquire);
		if (src == nullptr || cache_slots_ == nullptr || pcsx2_ax::vm_bridge::current_epoch() != expected_epoch)
			return false;

		const uint32_t total = src->sector_count();
		if (sector >= total)
			return false;

		const uint32_t count = std::min(kSectorsPerRead, total - sector);
		const int32_t media = src->media_type();
		CacheBlockSlot& slot = cache_slots_[sector_hash(sector)];

		slot.lsn.store(kInvalidLsn, std::memory_order_release);
		std::span<uint8_t> dst_span{slot.data.data(), kBlockRawBytes};

		for (int tries = 0; tries < 2; ++tries)
		{
			if (pcsx2_ax::vm_bridge::current_epoch() != expected_epoch)
				return false;

			const bool ok = (media >= 0)
				? src->read_sectors_2048(sector, count, dst_span)
				: src->read_sectors_2352(sector, count, dst_span);
			if (ok && pcsx2_ax::vm_bridge::current_epoch() == expected_epoch)
			{
				slot.lsn.store(sector, std::memory_order_release);
				slot.epoch.store(expected_epoch, std::memory_order_release);
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool cache_fetch(uint32_t lsn, uint64_t ep, uint8_t* out_block_data) const noexcept
	{
		if (cache_slots_ == nullptr)
			return false;
		CacheBlockSlot& slot = cache_slots_[sector_hash(lsn)];
		if (slot.epoch.load(std::memory_order_acquire) != ep ||
		    slot.lsn.load(std::memory_order_acquire) != lsn)
		{
			return false;
		}
		std::memcpy(out_block_data, slot.data.data(), kBlockRawBytes);
		return slot.epoch.load(std::memory_order_acquire) == ep &&
		       slot.lsn.load(std::memory_order_acquire) == lsn;
	}

	[[nodiscard]] bool update_disc_status(uint64_t current_ep) noexcept
	{
		ICdvdDiscSource* src = source_.load(std::memory_order_acquire);
		if (src == nullptr)
			return true;

		if (epoch_.load(std::memory_order_acquire) != current_ep)
		{
			epoch_.store(current_ep, std::memory_order_release);
			req_ring_.clear();
			reset_cache();
		}

		const bool ready = src->is_disc_ready();
		if (!ready)
		{
			if (!disc_has_changed_)
			{
				disc_has_changed_ = true;
				src->on_tray_changed(true, false);
			}
		}
		else if (disc_has_changed_)
		{
			disc_has_changed_ = false;
			src->on_tray_changed(false, true);
			refresh_data();
			req_ring_.clear();
		}
		return !ready;
	}

	void run_io_loop() noexcept
	{
#if PCSX2_AX_CDVD_HAS_AX_TOPOLOGY
		ax::topology::pin_to_housekeeping_core5();
#endif

		uint32_t prefetches_left = 0u;
		std::printf(" * CDVD: IO thread started...\n");

		while (thread_open_.load(std::memory_order_acquire))
		{
			const uint64_t ep = pcsx2_ax::vm_bridge::current_epoch();
			if (update_disc_status(ep))
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				prefetches_left = 0u;
				continue;
			}

			uint32_t request_lsn = 0u;
			const bool handling_request = req_ring_.pop(request_lsn);
			if (!handling_request)
			{
				if (prefetches_left == 0u)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
					continue;
				}
				--prefetches_left;
				request_lsn = last_block_lsn_.load(std::memory_order_relaxed) + kSectorsPerRead;
			}

			if (!cache_contains(request_lsn, ep) && !read_block_into_cache(request_lsn, ep))
			{
				prefetches_left = 0u;
				continue;
			}

			last_block_lsn_.store(request_lsn, std::memory_order_release);
			if (!handling_request)
				continue;

			ICdvdDiscSource* src = source_.load(std::memory_order_acquire);
			const uint32_t total = (src != nullptr) ? src->sector_count() : 0u;
			const uint32_t next_lsn = request_lsn + kSectorsPerRead;
			if (next_lsn >= total)
			{
				prefetches_left = 0u;
			}
			else
			{
				const uint32_t remaining = total - next_lsn;
				prefetches_left = std::min((remaining + kSectorsPerRead - 1u) / kSectorsPerRead, kMaxPrefetchBurst);
			}
		}

		std::printf(" * CDVD: IO thread finished.\n");
	}

	std::atomic<pcsx2_ax::core::IHardwareModeAdapter*> adapter_{nullptr};
	std::atomic<ICdvdDiscSource*> source_{nullptr};
	std::atomic<uint64_t> epoch_{1u};

	HugeTlbSlab cache_slab_{};
	std::unique_ptr<CacheBlockSlot[]> heap_fallback_slots_{};
	CacheBlockSlot* cache_slots_{nullptr};

	AtomicLsnRing req_ring_{};
	std::thread io_thread_{};

	alignas(kCacheLineBytes) std::atomic<bool> thread_open_{false};
	alignas(kCacheLineBytes) std::atomic<uint32_t> last_block_lsn_{0u};
	bool disc_has_changed_{false};
};

DiscThreadRuntime& runtime() noexcept
{
	static DiscThreadRuntime instance{};
	return instance;
}

} // namespace

void bind_cdvd_adapter(pcsx2_ax::core::IHardwareModeAdapter* adapter, ICdvdDiscSource* source) noexcept
{
	runtime().set_adapter(adapter, source);
}

} // namespace pcsx2_ax::cdvd

void cdvdStartThread() { (void)pcsx2_ax::cdvd::runtime().start(); }
void cdvdStopThread() { pcsx2_ax::cdvd::runtime().stop(); }
void cdvdRequestSector(uint32_t sector, int32_t mode) { pcsx2_ax::cdvd::runtime().request_sector(sector, mode); }

uint8_t* cdvdGetSector(uint32_t sector, int32_t mode)
{
	thread_local std::array<uint8_t, pcsx2_ax::cdvd::kBlockRawBytes> tls_block{};
	return pcsx2_ax::cdvd::runtime().get_sector(sector, mode, tls_block);
}

int32_t cdvdDirectReadSector(uint32_t sector, int32_t mode, uint8_t* buffer)
{
	thread_local std::array<uint8_t, pcsx2_ax::cdvd::kBlockRawBytes> tls_block{};
	return pcsx2_ax::cdvd::runtime().direct_read_sector(sector, mode, buffer, tls_block);
}

void cdvdRefreshData() { pcsx2_ax::cdvd::runtime().refresh_data(); }
