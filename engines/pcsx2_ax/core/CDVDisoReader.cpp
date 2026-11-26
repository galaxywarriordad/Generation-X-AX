// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CDVDRingSlab.hpp"
#include "CDVDcommon.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>
#include <thread>

#if __has_include(<ax/topology/pinning.hpp>)
#include <ax/topology/pinning.hpp>
#define PCSX2_AX_HAS_AX_TOPOLOGY 1
#else
#define PCSX2_AX_HAS_AX_TOPOLOGY 0
#endif

namespace pcsx2_ax::cdvd {

class CdvdStreamController {
public:
    [[nodiscard]] bool start(ZeroCopyIsoReader* reader) noexcept {
        reader_ = reader;
        running_.store(true, std::memory_order_release);
        worker_quiesced_.store(false, std::memory_order_relaxed);
        worker_ = std::thread([this]() noexcept { worker_loop(); });
        return true;
    }

    void stop() noexcept {
        running_.store(false, std::memory_order_release);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    // Called on the EE/IOP emulation core to fulfill a sequential or seeked DMA read.
    // Transfers directly from locked HugeTLB SectorSlot into IOP destination buffer.
    [[nodiscard]] bool read_sectors_to_iop(uint32_t start_lba,
                                           uint32_t sector_count,
                                           uint8_t* iop_dst_ptr,
                                           std::size_t stride_bytes) noexcept {
        for (uint32_t i = 0; i < sector_count; ++i) {
            const uint32_t target_lba = start_lba + i;

            const SectorSlot* slot = reader_->peek_sector();
            if (slot == nullptr || slot->lba != target_lba) {
                // Cache miss or non-sequential jump: realign ring to target_lba
                reader_->request_seek(target_lba, worker_quiesced_);
                while ((slot = reader_->peek_sector()) == nullptr) {
                    if (!running_.load(std::memory_order_acquire)) {
                        return false;
                    }
                }
            }

            if (slot->status != ReadStatus::Ok) {
                reader_->consume_sector();
                return false;
            }

            const std::span<const uint8_t> src = slot->readable_span();
            const std::size_t copy_bytes = (src.size() < stride_bytes) ? src.size() : stride_bytes;
            std::memcpy(iop_dst_ptr + (static_cast<std::size_t>(i) * stride_bytes),
                        src.data(),
                        copy_bytes);

            reader_->consume_sector();
        }
        return true;
    }

private:
    void worker_loop() noexcept {
#if PCSX2_AX_HAS_AX_TOPOLOGY
        // Enforce ax-sdk v1_36_2 2:3:1 isolation: keep background I/O off EE/VU1/GS cores
        ax::topology::pin_to_housekeeping_core5();
#endif

        while (running_.load(std::memory_order_acquire)) {
            if (reader_->seek_pending()) {
                worker_quiesced_.store(true, std::memory_order_release);
                while (reader_->seek_pending() && running_.load(std::memory_order_acquire)) {
                    // Wait for consumer seek reset to complete
                }
                worker_quiesced_.store(false, std::memory_order_release);
                continue;
            }

            if (reader_->needs_readahead()) {
                (void)reader_->prefetch_burst();
            } else {
                // Ring is >50% full; yield briefly without burning Core 5
                std::this_thread::yield();
            }
        }
        worker_quiesced_.store(true, std::memory_order_release);
    }

    ZeroCopyIsoReader* reader_{nullptr};
    std::thread        worker_{};
    std::atomic<bool>  running_{false};
    std::atomic<bool>  worker_quiesced_{true};
};

} // namespace pcsx2_ax::cdvd
