// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team & The Silicon Preservation Society
// SPDX-License-Identifier: GPL-3.0+

#include "CDVDRingSlab.hpp"
#include "IHardwareModeAdapter.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <queue>
#include <span>
#include <thread>

#if __has_include(<ax/topology/pinning.hpp>)
#include <ax/topology/pinning.hpp>
#define PCSX2_AX_CDVD_HAS_AX_TOPOLOGY 1
#else
#define PCSX2_AX_CDVD_HAS_AX_TOPOLOGY 0
#endif

namespace pcsx2_ax::cdvd {

// Decoupled block-device interface so core/CDVDdiscThread.cpp never includes
// adapters/ps2/CDVD.h or adapters/ps2/CDVDdiscReader.h directly.
class ICdvdDiscSource {
public:
    virtual ~ICdvdDiscSource() noexcept = default;

    [[nodiscard]] virtual bool is_disc_ready() const noexcept = 0;
    [[nodiscard]] virtual uint32_t sector_count() const noexcept = 0;
    // >= 0 for DVD/2048-byte media, < 0 for CD/2352-byte raw media
    [[nodiscard]] virtual int32_t media_type() const noexcept = 0;
    [[nodiscard]] virtual bool read_sectors_2048(uint32_t lba, uint32_t count, std::span<uint8_t> dst) noexcept = 0;
    [[nodiscard]] virtual bool read_sectors_2352(uint32_t lba, uint32_t count, std::span<uint8_t> dst) noexcept = 0;

    virtual void on_tray_changed(bool tray_open, bool disc_inserted) noexcept = 0;
    virtual void on_refresh_toc() noexcept = 0;
};

namespace {

inline constexpr uint32_t kSectorsPerRead   = 16u;
inline constexpr uint32_t kRawSectorBytes   = 2352u;
inline constexpr uint32_t kBlockRawBytes    = kRawSectorBytes * kSectorsPerRead; // 37,632 B
inline constexpr uint32_t kCacheIndexBits   = 12u; // 4096 block slots
inline constexpr uint32_t kCacheSlotCount   = 1u << kCacheIndexBits;
inline constexpr uint32_t kCacheSlotMask    = kCacheSlotCount - 1u;
inline constexpr uint32_t kInvalidLsn       = std::numeric_limits<uint32_t>::max();
inline constexpr uint32_t kMaxPrefetchBurst = 16u;

// PS2 CDVD sector read modes (matches upstream CDVD_MODE_* constants without pulling CDVD.h)
inline constexpr int32_t kMode2048 = 0;
inline constexpr int32_t kMode2328 = 1;
inline constexpr int32_t kMode2340 = 2;

static_assert(kSectorsPerRead > 1u && (kSectorsPerRead & (kSectorsPerRead - 1u)) == 0u,
              "kSectorsPerRead must be a power of two");

struct alignas(kCacheLineBytes) CacheBlockSlot {
    std::atomic<uint32_t> lsn{kInvalidLsn};
    std::array<uint8_t, kBlockRawBytes> data{};
};

class DiscThreadRuntime {
public:
    void set_adapter(pcsx2_ax::core::IHardwareModeAdapter* adapter, ICdvdDiscSource* source) noexcept {
        std::lock_guard<std::mutex> lock(state_lock_);
        adapter_ = adapter;
        source_  = source;
    }

    [[nodiscard]] pcsx2_ax::core::IHardwareModeAdapter* adapter() const noexcept {
        return adapter_;
    }

    [[nodiscard]] ICdvdDiscSource* source() const noexcept {
        return source_;
    }

    bool start() {
        ensure_cache_allocated();
        reset_cache();

        bool expected = false;
        if (thread_open_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            io_thread_ = std::thread([this]() noexcept { run_io_loop(); });
        }
        return true;
    }

    void stop() {
        thread_open_.store(false, std::memory_order_release);
        notify_cv_.notify_one();
        if (io_thread_.joinable()) {
            io_thread_.join();
        }
    }

    void request_sector(uint32_t sector, int32_t /*mode*/) {
        ICdvdDiscSource* src = source_;
        if (src == nullptr || sector >= src->sector_count()) {
            return;
        }

        const uint32_t aligned_lsn = sector & ~(kSectorsPerRead - 1u);
        if (cache_contains(aligned_lsn)) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(queue_lock_);
            request_queue_.push(aligned_lsn);
        }
        notify_cv_.notify_one();
    }

    uint8_t* get_sector(uint32_t sector, int32_t mode, std::array<uint8_t, kBlockRawBytes>& tls_block) {
        ICdvdDiscSource* src = source_;
        if (src == nullptr) {
            return tls_block.data();
        }

        const uint32_t sector_block = sector & ~(kSectorsPerRead - 1u);
        if (!cache_fetch(sector_block, tls_block.data())) {
            if (read_block_of_sectors(sector_block, tls_block.data())) {
                cache_update(sector_block, tls_block.data());
            }
        }

        const int32_t media = src->media_type();
        if (media >= 0) {
            const uint32_t offset = 2048u * (sector - sector_block);
            return tls_block.data() + offset;
        }

        const uint32_t offset = kRawSectorBytes * (sector - sector_block);
        uint8_t* frame = tls_block.data() + offset;

        switch (mode) {
            case kMode2048:
                return ((frame[15] & 3u) == 2u) ? (frame + 24) : (frame + 16);
            case kMode2328:
                return frame + 24;
            case kMode2340:
                return frame + 12;
            default:
                return frame;
        }
    }

    int32_t direct_read_sector(uint32_t sector,
                               int32_t mode,
                               uint8_t* out_buffer,
                               std::array<uint8_t, kBlockRawBytes>& tls_block) {
        ICdvdDiscSource* src = source_;
        if (src == nullptr || out_buffer == nullptr || sector >= src->sector_count()) {
            return -1;
        }

        const uint32_t sector_block = sector & ~(kSectorsPerRead - 1u);
        if (!cache_fetch(sector_block, tls_block.data())) {
            if (read_block_of_sectors(sector_block, tls_block.data())) {
                cache_update(sector_block, tls_block.data());
            }
        }

        const int32_t media = src->media_type();
        if (media >= 0) {
            const uint32_t offset = 2048u * (sector - sector_block);
            std::memcpy(out_buffer, tls_block.data() + offset, 2048u);
            return 0;
        }

        const uint32_t offset = kRawSectorBytes * (sector - sector_block);
        const uint8_t* frame = tls_block.data() + offset;

        switch (mode) {
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

    void refresh_data() {
        ICdvdDiscSource* src = source_;
        if (src != nullptr) {
            src->on_refresh_toc();
        }
        reset_cache();
    }

private:
    static constexpr uint32_t sector_hash(uint32_t lsn) noexcept {
        uint32_t hash = 0u;
        int shift = 32;
        constexpr uint32_t mask = kCacheSlotMask;
        while (shift >= 0) {
            hash ^= (lsn & mask);
            lsn >>= kCacheIndexBits;
            shift -= static_cast<int>(kCacheIndexBits);
        }
        return hash & mask;
    }

    void ensure_cache_allocated() noexcept {
        if (cache_slots_ != nullptr) {
            return;
        }
        const std::size_t bytes = sizeof(CacheBlockSlot) * kCacheSlotCount;
        if (cache_slab_.allocate(bytes)) {
            cache_slots_ = new (cache_slab_.data()) CacheBlockSlot[kCacheSlotCount];
            owns_slab_   = true;
        } else {
            heap_fallback_slots_ = std::make_unique<CacheBlockSlot[]>(kCacheSlotCount);
            cache_slots_         = heap_fallback_slots_.get();
            owns_slab_           = false;
        }
    }

    void reset_cache() noexcept {
        if (cache_slots_ == nullptr) {
            return;
        }
        for (uint32_t i = 0; i < kCacheSlotCount; ++i) {
            cache_slots_[i].lsn.store(kInvalidLsn, std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_release);
    }

    [[nodiscard]] bool cache_contains(uint32_t lsn) const noexcept {
        if (cache_slots_ == nullptr) {
            return false;
        }
        const uint32_t slot = sector_hash(lsn);
        return cache_slots_[slot].lsn.load(std::memory_order_acquire) == lsn;
    }

    void cache_update(uint32_t lsn, const uint8_t* block_data) noexcept {
        if (cache_slots_ == nullptr) {
            return;
        }
        CacheBlockSlot& slot = cache_slots_[sector_hash(lsn)];
        slot.lsn.store(kInvalidLsn, std::memory_order_release);
        std::memcpy(slot.data.data(), block_data, kBlockRawBytes);
        slot.lsn.store(lsn, std::memory_order_release);
    }

    [[nodiscard]] bool cache_fetch(uint32_t lsn, uint8_t* out_block_data) const noexcept {
        if (cache_slots_ == nullptr) {
            return false;
        }
        CacheBlockSlot& slot = cache_slots_[sector_hash(lsn)];
        const uint32_t current_lsn = slot.lsn.load(std::memory_order_acquire);
        if (current_lsn != lsn) {
            return false;
        }
        std::memcpy(out_block_data, slot.data.data(), kBlockRawBytes);
        // Verify LSN did not change mid-copy
        return slot.lsn.load(std::memory_order_acquire) == lsn;
    }

    [[nodiscard]] bool read_block_of_sectors(uint32_t sector, uint8_t* dst) noexcept {
        ICdvdDiscSource* src = source_;
        if (src == nullptr) {
            return false;
        }

        const uint32_t total = src->sector_count();
        if (sector >= total) {
            return false;
        }

        const uint32_t count = std::min(kSectorsPerRead, total - sector);
        const int32_t media = src->media_type();
        std::span<uint8_t> out_span{dst, kBlockRawBytes};

        for (int tries = 0; tries < 2; ++tries) {
            if (media >= 0) {
                if (src->read_sectors_2048(sector, count, out_span)) {
                    return true;
                }
            } else {
                if (src->read_sectors_2352(sector, count, out_span)) {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] bool update_disc_status() noexcept {
        ICdvdDiscSource* src = source_;
        if (src == nullptr) {
            return true;
        }

        const bool ready = src->is_disc_ready();
        if (!ready) {
            if (!disc_has_changed_) {
                disc_has_changed_ = true;
                src->on_tray_changed(/*tray_open=*/true, /*disc_inserted=*/false);
            }
        } else if (disc_has_changed_) {
            disc_has_changed_ = false;
            src->on_tray_changed(/*tray_open=*/false, /*disc_inserted=*/true);
            refresh_data();
            {
                std::lock_guard<std::mutex> lock(queue_lock_);
                std::queue<uint32_t> empty{};
                request_queue_.swap(empty);
            }
        }
        return !ready;
    }

    void run_io_loop() noexcept {
#if PCSX2_AX_CDVD_HAS_AX_TOPOLOGY
        // Pin background disc/CHD I/O strictly to housekeeping core under ax-sdk 2:3:1 topology
        ax::topology::pin_to_housekeeping_core5();
#endif

        std::array<uint8_t, kBlockRawBytes> io_buffer{};
        uint32_t prefetches_left = 0u;

        std::printf(" * CDVD: IO thread started...\n");
        std::unique_lock<std::mutex> lock(notify_lock_);

        while (thread_open_.load(std::memory_order_acquire)) {
            if (update_disc_status()) {
                notify_cv_.wait_for(lock, std::chrono::milliseconds(10));
                prefetches_left = 0u;
                continue;
            }

            if (prefetches_left == 0u) {
                notify_cv_.wait_for(lock, std::chrono::milliseconds(250));
            }

            if (!thread_open_.load(std::memory_order_acquire)) {
                break;
            }

            bool handling_request = false;
            uint32_t request_lsn = 0u;

            {
                std::lock_guard<std::mutex> q_lock(queue_lock_);
                if (!request_queue_.empty()) {
                    request_lsn = request_queue_.front();
                    request_queue_.pop();
                    handling_request = true;
                }
            }

            if (!handling_request) {
                if (prefetches_left == 0u) {
                    continue;
                }
                --prefetches_left;
                request_lsn = last_block_lsn_.load(std::memory_order_relaxed) + kSectorsPerRead;
            }

            if (!cache_contains(request_lsn)) {
                if (read_block_of_sectors(request_lsn, io_buffer.data())) {
                    cache_update(request_lsn, io_buffer.data());
                } else {
                    prefetches_left = 0u;
                    continue;
                }
            }

            last_block_lsn_.store(request_lsn, std::memory_order_release);

            if (!handling_request) {
                continue;
            }

            ICdvdDiscSource* src = source_;
            const uint32_t total = (src != nullptr) ? src->sector_count() : 0u;
            const uint32_t next_lsn = request_lsn + kSectorsPerRead;
            if (next_lsn >= total) {
                prefetches_left = 0u;
            } else {
                const uint32_t remaining = total - next_lsn;
                const uint32_t available_blocks = (remaining + kSectorsPerRead - 1u) / kSectorsPerRead;
                prefetches_left = std::min(available_blocks, kMaxPrefetchBurst);
            }
        }

        std::printf(" * CDVD: IO thread finished.\n");
    }

    std::mutex state_lock_{};
    pcsx2_ax::core::IHardwareModeAdapter* adapter_{nullptr};
    ICdvdDiscSource* source_{nullptr};

    HugeTlbSlab cache_slab_{};
    std::unique_ptr<CacheBlockSlot[]> heap_fallback_slots_{};
    CacheBlockSlot* cache_slots_{nullptr};
    bool owns_slab_{false};

    std::thread io_thread_{};
    std::mutex notify_lock_{};
    std::condition_variable notify_cv_{};
    std::mutex queue_lock_{};
    std::queue<uint32_t> request_queue_{};

    std::atomic<bool> thread_open_{false};
    std::atomic<uint32_t> last_block_lsn_{0u};
    bool disc_has_changed_{false};
};

DiscThreadRuntime& runtime() noexcept {
    static DiscThreadRuntime instance{};
    return instance;
}

} // namespace

void bind_cdvd_adapter(pcsx2_ax::core::IHardwareModeAdapter* adapter, ICdvdDiscSource* source) noexcept {
    runtime().set_adapter(adapter, source);
}

} // namespace pcsx2_ax::cdvd

// Legacy C entry points preserved for libax_pcsx2_core callers
void cdvdStartThread() {
    (void)pcsx2_ax::cdvd::runtime().start();
}

void cdvdStopThread() {
    pcsx2_ax::cdvd::runtime().stop();
}

void cdvdRequestSector(uint32_t sector, int32_t mode) {
    pcsx2_ax::cdvd::runtime().request_sector(sector, mode);
}

uint8_t* cdvdGetSector(uint32_t sector, int32_t mode) {
    thread_local std::array<uint8_t, pcsx2_ax::cdvd::kBlockRawBytes> tls_block{};
    return pcsx2_ax::cdvd::runtime().get_sector(sector, mode, tls_block);
}

int32_t cdvdDirectReadSector(uint32_t sector, int32_t mode, uint8_t* buffer) {
    thread_local std::array<uint8_t, pcsx2_ax::cdvd::kBlockRawBytes> tls_block{};
    return pcsx2_ax::cdvd::runtime().direct_read_sector(sector, mode, buffer, tls_block);
}

void cdvdRefreshData() {
    pcsx2_ax::cdvd::runtime().refresh_data();
}
