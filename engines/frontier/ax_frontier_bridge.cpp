#include "ax_midi_mpk3.hpp"
#include <memory>

namespace {

std::unique_ptr<ax::retro::MpkMini3FrontierBridge> g_mpk3_bridge;

ax_frontier_midi_cmd_t MapMidiCommand(ax::retro::FrontierCommandId id) noexcept {
  switch (id) {
    case ax::retro::FrontierCommandId::TrimRollYaw:               return AX_FRONTIER_MIDI_CMD_TRIM_ROLL_YAW;
    case ax::retro::FrontierCommandId::TrimPitch:                 return AX_FRONTIER_MIDI_CMD_TRIM_PITCH;
    case ax::retro::FrontierCommandId::ThrottleTrim:              return AX_FRONTIER_MIDI_CMD_THROTTLE_TRIM;
    case ax::retro::FrontierCommandId::CycleDriveMode:            return AX_FRONTIER_MIDI_CMD_CYCLE_DRIVE_MODE;
    case ax::retro::FrontierCommandId::ZoomRadar:                 return AX_FRONTIER_MIDI_CMD_ZOOM_RADAR;
    case ax::retro::FrontierCommandId::CycleHudMfd:               return AX_FRONTIER_MIDI_CMD_CYCLE_HUD_MFD;
    case ax::retro::FrontierCommandId::LaserCapBias:              return AX_FRONTIER_MIDI_CMD_LASER_CAP_BIAS;
    case ax::retro::FrontierCommandId::ShieldDriveBias:           return AX_FRONTIER_MIDI_CMD_SHIELD_DRIVE_BIAS;
    case ax::retro::FrontierCommandId::StepTimeCompression:       return AX_FRONTIER_MIDI_CMD_STEP_TIME_COMPRESSION;
    case ax::retro::FrontierCommandId::ResetTimeCompression1x:    return AX_FRONTIER_MIDI_CMD_RESET_TIME_COMPRESSION_1X;
    case ax::retro::FrontierCommandId::MasterAudioTrim:           return AX_FRONTIER_MIDI_CMD_MASTER_AUDIO_TRIM;
    case ax::retro::FrontierCommandId::FirePrimaryLaser:          return AX_FRONTIER_MIDI_CMD_FIRE_PRIMARY_LASER;
    case ax::retro::FrontierCommandId::LaunchMissile:             return AX_FRONTIER_MIDI_CMD_LAUNCH_MISSILE;
    case ax::retro::FrontierCommandId::TargetNearestHostile:      return AX_FRONTIER_MIDI_CMD_TARGET_NEAREST_HOSTILE;
    case ax::retro::FrontierCommandId::CycleNextTarget:           return AX_FRONTIER_MIDI_CMD_CYCLE_NEXT_TARGET;
    case ax::retro::FrontierCommandId::ToggleAutopilot:           return AX_FRONTIER_MIDI_CMD_TOGGLE_AUTOPILOT;
    case ax::retro::FrontierCommandId::EngageDockingComputer:     return AX_FRONTIER_MIDI_CMD_ENGAGE_DOCKING_COMPUTER;
    case ax::retro::FrontierCommandId::MatchTargetVelocity:       return AX_FRONTIER_MIDI_CMD_MATCH_TARGET_VELOCITY;
    case ax::retro::FrontierCommandId::HyperspaceJump:            return AX_FRONTIER_MIDI_CMD_HYPERSPACE_JUMP;
    case ax::retro::FrontierCommandId::JettisonOrEscapeGuarded:  return AX_FRONTIER_MIDI_CMD_JETTISON_OR_ESCAPE_GUARDED;
    case ax::retro::FrontierCommandId::ToggleLandingGear:         return AX_FRONTIER_MIDI_CMD_TOGGLE_LANDING_GEAR;
    case ax::retro::FrontierCommandId::ActivateEcm:               return AX_FRONTIER_MIDI_CMD_ACTIVATE_ECM;
    case ax::retro::FrontierCommandId::OpenStarMap:               return AX_FRONTIER_MIDI_CMD_OPEN_STAR_MAP;
    case ax::retro::FrontierCommandId::OpenSystemMap:             return AX_FRONTIER_MIDI_CMD_OPEN_SYSTEM_MAP;
    case ax::retro::FrontierCommandId::OpenShipStatus:            return AX_FRONTIER_MIDI_CMD_OPEN_SHIP_STATUS;
    case ax::retro::FrontierCommandId::ToggleTelemetryOverlay:    return AX_FRONTIER_MIDI_CMD_TOGGLE_TELEMETRY_OVERLAY;
    case ax::retro::FrontierCommandId::CameraOrMfdPreset:         return AX_FRONTIER_MIDI_CMD_CAMERA_OR_MFD_PRESET;
    case ax::retro::FrontierCommandId::None:
    default:                                                      return AX_FRONTIER_MIDI_CMD_NONE;
  }
}

} // namespace

extern "C" {

int ax_frontier_midi_init(const char* alsa_device, uint64_t listener_core_mask, size_t max_events_per_frame) {
  ax::retro::MpkMini3Config cfg{};
  cfg.device = (alsa_device && alsa_device[0] != '\0') ? alsa_device : "hw:CARD=MPKmini3,DEV=0";
  cfg.listener_core_mask = (listener_core_mask != 0) ? listener_core_mask : 0x20ULL; // Core 5
  cfg.max_events_per_frame = (max_events_per_frame > 0) ? max_events_per_frame : 32;

  g_mpk3_bridge = std::make_unique<ax::retro::MpkMini3FrontierBridge>(cfg);
  return g_mpk3_bridge->Start() ? 0 : -1;
}

void ax_frontier_midi_shutdown(void) {
  if (g_mpk3_bridge) {
    g_mpk3_bridge->Stop();
    g_mpk3_bridge.reset();
  }
}

size_t ax_frontier_midi_drain_frame(ax_frontier_midi_handler_fn handler, void* user_data, size_t max_events) {
  if (!g_mpk3_bridge || !handler) {
    return 0;
  }
  return g_mpk3_bridge->DrainFrameEvents(
      [handler, user_data](const ax::retro::FrontierInputCommand& cmd) {
        ax_frontier_midi_event_t out{};
        out.command = MapMidiCommand(cmd.command);
        out.axis_value = cmd.axis_value;
        out.step_delta = cmd.step_delta;
        out.raw_data1 = cmd.raw_data1;
        out.raw_data2 = cmd.raw_data2;
        out.sustain_held = cmd.sustain_held ? 1u : 0u;
        out.timestamp_ns = cmd.timestamp_ns;
        if (out.command != AX_FRONTIER_MIDI_CMD_NONE) {
          handler(&out, user_data);
        }
      },
      max_events);
}

} // extern "C"
