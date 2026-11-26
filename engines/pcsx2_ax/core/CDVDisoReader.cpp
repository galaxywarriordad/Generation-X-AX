// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CDVDRingSlab.hpp"
#include "IHardwareModeAdapter.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>

namespace pcsx2_ax::cdvd {

class ZeroCopyIsoReader {
public:
	static constexpr std::size_t kPrefetchBurstSectors = 16u;

	ZeroCopyIsoReader() noexcept = default;
	~ZeroCopyIsoReader() noexcept { shutdown(); }

	ZeroCopyIsoReader(const ZeroCopyIsoReader&)            = delete;
	ZeroCopyIsoReader& operator=(const ZeroCopyIsoReader&) = delete;

	[[nodiscard]] bool init(pcsx2_ax::core::IHardwareModeAdapter* adapter,
	                        uint32_t total_sectors = 0u) noexcept {
		shutdown();
		if (!ring_.init())
			return false;
		adapter_.store(adapter, std::memory_order_release);
		total_sectors_.store(total_sectors, std::memory_order_release);
		next_read_lba_.store(0u, std::memory_order_release);
		seek_target_lba_.store(0u, std::memory_order_release);
		seek_pending_.store(false, std::memory_order_release);
		return true;
	}

	void shutdown() noexcept {
		seek_pending_.store(false, std::memory_order_release);
		ring_.wake_signal().notify_all();
		ring_.shutdown();
	}

	void set_adapter(pcsx2_ax::core::IHardwareModeAdapter* adapter, uint32_t total_sectors) noexcept {
		adapter_.store(adapter, std::memory_order_release);
		total_sectors_.store(total_sectors, std::memory_order_release);
	}

	[[nodiscard]] const SectorSlot* peek_sector() const noexcept {
		return ring_.peek_read_slot();
	}

	void consume_sector() noexcept {
		ring_.release_read_slot();
	}

	void request_seek(uint32_t target_lba, const std::atomic<bool>& worker_quiesced) noexcept {
		seek_target_lba_.store(target_lba, std::memory_order_release);
		seek_pending_.store(true, std::memory_order_release);
		ring_.wake_signal().notify();

		while (seek_pending_.load(std::memory_order_acquire) &&
		       !worker_quiesced.load(std::memory_order_acquire)) {
			ring_.wake_signal().wait_once();
		}

		ring_.reset_indices();
		next_read_lba_.store(target_lba, std::memory_order_release);
		seek_pending_.store(false, std::memory_order_release);
		ring_.wake_signal().notify();
	}

	[[nodiscard]] bool seek_pending() const noexcept {
		return seek_pending_.load(std::memory_order_acquire);
	}

	[[nodiscard]] bool needs_readahead() const noexcept {
		return ring_.needs_readahead();
	}

	[[nodiscard]] std::size_t prefetch_burst() noexcept {
		auto* adp = adapter_.load(std::memory_order_acquire);
		const uint32_t total = total_sectors_.load(std::memory_order_acquire);
		uint32_t lba = next_read_lba_.load(std::memory_order_acquire);
		if (adp == nullptr || total == 0u || lba >= total)
			return 0u;

		std::size_t produced = 0u;
		const std::size_t max_burst = std::min<std::size_t>(kPrefetchBurstSectors, ring_.free_slots());
		for (std::size_t i = 0u; i < max_burst && lba < total; ++i, ++lba) {
			SectorSlot* slot = ring_.acquire_write_slot();
			if (slot == nullptr)
				break;

			uint16_t sector_bytes = 2048u;
			std::span<uint8_t> dst = slot->writable_span(kMaxRawSectorBytes);
			const bool ok = adp->read_media_sectors(lba, 1u, dst, sector_bytes);
			slot->lba = lba;
			slot->sector_bytes = (sector_bytes > 0u) ? sector_bytes : 2048u;
			slot->status = ok ? ReadStatus::Ok : ReadStatus::IoError;
			ring_.publish_write_slot();
			++produced;
			if (!ok)
				break;
		}

		next_read_lba_.store(lba, std::memory_order_release);
		return produced;
	}

	[[nodiscard]] CdvdSpscRing<kDefaultRingSlots>& ring() noexcept { return ring_; }

private:
	CdvdSpscRing<kDefaultRingSlots> ring_{};
	std::atomic<pcsx2_ax::core::IHardwareModeAdapter*> adapter_{nullptr};
	alignas(kCacheLineBytes) std::atomic<uint32_t> total_sectors_{0u};
	alignas(kCacheLineBytes) std::atomic<uint32_t> next_read_lba_{0u};
	alignas(kCacheLineBytes) std::atomic<uint32_t> seek_target_lba_{0u};
	alignas(kCacheLineBytes) std::atomic<bool> seek_pending_{false};
};

} // namespace pcsx2_ax::cdvd
