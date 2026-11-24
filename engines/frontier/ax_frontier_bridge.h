typedef enum ax_frontier_midi_cmd_e {
  AX_FRONTIER_MIDI_CMD_NONE = 0,
  AX_FRONTIER_MIDI_CMD_TRIM_ROLL_YAW,
  AX_FRONTIER_MIDI_CMD_TRIM_PITCH,
  AX_FRONTIER_MIDI_CMD_THROTTLE_TRIM,
  AX_FRONTIER_MIDI_CMD_CYCLE_DRIVE_MODE,
  AX_FRONTIER_MIDI_CMD_ZOOM_RADAR,
  AX_FRONTIER_MIDI_CMD_CYCLE_HUD_MFD,
  AX_FRONTIER_MIDI_CMD_LASER_CAP_BIAS,
  AX_FRONTIER_MIDI_CMD_SHIELD_DRIVE_BIAS,
  AX_FRONTIER_MIDI_CMD_STEP_TIME_COMPRESSION,
  AX_FRONTIER_MIDI_CMD_RESET_TIME_COMPRESSION_1X,
  AX_FRONTIER_MIDI_CMD_MASTER_AUDIO_TRIM,
  AX_FRONTIER_MIDI_CMD_FIRE_PRIMARY_LASER,
  AX_FRONTIER_MIDI_CMD_LAUNCH_MISSILE,
  AX_FRONTIER_MIDI_CMD_TARGET_NEAREST_HOSTILE,
  AX_FRONTIER_MIDI_CMD_CYCLE_NEXT_TARGET,
  AX_FRONTIER_MIDI_CMD_TOGGLE_AUTOPILOT,
  AX_FRONTIER_MIDI_CMD_ENGAGE_DOCKING_COMPUTER,
  AX_FRONTIER_MIDI_CMD_MATCH_TARGET_VELOCITY,
  AX_FRONTIER_MIDI_CMD_HYPERSPACE_JUMP,
  AX_FRONTIER_MIDI_CMD_JETTISON_OR_ESCAPE_GUARDED,
  AX_FRONTIER_MIDI_CMD_TOGGLE_LANDING_GEAR,
  AX_FRONTIER_MIDI_CMD_ACTIVATE_ECM,
  AX_FRONTIER_MIDI_CMD_OPEN_STAR_MAP,
  AX_FRONTIER_MIDI_CMD_OPEN_SYSTEM_MAP,
  AX_FRONTIER_MIDI_CMD_OPEN_SHIP_STATUS,
  AX_FRONTIER_MIDI_CMD_TOGGLE_TELEMETRY_OVERLAY,
  AX_FRONTIER_MIDI_CMD_CAMERA_OR_MFD_PRESET
} ax_frontier_midi_cmd_t;

typedef struct ax_frontier_midi_event_s {
  ax_frontier_midi_cmd_t command;
  float axis_value;
  int32_t step_delta;
  uint8_t raw_data1;
  uint8_t raw_data2;
  uint8_t sustain_held;
  uint64_t timestamp_ns;
} ax_frontier_midi_event_t;

typedef void (*ax_frontier_midi_handler_fn)(const ax_frontier_midi_event_t* ev, void* user_data);

int ax_frontier_midi_init(const char* alsa_device, uint64_t listener_core_mask, size_t max_events_per_frame);
void ax_frontier_midi_shutdown(void);
size_t ax_frontier_midi_drain_frame(ax_frontier_midi_handler_fn handler, void* user_data, size_t max_events);
