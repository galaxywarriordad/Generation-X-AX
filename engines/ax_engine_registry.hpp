// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/ModeCapabilities.hpp"
#include "frontier/ax_frontier_bridge.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ax::engines {

enum class EngineId : uint8_t {
  Pcsx2Ax = 0u,
  Rmg,
  BlastEm,
  Cannonball,
  Hatari,
  Flycast,
  Frontier,
  Eduke32,
  Snes,
  FbNeo,
};

struct EngineDescriptor {
  EngineId id{EngineId::Pcsx2Ax};
  std::string_view cli_name{};
  std::string_view display_name{};
  std::string_view save_dir{};
  double default_hz{60.0};
  pcsx2_ax::core::HardwareMode bridge_mode{pcsx2_ax::core::HardwareMode::Ps2Console};
  bool supports_midi_mpk3{false};
  std::string_view default_midi_device{};
  std::string_view repo_origin{"Generation-X-AX"};
};

inline constexpr EngineDescriptor kEngineCatalog[] = {
  EngineDescriptor{
    .id = EngineId::Pcsx2Ax,
    .cli_name = "pcsx2_ax",
    .display_name = "PCSX2_AX (PS2 + COH-H/PCSX2x6)",
    .save_dir = "/userdata/saves/ps2",
    .default_hz = 59.94,
    .bridge_mode = pcsx2_ax::core::HardwareMode::Ps2Console,
    .supports_midi_mpk3 = false,
    .default_midi_device = "",
    .repo_origin = "Generation-X-AX",
  },
  EngineDescriptor{
    .id = EngineId::Rmg,
    .cli_name = "rmg",
    .display_name = "RMG (Nintendo 64)",
    .save_dir = "/userdata/saves/n64",
    .default_hz = 60.0,
    .bridge_mode = pcsx2_ax::core::HardwareMode::Ps2Console,
    .supports_midi_mpk3 = false,
    .default_midi_device = "",
    .repo_origin = "Generation-X-AX",
  },
  EngineDescriptor{
    .id = EngineId::BlastEm,
    .cli_name = "blastem",
    .display_name = "BlastEm (Sega Mega Drive)",
    .save_dir = "/userdata/saves/megadrive",
    .default_hz = 60.0,
    .bridge_mode = pcsx2_ax::core::HardwareMode::Ps2Console,
    .supports_midi_mpk3 = false,
    .default_midi_device = "",
    .repo_origin = "Generation-X-AX",
  },
  EngineDescriptor{
    .id = EngineId::Cannonball,
    .cli_name = "cannonball",
    .display_name = "Cannonball (OutRun Arcade)",
    .save_dir = "/userdata/saves/cannonball",
    .default_hz = 60.0,
    .bridge_mode = pcsx2_ax::core::HardwareMode::CohHArcade,
    .supports_midi_mpk3 = false,
    .default_midi_device = "",
    .repo_origin = "Generation-X-AX",
  },
  EngineDescriptor{
    .id = EngineId::Hatari,
    .cli_name = "hatari",
    .display_name = "Hatari (Atari ST/STE)",
    .save_dir = "/userdata/saves/atarist",
    .default_hz = 70.0,
    .bridge_mode = pcsx2_ax::core::HardwareMode::Ps2Console,
    .supports_midi_mpk3 = false,
    .default_midi_device = "",
    .repo_origin = "Generation-X-AX",
  },
  EngineDescriptor{
    .id = EngineId::Flycast,
    .cli_name = "flycast",
    .display_name = "Flycast (Dreamcast / NAOMI)",
    .save_dir = "/userdata/saves/dreamcast",
    .default_hz = 60.0,
    .bridge_mode = pcsx2_ax::core::HardwareMode::CohHArcade,
    .supports_midi_mpk3 = false,
    .default_midi_device = "",
    .repo_origin = "Generation-X-AX",
  },
  EngineDescriptor{
    .id = EngineId::Frontier,
    .cli_name = "frontier",
    .display_name = "Frontier: Elite II (MPK Mini MK3 MIDI)",
    .save_dir = "/userdata/saves/frontier",
    .default_hz = 60.0,
    .bridge_mode = pcsx2_ax::core::HardwareMode::Ps2Console,
    .supports_midi_mpk3 = true,
    .default_midi_device = "hw:CARD=MPKmini3,DEV=0",
    .repo_origin = "Generation-X-AX",
  },
  EngineDescriptor{
    .id = EngineId::Eduke32,
    .cli_name = "eduke32",
    .display_name = "EDuke32 (Build Engine FPS)",
    .save_dir = "/userdata/saves/eduke32",
    .default_hz = 60.0,
    .bridge_mode = pcsx2_ax::core::HardwareMode::Ps2Console,
    .supports_midi_mpk3 = false,
    .default_midi_device = "",
    .repo_origin = "Generation-X-AX",
  },
  EngineDescriptor{
    .id = EngineId::Snes,
    .cli_name = "snes",
    .display_name = "genx-snes-ax (Super Nintendo)",
    .save_dir = "/userdata/saves/snes",
    .default_hz = 60.0,
    .bridge_mode = pcsx2_ax::core::HardwareMode::Ps2Console,
    .supports_midi_mpk3 = false,
    .default_midi_device = "",
    .repo_origin = "Generation-X-AX + Piccolo-AX",
  },
  EngineDescriptor{
    .id = EngineId::FbNeo,
    .cli_name = "fbneo",
    .display_name = "FinalBurn Neo (Coin-Op Arcade)",
    .save_dir = "/userdata/saves/fbneo",
    .default_hz = 60.0,
    .bridge_mode = pcsx2_ax::core::HardwareMode::CohHArcade,
    .supports_midi_mpk3 = false,
    .default_midi_device = "",
    .repo_origin = "Piccolo-AX / Generation-X-AX",
  },
};

[[nodiscard]] inline const EngineDescriptor* find_engine(std::string_view name) noexcept {
  for (const auto& entry : kEngineCatalog) {
    if (entry.cli_name == name) {
      return &entry;
    }
  }
  return nullptr;
}

[[nodiscard]] inline const char* midi_cmd_name(ax_frontier_midi_cmd_t cmd) noexcept {
  switch (cmd) {
    case AX_FRONTIER_MIDI_CMD_TRIM_ROLL_YAW:              return "TRIM_ROLL_YAW";
    case AX_FRONTIER_MIDI_CMD_TRIM_PITCH:                return "TRIM_PITCH";
    case AX_FRONTIER_MIDI_CMD_THROTTLE_TRIM:             return "THROTTLE_TRIM";
    case AX_FRONTIER_MIDI_CMD_CYCLE_DRIVE_MODE:           return "CYCLE_DRIVE_MODE";
    case AX_FRONTIER_MIDI_CMD_ZOOM_RADAR:                return "ZOOM_RADAR";
    case AX_FRONTIER_MIDI_CMD_CYCLE_HUD_MFD:              return "CYCLE_HUD_MFD";
    case AX_FRONTIER_MIDI_CMD_LASER_CAP_BIAS:             return "LASER_CAP_BIAS";
    case AX_FRONTIER_MIDI_CMD_SHIELD_DRIVE_BIAS:          return "SHIELD_DRIVE_BIAS";
    case AX_FRONTIER_MIDI_CMD_STEP_TIME_COMPRESSION:      return "STEP_TIME_COMPRESSION";
    case AX_FRONTIER_MIDI_CMD_RESET_TIME_COMPRESSION_1X:   return "RESET_TIME_COMPRESSION_1X";
    case AX_FRONTIER_MIDI_CMD_MASTER_AUDIO_TRIM:          return "MASTER_AUDIO_TRIM";
    case AX_FRONTIER_MIDI_CMD_FIRE_PRIMARY_LASER:         return "FIRE_PRIMARY_LASER";
    case AX_FRONTIER_MIDI_CMD_LAUNCH_MISSILE:            return "LAUNCH_MISSILE";
    case AX_FRONTIER_MIDI_CMD_TARGET_NEAREST_HOSTILE:     return "TARGET_NEAREST_HOSTILE";
    case AX_FRONTIER_MIDI_CMD_CYCLE_NEXT_TARGET:          return "CYCLE_NEXT_TARGET";
    case AX_FRONTIER_MIDI_CMD_TOGGLE_AUTOPILOT:          return "TOGGLE_AUTOPILOT";
    case AX_FRONTIER_MIDI_CMD_ENGAGE_DOCKING_COMPUTER:    return "ENGAGE_DOCKING_COMPUTER";
    case AX_FRONTIER_MIDI_CMD_MATCH_TARGET_VELOCITY:      return "MATCH_TARGET_VELOCITY";
    case AX_FRONTIER_MIDI_CMD_HYPERSPACE_JUMP:           return "HYPERSPACE_JUMP";
    case AX_FRONTIER_MIDI_CMD_JETTISON_OR_ESCAPE_GUARDED: return "JETTISON_OR_ESCAPE_GUARDED";
    case AX_FRONTIER_MIDI_CMD_TOGGLE_LANDING_GEAR:        return "TOGGLE_LANDING_GEAR";
    case AX_FRONTIER_MIDI_CMD_ACTIVATE_ECM:              return "ACTIVATE_ECM";
    case AX_FRONTIER_MIDI_CMD_OPEN_STAR_MAP:              return "OPEN_STAR_MAP";
    case AX_FRONTIER_MIDI_CMD_OPEN_SYSTEM_MAP:            return "OPEN_SYSTEM_MAP";
    case AX_FRONTIER_MIDI_CMD_OPEN_SHIP_STATUS:           return "OPEN_SHIP_STATUS";
    case AX_FRONTIER_MIDI_CMD_TOGGLE_TELEMETRY_OVERLAY:   return "TOGGLE_TELEMETRY_OVERLAY";
    case AX_FRONTIER_MIDI_CMD_CAMERA_OR_MFD_PRESET:        return "CAMERA_OR_MFD_PRESET";
    case AX_FRONTIER_MIDI_CMD_NONE:
    default:                                             return "NONE";
  }
}

} // namespace ax::engines