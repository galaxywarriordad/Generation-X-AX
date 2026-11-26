// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/IHardwareModeAdapter.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace pcsx2_ax::adapters::ps2 {

class Ps2ConsoleAdapter final : public pcsx2_ax::core::IHardwareModeAdapter {
public:
    static constexpr std::string_view kSaveRoot = "/userdata/saves/ps2/";

    Ps2ConsoleAdapter() noexcept = default;
    ~Ps2ConsoleAdapter() noexcept override { shutdown(); }

    Ps2ConsoleAdapter(const Ps2ConsoleAdapter&)            = delete;
    Ps2ConsoleAdapter& operator=(const Ps2ConsoleAdapter&) = delete;

    [[nodiscard]] pcsx2_ax::core::HardwareMode mode() const noexcept override {
        return pcsx2_ax::core::HardwareMode::Ps2Console;
    }

    [[nodiscard]] std::string_view save_directory() const noexcept override {
        return kSaveRoot;
    }

    [[nodiscard]] bool initialize() noexcept override {
        if (initialized_) {
            return true;
        }
        // Prototype foundation hook: wire BiosTools, IopBios, CDVD, SIO2, and Pad init here
        bios_info_ = pcsx2_ax::core::BiosDescriptor{
            .rom_version     = 0x0220u,
            .bios_crc        = 0u,
            .region_code     = "PS2",
            .is_arcade_coh_h = false,
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
        // Prototype foundation hook: flush/release retail PS2 disc and memory card state
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
        constexpr uint16_t kDefaultPs2SectorSize = 2048u;
        const std::size_t required = static_cast<std::size_t>(sector_count) * kDefaultPs2SectorSize;
        if (!initialized_ || sector_count == 0 || out_bytes.size() < required) {
            return false;
        }
        (void)lba;
        // Prototype foundation hook: delegate sector reads to CDVDdiscReader / CdvdSpscRing
        out_sector_bytes = kDefaultPs2SectorSize;
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

} // namespace pcsx2_ax::adapters::ps2
