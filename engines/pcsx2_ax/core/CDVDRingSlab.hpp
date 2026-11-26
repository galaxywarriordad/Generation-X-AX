// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Zero-copy SPSC sector ring buffer, event-signaled wakeup primitive, and
// HugeTLB/mlockall slab allocator for PCSX2_AX (libax_pcsx2_core).

#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <thread>
#include <utility>

#if defined(__linux__)
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <malloc.h>
#include <windows.h>
#pragma comment(lib, "Synchronization.lib")
#endif

#if __has_include(<ax/memory/hugetlb.hpp>)
#include <ax/memory/hugetlb.hpp>
#define PCSX2_AX_HAS_AX_HUGETLB 1
#else
#define PCSX2_AX_HAS_AX_HUGETLB 0
#endif

namespace pcsx2_ax::cdvd {

inline constexpr std::size_t kCacheLineBytes     = 64;
inline constexpr std::size_t kPageSizeBytes      = 4096;
inline constexpr std::size_t kHugePage2MiBBytes  = 2u * 1024u * 1024u;
inline constexpr std::size_t kMaxRawSectorBytes  = 2352;
inline constexpr std::size_t kDefaultRingSlots   = 256;

enum class ReadStatus : uint8_t {
	Ok         = 0,
	EndOfMedia = 1,
	IoError    = 2,
};

struct alignas(kPageSizeBytes) SectorSlot {
	uint32_t   lba{0};
	uint16_t   sector_bytes{2048};
	ReadStatus status{ReadStatus::Ok};
	uint8_t    reserved_[57]{};
	uint8_t    payload[kPageSizeBytes - 64]{};

	[[nodiscard]] std::span<uint8_t> writable_span(std::size_t bytes = kMaxRawSectorBytes) noexcept {
		const std::size_t clamped = (bytes <= sizeof(payload)) ? bytes : sizeof(payload);
		return {payload, clamped};
	}

	[[nodiscard]] std::span<const uint8_t> readable_span() const noexcept {
		const std::size_t clamped = (sector_bytes <= sizeof(payload)) ? sector_bytes : sizeof(payload);
		return {payload, clamped};
	}
};

static_assert(sizeof(SectorSlot) == kPageSizeBytes, "SectorSlot must be exactly one 4096-byte page");
static_assert(sizeof(SectorSlot::payload) >= kMaxRawSectorBytes, "Payload must hold a full 2352-byte raw frame");

// Zero-poll event wakeup primitive using eventfd (EFD_SEMAPHORE) on Linux and WaitOnAddress on Windows.
class alignas(kCacheLineBytes) WakeSignal {
public:
	WakeSignal() noexcept {
#if defined(__linux__)
		efd_ = ::eventfd(0u, EFD_NONBLOCK | EFD_CLOEXEC | EFD_SEMAPHORE);
#endif
	}

	~WakeSignal() noexcept {
#if defined(__linux__)
		if (efd_ >= 0)
			::close(efd_);
#endif
	}

	WakeSignal(const WakeSignal&)            = delete;
	WakeSignal& operator=(const WakeSignal&) = delete;

	void set_sleeping(bool sleeping) noexcept {
		sleeping_.store(sleeping, std::memory_order_release);
	}

	[[nodiscard]] bool is_sleeping() const noexcept {
		return sleeping_.load(std::memory_order_acquire);
	}

	void notify() noexcept {
		if (!sleeping_.load(std::memory_order_acquire))
			return;
#if defined(__linux__)
		if (efd_ >= 0) {
			const uint64_t one = 1u;
			(void)::write(efd_, &one, sizeof(one));
		}
#elif defined(_WIN32)
		gen_.fetch_add(1u, std::memory_order_acq_rel);
		::WakeByAddressSingle(&gen_);
#endif
	}

	void notify_all() noexcept {
		sleeping_.store(false, std::memory_order_release);
#if defined(__linux__)
		if (efd_ >= 0) {
			const uint64_t one = 1u;
			(void)::write(efd_, &one, sizeof(one));
		}
#elif defined(_WIN32)
		gen_.fetch_add(1u, std::memory_order_acq_rel);
		::WakeByAddressAll(&gen_);
#endif
	}

	void wait_once() noexcept {
#if defined(__linux__)
		if (efd_ >= 0) {
			uint64_t val = 0u;
			(void)::read(efd_, &val, sizeof(val));
		}
#elif defined(_WIN32)
		DWORD expected = gen_.load(std::memory_order_acquire);
		::WaitOnAddress(&gen_, &expected, sizeof(expected), 50u);
#else
		std::this_thread::yield();
#endif
	}

private:
	std::atomic<bool> sleeping_{false};
#if defined(__linux__)
	int efd_{-1};
#elif defined(_WIN32)
	alignas(4) std::atomic<DWORD> gen_{0u};
#endif
};

class HugeTlbSlab {
public:
	HugeTlbSlab() noexcept = default;
	~HugeTlbSlab() noexcept { release(); }

	HugeTlbSlab(const HugeTlbSlab&)            = delete;
	HugeTlbSlab& operator=(const HugeTlbSlab&) = delete;

	HugeTlbSlab(HugeTlbSlab&& other) noexcept
		: ptr_(std::exchange(other.ptr_, nullptr))
		, mapped_bytes_(std::exchange(other.mapped_bytes_, 0))
		, uses_hugetlb_(std::exchange(other.uses_hugetlb_, false))
		, is_locked_(std::exchange(other.is_locked_, false)) {}

	HugeTlbSlab& operator=(HugeTlbSlab&& other) noexcept {
		if (this != &other) {
			release();
			ptr_          = std::exchange(other.ptr_, nullptr);
			mapped_bytes_ = std::exchange(other.mapped_bytes_, 0);
			uses_hugetlb_ = std::exchange(other.uses_hugetlb_, false);
			is_locked_    = std::exchange(other.is_locked_, false);
		}
		return *this;
	}

	[[nodiscard]] bool allocate(std::size_t requested_bytes) noexcept {
		release();
		if (requested_bytes == 0)
			return false;

#if PCSX2_AX_HAS_AX_HUGETLB
		auto region = ax::memory::allocate_hugetlb_slab(requested_bytes);
		if (region.ptr != nullptr) {
			ptr_          = static_cast<uint8_t*>(region.ptr);
			mapped_bytes_ = region.mapped_bytes;
			uses_hugetlb_ = region.uses_hugetlb;
			is_locked_    = region.is_locked;
			prefault_pages();
			return true;
		}
#endif

#if defined(__linux__)
		const std::size_t huge_aligned = align_up(requested_bytes, kHugePage2MiBBytes);
		void* p = ::mmap(nullptr,
		                 huge_aligned,
		                 PROT_READ | PROT_WRITE,
		                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE,
		                 -1,
		                 0);
		if (p != MAP_FAILED) {
			ptr_          = static_cast<uint8_t*>(p);
			mapped_bytes_ = huge_aligned;
			uses_hugetlb_ = true;
			is_locked_    = true;
			prefault_pages();
			return true;
		}

		const std::size_t page_aligned = align_up(requested_bytes, kPageSizeBytes);
		p = ::mmap(nullptr,
		           page_aligned,
		           PROT_READ | PROT_WRITE,
		           MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
		           -1,
		           0);
		if (p == MAP_FAILED)
			return false;

		ptr_          = static_cast<uint8_t*>(p);
		mapped_bytes_ = page_aligned;
		uses_hugetlb_ = false;
		is_locked_    = (::mlock(ptr_, mapped_bytes_) == 0);
		prefault_pages();
		return true;
#elif defined(_WIN32)
		const std::size_t page_aligned = align_up(requested_bytes, kPageSizeBytes);
		void* p = ::_aligned_malloc(page_aligned, kPageSizeBytes);
		if (p == nullptr)
			return false;
		ptr_          = static_cast<uint8_t*>(p);
		mapped_bytes_ = page_aligned;
		uses_hugetlb_ = false;
		is_locked_    = false;
		prefault_pages();
		return true;
#else
		return false;
#endif
	}

	void release() noexcept {
		if (ptr_ == nullptr)
			return;
#if PCSX2_AX_HAS_AX_HUGETLB
		ax::memory::release_hugetlb_slab(ptr_, mapped_bytes_, uses_hugetlb_);
#elif defined(__linux__)
		if (is_locked_ && !uses_hugetlb_)
			::munlock(ptr_, mapped_bytes_);
		::munmap(ptr_, mapped_bytes_);
#elif defined(_WIN32)
		::_aligned_free(ptr_);
#endif
		ptr_          = nullptr;
		mapped_bytes_ = 0;
		uses_hugetlb_ = false;
		is_locked_    = false;
	}

	[[nodiscard]] uint8_t*    data() const noexcept { return ptr_; }
	[[nodiscard]] std::size_t mapped_bytes() const noexcept { return mapped_bytes_; }
	[[nodiscard]] bool        uses_hugetlb() const noexcept { return uses_hugetlb_; }
	[[nodiscard]] bool        is_locked() const noexcept { return is_locked_; }

private:
	static constexpr std::size_t align_up(std::size_t value, std::size_t alignment) noexcept {
		return (value + (alignment - 1)) & ~(alignment - 1);
	}

	void prefault_pages() noexcept {
		if (ptr_ == nullptr)
			return;
		for (std::size_t offset = 0; offset < mapped_bytes_; offset += kPageSizeBytes)
			ptr_[offset] = 0;
	}

	uint8_t*    ptr_{nullptr};
	std::size_t mapped_bytes_{0};
	bool        uses_hugetlb_{false};
	bool        is_locked_{false};
};

template <std::size_t Capacity = kDefaultRingSlots>
class CdvdSpscRing {
	static_assert(Capacity >= 2 && std::has_single_bit(Capacity),
	              "CdvdSpscRing Capacity must be a power of two >= 2");

public:
	static constexpr std::size_t kCapacity = Capacity;
	static constexpr std::size_t kMask     = Capacity - 1;

	CdvdSpscRing() noexcept = default;
	~CdvdSpscRing() noexcept { shutdown(); }

	CdvdSpscRing(const CdvdSpscRing&)            = delete;
	CdvdSpscRing& operator=(const CdvdSpscRing&) = delete;

	[[nodiscard]] bool init() noexcept {
		shutdown();
		if (!slab_.allocate(sizeof(SectorSlot) * Capacity))
			return false;
		slots_ = new (slab_.data()) SectorSlot[Capacity];
		head_.store(0, std::memory_order_relaxed);
		tail_.store(0, std::memory_order_relaxed);
		return true;
	}

	void shutdown() noexcept {
		if (slots_ != nullptr) {
			for (std::size_t i = 0; i < Capacity; ++i)
				slots_[i].~SectorSlot();
			slots_ = nullptr;
		}
		slab_.release();
	}

	void reset_indices() noexcept {
		head_.store(0, std::memory_order_relaxed);
		tail_.store(0, std::memory_order_release);
	}

	[[nodiscard]] SectorSlot* acquire_write_slot() noexcept {
		const std::size_t head = head_.load(std::memory_order_relaxed);
		const std::size_t tail = tail_.load(std::memory_order_acquire);
		if ((head - tail) >= Capacity)
			return nullptr;
		return &slots_[head & kMask];
	}

	void publish_write_slot() noexcept {
		const std::size_t head = head_.load(std::memory_order_relaxed);
		head_.store(head + 1, std::memory_order_release);
		wake_.notify();
	}

	[[nodiscard]] const SectorSlot* peek_read_slot() const noexcept {
		const std::size_t tail = tail_.load(std::memory_order_relaxed);
		const std::size_t head = head_.load(std::memory_order_acquire);
		if (head == tail)
			return nullptr;
		return &slots_[tail & kMask];
	}

	void release_read_slot() noexcept {
		const std::size_t tail = tail_.load(std::memory_order_relaxed);
		tail_.store(tail + 1, std::memory_order_release);
		wake_.notify();
	}

	[[nodiscard]] std::size_t size() const noexcept {
		const std::size_t head = head_.load(std::memory_order_acquire);
		const std::size_t tail = tail_.load(std::memory_order_acquire);
		return head - tail;
	}

	[[nodiscard]] std::size_t free_slots() const noexcept {
		return Capacity - size();
	}

	[[nodiscard]] bool needs_readahead() const noexcept {
		return free_slots() >= (Capacity / 2);
	}

	[[nodiscard]] WakeSignal& wake_signal() noexcept { return wake_; }
	[[nodiscard]] const WakeSignal& wake_signal() const noexcept { return wake_; }
	[[nodiscard]] bool is_hugetlb_backed() const noexcept { return slab_.uses_hugetlb(); }
	[[nodiscard]] bool is_mlocked() const noexcept { return slab_.is_locked(); }

private:
	HugeTlbSlab slab_{};
	SectorSlot* slots_{nullptr};

	alignas(kCacheLineBytes) std::atomic<std::size_t> head_{0};
	alignas(kCacheLineBytes) std::atomic<std::size_t> tail_{0};
	WakeSignal wake_{};
};

} // namespace pcsx2_ax::cdvd
