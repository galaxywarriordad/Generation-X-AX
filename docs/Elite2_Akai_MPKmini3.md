# Akai MPK Mini MK3 (`MPKmini3`) Integration & Control Mapping for *Frontier: Elite II* (`Generation-X-AX`)

**Subsystem:** `Generation-X-AX` (`engines/frontier` \+ `shared/ax_retro_bridge`)  
**SDK Baseline:** `ax-sdk` `v1_36_2` (`ax::core`, C++20)  
**Target Topology:** 6-Core `x86_64` (`2:3:1` Core/IRQ Partitioning)  
**Hardware Peripheral:** Akai Professional MPK Mini MK3 (`VID 0x09E8` / `PID 0x0049`, ALSA RawMIDI `hw:CARD=MPKmini3,DEV=0`)

---

## 1\. Architectural Integration (`2:3:1` Core Isolation & SPSC Lock-Free Ring)

Mapping the Akai MPK Mini MK3 controller to *Frontier: Elite II* inside `Generation-X-AX` reuses the existing lock-free SPSC listener architecture (`MpkMini3Listener`, modeled on `FLAC_AX`'s `src/midi/mpk_mini3.hpp` & `.cpp`) rather than introducing a blocking or duplicate MIDI stack.

### Thread & Core Separation

| Execution Domain | Core Mask (`2:3:1`) | Scheduling Policy | Responsibility in *Frontier: Elite II* |
| :---- | :---- | :---- | :---- |
| **Audio & Real-Time Timing** | Core 0 (`0x01`) | `SCHED_FIFO` | Zero-underrun audio output and deterministic frame/tick pacing |
| **Vulkan / OpenGL Presentation** | Core 1 (`0x02`) | `SCHED_FIFO` | Command submission, HUD/cockpit rendering, and scanout |
| **Simulation & Input Drain** | Cores 2–4 (`0x1C`) | `SCHED_OTHER` / Isolated | Flight physics, 68k/guest execution, and bounded per-frame SPSC MIDI event drain (`<= 32` events/frame) |
| **Housekeeping & USB RawMIDI** | Core 5 (`0x20`) | `SCHED_OTHER` | `MpkMini3Listener` ALSA RawMIDI polling (`hw:CARD=MPKmini3,DEV=0`), hot-plug/unplug recovery, and `ax_engine.ini` I/O |

### Design Guardrails

* **Zero Contention on Real-Time/Simulation Cores:** All USB-MIDI descriptor probing, ALSA RawMIDI polling, and disconnect/reconnect backoff run strictly on **Core 5 (`0x20`)**. Events are pushed into a 64-byte cache-line-aligned lock-free SPSC ring buffer so flight physics and audio clocks never block on USB I/O.  
* **Graceful Fallback via `ax-sdk` (`v1_36_2`):** The MIDI bridge is gated behind `ax_retro_bridge` (`ax::core`) and `ax_engine.ini`. If the MPK Mini MK3 is disconnected or disabled, *Frontier* runs identically with standard keyboard, mouse, and gamepad inputs.  
* **DAC & Session Arbitration (`/run/lock/ax-dac.lock`):** When `frontier` runs under `Piccolo-AX` or alongside `FLAC_AX` / `ax_av_studio`, MIDI events stay scoped to Frontier's flight/cockpit controls without acquiring audio DAC locks.

---

## 2\. Physical Control Layout for *Frontier: Elite II*

### Analog & Rotary Controls (Thumbstick \+ Encoders K1–K8)

| MPK Mini MK3 Control | MIDI Signal | *Frontier: Elite II* Action | Operational Notes & Tuning |
| :---- | :---- | :---- | :---- |
| **Thumbstick X-Axis** | Pitch Bend (Ch 1, 14-bit) | **Roll / Yaw Fine Trim** | Apply a `±6%` center deadzone and exponential response curve for smooth dogfighting and station docking alignment |
| **Thumbstick Y-Axis** | CC 1 (Mod Wheel, Ch 1\) | **Pitch Up / Down Trim** | Natural elevator-style pitch control without hunting keyboard keys |
| **Encoder K1** | CC 70 (Ch 1\) | **Main Thruster / Set-Speed Trim** | Incremental throttle adjustment (`0%–100%` target velocity) |
| **Encoder K2** | CC 71 (Ch 1\) | **Drive Mode Selector** | Cycles Manual Thrust, Set-Speed Cruise, and Engines Off |
| **Encoder K3** | CC 72 (Ch 1\) | **Scanner / Tactical Radar Zoom** | Steps radar range in/out during system navigation and combat |
| **Encoder K4** | CC 73 (Ch 1\) | **HUD / MFD Panel Cycle** | Cycles Navigation, Orbital Map, System Info, and Comms views |
| **Encoder K5** | CC 74 (Ch 1\) | **Weapon / Laser Capacitor Bias** | Adjusts forward/rear laser fire cadence or energy priority |
| **Encoder K6** | CC 75 (Ch 1\) | **Shield / Drive Power Balance** | Shifts power distribution during heavy engagements |
| **Encoder K7** | CC 76 (Ch 1\) | **Time Compression (`Stardreamer`)** | Detented step control (`1x` \-\> `10x` \-\> `100x` \-\> `1000x` \-\> `10000x`) with a **safety guard** requiring deliberate multi-tick rotation to prevent accidental jumps |
| **Encoder K8** | CC 77 (Ch 1\) | **Master / Cockpit Audio Trim** | Local volume/trim scaling without altering DAC hardware sample rate |

### MPC Pads (Bank A & Bank B — MIDI Channel 10\)

| Pad | Bank A (Flight & Combat) | Bank B (Navigation, Ship & System) |
| :---- | :---- | :---- |
| **Pad 1** | **Fire Primary Laser** (Hold/Repeat) | **Hyperspace Jump Initiate** (Requires valid target lock) |
| **Pad 2** | **Launch Missile** | **Jettison Cargo / Escape Capsule** (Requires Sustain Shift) |
| **Pad 3** | **Target Nearest Hostile** | **Toggle Landing Gear** |
| **Pad 4** | **Cycle Next Target** | **Activate ECM / Countermeasures** |
| **Pad 5** | **Engage / Disengage Autopilot** | **Open Galactic / Sector Star Map** |
| **Pad 6** | **Docking Computer Engage** | **Open System / Orbital Map** |
| **Pad 7** | **Match Target Velocity** | **Open Ship Status / Cargo Manifest** |
| **Pad 8** | **Reset Time Compression to `1x` (Emergency Drop)** | **Pause / Toggle `[AX]` Telemetry Overlay** |

### Keybed & Sustain Pedal

* **25-Note Velocity Keybed (Ch 1, Notes `C2–C4`):**  
  * **Lower Octave (`C2–B2`):** Quick camera presets (Forward View, Rear View, External Chase Cam, Tactical Overview) and cockpit MFD shortcuts.  
  * **Upper Octave (`C3–C4`):** Programmable macro hotkeys (e.g., Request Docking Clearance, Hail Target, Quick-Save / Snapshot).  
* **Sustain Pedal (`CC 64`, Ch 1):** Acts as a **Flight Modifier / Shift & Airbrake Latch**:  
  * Held alone: Engages retro-thruster braking / attitude stabilization.  
  * Held with Pads/Encoders: Unlocks guarded actions (such as Hyperspace Jump confirmation or Cargo Jettison) and switches encoders to fine-trim mode.

---

## 3\. Configuration (`ax_engine.ini`)

Add the following `[frontier.midi]` section to `ax_engine.ini` in `Generation-X-AX` so bindings, deadzones, and device auto-detection remain declarative and portable:

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

## 4\. Build & Verification Checklist

1. **Verify Hardware Detection on Core 5 (`0x20`):**

```sh
amidi -l | grep -i MPKmini3
```

2. **Confirm `ax_retro_bridge` & `ax::core` Linkage:** Ensure `engines/frontier` links `ax_retro_bridge` (`ax-sdk v1_36_2`) in `Generation-X-AX/CMakeLists.txt` with `AX_BUILD_FRONTIER=ON`.  
3. **Runtime Smoke Test:** Launch `frontier` with and without the MPK Mini MK3 connected, verify zero audio underruns on Core 0 (`0x01`), test hot-unplug/replug recovery on Core 5 (`0x20`), and confirm Pad 8 (`Reset Time Compression to 1x`) immediately drops `Stardreamer` warp back to real time.
