// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/IHardwareModeAdapter.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace pcsx2_ax::adapters::coh_h {

class CohHArcadeAdapter final : public pcsx2_ax::core::IHardwareModeAdapter {
public:
    static constexpr std::string_view kSaveRoot = "/userdata/saves/pcsx2x6/";

    CohHArcadeAdapter() noexcept = default;
    ~CohHArcadeAdapter() noexcept override { shutdown(); }

    CohHArcadeAdapter(const CohHArcadeAdapter&)            = delete;
    CohHArcadeAdapter& operator=(const CohHArcadeAdapter&) = delete;

    [[nodiscard]] pcsx2_ax::core::HardwareMode mode() const noexcept override {
        return pcsx2_ax::core::HardwareMode::CohHArcade;
    }

    [[nodiscard]] std::string_view save_directory() const noexcept override {
        return kSaveRoot;
    }

    [[nodiscard]] bool initialize() noexcept override {
        if (initialized_) {
            return true;
        }
        // Prototype foundation hook: wire ACCORE, ACATA/ACATAPI/ACATA_IO_CHD, ACRAM, ACSRAM, ACJV, ACUART
        bios_info_ = pcsx2_ax::core::BiosDescriptor{
            .rom_version     = 0x0246u,
            .bios_crc        = 0u,
            .region_code     = "COH-H",
            .is_arcade_coh_h = true,
        };
        initialized_ = true;
        return true;
    }

    [[nodiscard]] bool reset() noexcept override {
        if (!initialized_) {
            return initialize();
        }
        vsync_ticks_ = 0;
        return true;
    }

    void shutdown() noexcept override {
        if (!initialized_) {
            return;
        }
        // Prototype foundation hook: commit ACSRAM/NVRAM via atomic .tmp -> fsync -> rename
        initialized_ = false;
    }

    void on_vsync() noexcept override {
        if (!initialized_) {
            return;
        }
        ++vsync_ticks_;
    }

    [[nodiscard]] pcsx2_ax::core::BiosDescriptor bios_descriptor() const noexcept override {
        return bios_info_;
    }

    [[nodiscard]] bool read_media_sectors(uint32_t lba,
                                          uint32_t sector_count,
                                          std::span<uint8_t> out_bytes,
                                          uint16_t& out_sector_bytes) noexcept override {
        constexpr uint16_t kDefaultChdSectorSize = 2048u;
        const std::size_t required = static_cast<std::size_t>(sector_count) * kDefaultChdSectorSize;
        if (!initialized_ || sector_count == 0 || out_bytes.size() < required) {
            return false;
        }
        (void)lba;
        // Prototype foundation hook: route CHD hunk reads into ring slot / /dev/shm scratch
        out_sector_bytes = kDefaultChdSectorSize;
        return true;
    }

    bool freeze_state(StateWrapper& wrapper) noexcept override {
        (void)wrapper;
        return initialized_;
    }

    [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }
    [[nodiscard]] uint64_t vsync_ticks() const noexcept { return vsync_ticks_; }

private:
    bool                           initialized_{false};
    uint64_t                       vsync_ticks_{0};
    pcsx2_ax::core::BiosDescriptor bios_info_{};
};

} // namespace pcsx2_ax::adapters::coh_h
