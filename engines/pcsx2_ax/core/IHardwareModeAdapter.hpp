// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string_view>

class StateWrapper;

namespace pcsx2_ax::core {

enum class HardwareMode : uint32_t {
	Ps2Console = 0u,
	CohHArcade = 1u,
};

struct BiosDescriptor {
	uint32_t rom_version{0u};
	uint32_t bios_crc{0u};
	std::string_view region_code{"NTSC"};
	bool is_arcade_coh_h{false};
};

class IHardwareModeAdapter {
public:
	virtual ~IHardwareModeAdapter() noexcept = default;

	[[nodiscard]] virtual HardwareMode mode() const noexcept = 0;
	[[nodiscard]] virtual std::string_view save_directory() const noexcept = 0;
	[[nodiscard]] virtual BiosDescriptor bios_descriptor() const noexcept = 0;

	[[nodiscard]] virtual bool initialize() = 0;
	[[nodiscard]] virtual bool reset() = 0;
	virtual void shutdown() noexcept = 0;
	virtual void on_vsync() noexcept = 0;
	[[nodiscard]] virtual bool freeze_state(StateWrapper& wrapper) = 0;
};

} // namespace pcsx2_ax::core
