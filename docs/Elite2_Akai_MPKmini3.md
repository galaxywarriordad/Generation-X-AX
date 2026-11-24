# Akai MPK Mini MK3 Control Mapping (`frontier-fe2` / `Generation-X-AX`)

Deterministic USB RawMIDI hardware control specification for **Frontier: Elite II** (`frontier-fe2`) running under the `Generation-X-AX` `2:3:1` domain architecture on `Piccolo-AX` (`x86_64` Buildroot Linux `6.12-rt`).

---

## 1\. Hardware & Deterministic Threading Topology

* **Controller:** Akai Professional MPK Mini MK3 (`hw:CARD=MPKmini3,DEV=0`)  
* **ALSA RawMIDI Listener Thread:** Pinned to **Core 5 (`0x20`, Housekeeping Domain)** via `ax_frontier_midi_init`  
* **Lock-Free Transport:** 64-byte cache-line-aligned SPSC ring buffer (`256` slots, zero mutexes, zero heap allocations on the hot path)  
* **Simulation Drain Thread:** Pinned to **Cores 2–4 (`0x1C`, Guest/Simulation Domain)** via `ax_frontier_step_frame`, bounded to **32 events per frame** (`max_events_per_frame = 32`) prior to advancing physics

### `ax_engine.ini` Reference Block

```
[frontier.midi]
enabled = true
device = hw:CARD=MPKmini3,DEV=0
core_mask = 0x20
ring_capacity = 256
max_events_per_frame = 32
stick_deadzone = 0.06
encoder_step_sensitivity = 1.0
time_compression_guard = true
sustain_shift_cc = 64
```

---

## 2\. Analog Thumbstick & Modifier Pedal (MIDI Channel 1 / Index `0`)

| Physical Control | MIDI Message | Bridge Command ID | Flight / Cockpit Action | Response Curve & Guardrails |
| :---- | :---- | :---- | :---- | :---- |
| **4-Way Thumbstick X** | Pitch Bend (`0xE0`, `-8192..+8191`) | `AX_FRONTIER_MIDI_CMD_TRIM_ROLL_YAW` | Roll / Yaw fine attitude trim | `±6%` center deadzone (`0.06`), quadratic curve (`scaled²`) for docking and dogfight precision |
| **4-Way Thumbstick Y** | Mod Wheel (`CC 1`, `0..127`, center `64`) | `AX_FRONTIER_MIDI_CMD_TRIM_PITCH` | Pitch fine attitude trim | `±6%` center deadzone (`0.06`), quadratic curve (`scaled²`) |
| **Sustain Pedal Jack** | Sustain (`CC 64`, `>= 64` held) | Internal Shift State (`sustain_held = 1`) | Flight safety shift modifier | Required to arm destructive/irreversible actions (`Jettison / Escape Capsule`) |

---

## 3\. Endless Rotary Encoders K1–K8 (MIDI Channel 1 / Index `0`)

| Encoder | MIDI CC | Bridge Command ID | Flight / Cockpit Action | Behavior |
| :---- | :---- | :---- | :---- | :---- |
| **K1** | `CC 70` | `AX_FRONTIER_MIDI_CMD_THROTTLE_TRIM` | Main Thruster / Set-Speed Trim | Normalized `0.0 .. 1.0` (`value / 127.0`) |
| **K2** | `CC 71` | `AX_FRONTIER_MIDI_CMD_CYCLE_DRIVE_MODE` | Flight / Drive Mode Selector | Stepped delta (`+1` if `>= 64`, `-1` if `< 64`) |
| **K3** | `CC 72` | `AX_FRONTIER_MIDI_CMD_ZOOM_RADAR` | Scanner / Radar Range Zoom | Stepped delta (`+1` / `-1`) |
| **K4** | `CC 73` | `AX_FRONTIER_MIDI_CMD_CYCLE_HUD_MFD` | HUD / MFD Sub-Panel Cycle | Stepped delta (`+1` / `-1`) |
| **K5** | `CC 74` | `AX_FRONTIER_MIDI_CMD_LASER_CAP_BIAS` | Weapon / Laser Capacitor Bias | Normalized `0.0 .. 1.0` |
| **K6** | `CC 75` | `AX_FRONTIER_MIDI_CMD_SHIELD_DRIVE_BIAS` | Shield / Drive Power Balance | Normalized `0.0 .. 1.0` |
| **K7** | `CC 76` | `AX_FRONTIER_MIDI_CMD_STEP_TIME_COMPRESSION` | Stardreamer Time Compression Step | Guarded (`time_compression_guard = true`): requires 2 encoder ticks per step to prevent accidental warp spikes |
| **K8** | `CC 77` | `AX_FRONTIER_MIDI_CMD_MASTER_AUDIO_TRIM` | Cockpit / Master Audio Trim | Normalized `0.0 .. 1.0` |

---

## 4\. MPC Drum Pads — Bank A & Bank B (MIDI Channel 10 / Index `9`)

### Bank A (Combat, Targeting & Flight Assist — Notes `36..43`)

| Pad | MIDI Note | Bridge Command ID | Flight / Cockpit Action |
| :---- | :---- | :---- | :---- |
| **Pad 1** | Note `36` | `AX_FRONTIER_MIDI_CMD_FIRE_PRIMARY_LASER` | Fire Primary Pulse / Beam Laser |
| **Pad 2** | Note `37` | `AX_FRONTIER_MIDI_CMD_LAUNCH_MISSILE` | Launch Armed Missile |
| **Pad 3** | Note `38` | `AX_FRONTIER_MIDI_CMD_TARGET_NEAREST_HOSTILE` | Acquire Nearest Hostile Contact |
| **Pad 4** | Note `39` | `AX_FRONTIER_MIDI_CMD_CYCLE_NEXT_TARGET` | Cycle Next Scanner Target |
| **Pad 5** | Note `40` | `AX_FRONTIER_MIDI_CMD_TOGGLE_AUTOPILOT` | Toggle Flight Autopilot |
| **Pad 6** | Note `41` | `AX_FRONTIER_MIDI_CMD_ENGAGE_DOCKING_COMPUTER` | Engage Orbital / Starport Docking Computer |
| **Pad 7** | Note `42` | `AX_FRONTIER_MIDI_CMD_MATCH_TARGET_VELOCITY` | Match Selected Target Velocity |
| **Pad 8** | Note `43` | `AX_FRONTIER_MIDI_CMD_RESET_TIME_COMPRESSION_1X` | Immediate Stardreamer Drop to `1x` Real-Time |

### Bank B (Navigation, Systems & Guarded Actions — Notes `44..51`)

| Pad | MIDI Note | Bridge Command ID | Flight / Cockpit Action | Safety Guardrail |
| :---- | :---- | :---- | :---- | :---- |
| **Pad 9 (B1)** | Note `44` | `AX_FRONTIER_MIDI_CMD_HYPERSPACE_JUMP` | Execute Hyperspace Jump | Requires locked destination system |
| **Pad 10 (B2)** | Note `45` | `AX_FRONTIER_MIDI_CMD_JETTISON_OR_ESCAPE_GUARDED` | Jettison Cargo / Trigger Escape Capsule | **Sustain-Guarded:** Ignored unless `CC 64` (`sustain_held == 1`) is active |
| **Pad 11 (B3)** | Note `46` | `AX_FRONTIER_MIDI_CMD_TOGGLE_LANDING_GEAR` | Deploy / Retract Landing Undercarriage | — |
| **Pad 12 (B4)** | Note `47` | `AX_FRONTIER_MIDI_CMD_ACTIVATE_ECM` | Fire Electronic Countermeasures (ECM) | — |
| **Pad 13 (B5)** | Note `48` | `AX_FRONTIER_MIDI_CMD_OPEN_STAR_MAP` | Open Sector / Galaxy Star Map | — |
| **Pad 14 (B6)** | Note `49` | `AX_FRONTIER_MIDI_CMD_OPEN_SYSTEM_MAP` | Open Local Planetary System Orrery | — |
| **Pad 15 (B7)** | Note `50` | `AX_FRONTIER_MIDI_CMD_OPEN_SHIP_STATUS` | Open Ship Status, Cargo & Equipment Manifest | — |
| **Pad 16 (B8)** | Note `51` | `AX_FRONTIER_MIDI_CMD_TOGGLE_TELEMETRY_OVERLAY` | Toggle `[AX]` Determinism & Frame Telemetry Overlay | — |

---

## 5\. 25-Note Keybed Presets (MIDI Channel 1 / Index `0`)

* **MIDI Notes (`NoteOn`, velocity `> 0`):** Emits `AX_FRONTIER_MIDI_CMD_CAMERA_OR_MFD_PRESET` with `step_delta = midi_note` (`48..72` default octave) and `axis_value = velocity / 127.0` for instant cockpit camera angles, navigation bookmarks, or MFD presets.

---

## 6\. Runtime Verification on `Piccolo-AX`

1. Confirm ALSA exposes the controller node:

```sh
amidi -l
```

   Verify `hw:CARD=MPKmini3,DEV=0` appears in the device list.

   

2. Verify thread affinity under `frontier-fe2`:

```sh
ps -Lo pid,tid,psr,comm -p $(pidof frontier-fe2)
```

   Confirm the simulation thread executes on **Cores 2–4** (`PSR` `2`, `3`, or `4`) and the ALSA RawMIDI listener thread executes on **Core 5** (`PSR` `5`).
