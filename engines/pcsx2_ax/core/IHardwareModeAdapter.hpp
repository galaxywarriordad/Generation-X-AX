// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

class StateWrapper;

namespace pcsx2_ax::core {

enum class HardwareMode : uint8_t {
    Ps2Console = 0, // Retail PS2 (--mode ps2 -> /userdata/saves/ps2/)
    CohHArcade = 1, // Namco System 246/256 (--mode coh-h -> /userdata/saves/pcsx2x6/)
};

struct BiosDescriptor {
    uint32_t         rom_version{0};
    uint32_t         bios_crc{0};
    std::string_view region_code{};
    bool             is_arcade_coh_h{false};
};

// Narrow boundary interface inside libax_pcsx2_core so shared core sources
// (VMManager, SaveState, CDVD, R5900OpcodeImpl) never directly include adapters/ps2/.
class IHardwareModeAdapter {
public:
    virtual ~IHardwareModeAdapter() noexcept = default;

    [[nodiscard]] virtual HardwareMode mode() const noexcept = 0;
    [[nodiscard]] virtual std::string_view save_directory() const noexcept = 0;

    [[nodiscard]] virtual bool initialize() noexcept = 0;
    [[nodiscard]] virtual bool reset() noexcept = 0;
    virtual void shutdown() noexcept = 0;
    virtual void on_vsync() noexcept = 0;

    [[nodiscard]] virtual BiosDescriptor bios_descriptor() const noexcept = 0;
    [[nodiscard]] virtual bool read_media_sectors(uint32_t lba,
                                                  uint32_t sector_count,
                                                  std::span<uint8_t> out_bytes,
                                                  uint16_t& out_sector_bytes) noexcept = 0;

    virtual bool freeze_state(StateWrapper& wrapper) noexcept = 0;
};

} // namespace pcsx2_ax::core
