// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CDVDRingSlab.hpp"
#include "CDVDcommon.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>

namespace pcsx2_ax::cdvd {

class ZeroCopyIsoReader {
public:
    static constexpr std::size_t kMaxBurstSectors = 64; // 64 * 2048 B = 128 KiB per burst

    [[nodiscard]] bool init(uint32_t total_sectors, uint16_t sector_bytes = 2048) noexcept {
        total_sectors_ = total_sectors;
        sector_bytes_  = sector_bytes;
        prefetch_lba_.store(0, std::memory_order_relaxed);
        seek_pending_.store(false, std::memory_order_relaxed);
        return ring_.init();
    }

    void shutdown() noexcept {
        ring_.shutdown();
    }

    // Called on Core 5 (ax::topology::pin_to_housekeeping_core5()) by the disc worker.
    // Decodes sectors directly into locked HugeTLB SectorSlot::payload spans.
    std::size_t prefetch_burst() noexcept {
        if (seek_pending_.load(std::memory_order_acquire) || !ring_.needs_readahead()) {
            return 0;
        }

        std::size_t produced = 0;
        uint32_t lba = prefetch_lba_.load(std::memory_order_relaxed);

        while (produced < kMaxBurstSectors) {
            if (seek_pending_.load(std::memory_order_acquire)) {
                break; // Abort burst immediately if IOP requested a non-sequential seek
            }

            SectorSlot* slot = ring_.acquire_write_slot();
            if (slot == nullptr) {
                break; // Ring full
            }

            slot->lba          = lba;
            slot->sector_bytes = sector_bytes_;

            if (lba >= total_sectors_) {
                slot->status = ReadStatus::EndOfMedia;
                ring_.publish_write_slot();
                ++produced;
                break;
            }

            std::span<uint8_t> dst = slot->writable_span(sector_bytes_);
            const bool ok = read_raw_sector_into(lba, dst);
            slot->status = ok ? ReadStatus::Ok : ReadStatus::IoError;

            ring_.publish_write_slot();
            ++lba;
            ++produced;

            if (!ok) {
                break;
            }
        }

        prefetch_lba_.store(lba, std::memory_order_release);
        return produced;
    }

    // --- Consumer Interface for CDVDdiscThread.cpp ---

    [[nodiscard]] const SectorSlot* peek_sector() const noexcept {
        return ring_.peek_read_slot();
    }

    void consume_sector() noexcept {
        ring_.release_read_slot();
    }

    // Quiesce producer, reset SPSC indices, and retarget read-ahead LBA on seek.
    void request_seek(uint32_t target_lba, std::atomic<bool>& worker_quiesced) noexcept {
        seek_pending_.store(true, std::memory_order_release);
        while (!worker_quiesced.load(std::memory_order_acquire)) {
            // Spin-wait until Core 5 worker exits prefetch_burst()
        }
        ring_.reset_indices();
        prefetch_lba_.store(target_lba, std::memory_order_release);
        seek_pending_.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool seek_pending() const noexcept {
        return seek_pending_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool needs_readahead() const noexcept {
        return ring_.needs_readahead();
    }

private:
    // Hook into existing .iso / .zso / CHD block backend (writes directly to dst, zero heap alloc)
    [[nodiscard]] bool read_raw_sector_into(uint32_t lba, std::span<uint8_t> dst) noexcept;

    CdvdSpscRing<kDefaultRingSlots> ring_{};
    uint32_t                        total_sectors_{0};
    uint16_t                        sector_bytes_{2048};
    std::atomic<uint32_t>           prefetch_lba_{0};
    std::atomic<bool>               seek_pending_{false};
};

} // namespace pcsx2_ax::cdvd
