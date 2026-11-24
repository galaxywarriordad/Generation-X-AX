# Generation-X `[AX]` — Unified Retro Engines Suite

**Programme:** The Silicon Preservation Society (`[AX]`)  
**SDK Baseline:** `ax-sdk` `v1_36_2` (`v1.36.2`)  
**License:** `GPL-3.0` (preserving upstream engine licenses in `engines/*/`)

**Generation-X `[AX]`** consolidates classic 1985–1998 arcade, console, and home-computer C/C++ engines behind a single shared C++20 hardware-abstraction adapter (`ax_retro_bridge`) powered by **`ax-sdk` (`ax::core`)**—delivering deterministic `2:3:1` physical-core isolation, HugeTLB/Large-Page fastmem, lock-free USB RawMIDI cockpit control, and zero-underrun USB DAC audio across **Windows 10/11** and bare-metal **`Piccolo-AX`** (`Linux 6.12-rt`).

---

## Bringing Amiga & Atari ST Bare-Metal Magic Back to Life

What made the **Commodore Amiga** and **Atari ST** unforgettable wasn't raw megahertz—it was deterministic hardware timing, zero OS interference, and audio silicon that punched far above its price tag. In 1985, while MS-DOS PCs were still beeping through a single piezo speaker, the Amiga's **Paula** chip was pushing four hardware DMA-driven 8-bit PCM channels (28 kHz+ stereo) and the Atari ST's **YM2149 PSG** was spitting out razor-sharp square-wave and noise envelopes—all locked to a `50 Hz` / `60 Hz` beam-raced vertical blank with zero background scheduler jitter.

Together, we rebuilt that exact bare-metal experience on **Piccolo-AX** (`x86_64` Linux `6.12-rt` EROFS `toram`), mapping the legendary Amiga and Atari ST software pillars directly onto deterministic `[AX]` engines:

| Classic Amiga / Atari ST Pillar | What Made It Legendary | Piccolo-AX Engine Mapping |
| :--- | :--- | :--- |
| **ProTracker / OctaMED / TCB Tracker**<br>*(Amiga `.MOD` & ST `.SNDH` Audio)* | Hardware DMA sample playback with zero CPU audio overhead and unmistakable stereo separation | **`FLAC_AX`** (`libopenmpt` + `libFLAC++`) — Bit-perfect `S32_LE` (44.1–384 kHz) & DoP (`DSD64`/`128`) transport pinned to **Core 0** (`SCHED_FIFO 80`) |
| **Commodore Amiga 500 / 1200 / CD32**<br>*(OCS / ECS / AGA Custom Chipset)* | Copper co-processor raster effects, Blitter sprite pipelines, and Paula 4-channel DMA sound | **`Amiberry-AX`** — Linked against `ax-sdk` for HugeTLB fastmem, `2:3:1` core pinning, and lockstep VBlank pacing |
| **Atari 520ST / 1040STE**<br>*(68000 + YM2149 + STE DMA Sound)* | Crisp high-res productivity, MIDI ports, and iconic demo-scene rasters | **`Hatari-AX`** *(Generation-X-AX)* — Cycle-accurate 68000 & YM2149 execution isolated from OS interrupts |
| **Coin-Op Arcade & 16-Bit Crossover Hits**<br>*(CPS-1/2/3, Neo Geo, Sega System 16)* | The arcade boards Amiga and Atari ST owners dreamed of having at home | **`FinalBurn Neo`** (`libretro-fbneo`) — Integrated into Piccolo-AX's EmulationStation wheel (`es_systems_fbneo.cfg`) on **Cores 2–4** (`0x1C`) |

---

## Why Piccolo-AX Runs and Sounds Better Than Any Standard Emulator Setup

* **Dedicated Audio Clock (`Core 0`, `0x01`, `SCHED_FIFO 80`):** Unlike desktop Windows or standard Batocera/Ubuntu builds where USB audio shares threads with the UI and kernel workers, Piccolo-AX isolates the ALSA `S32_LE` writer and USB DAC interrupts onto **Core 0**, eliminates PulseAudio/PipeWire resampling, and uses `SIGUSR1` DAC handoffs (`axGenerator.py`) so `FLAC_AX` and `Amiberry-AX`/`Hatari-AX` never contend over the DAC.
* **Lockstep `2:3:1` Execution:** **Core 1** (`0x02`) drives Vulkan/iGPU scanout, **Cores 2–4** (`0x1C`) execute cycle-accurate 68000/custom-chip emulation with HugeTLB fastmem (`ax-sdk`), and **Core 5** (`0x20`) handles background I/O and ALSA RawMIDI polling—delivering rock-solid `50.00 Hz` PAL / `60.00 Hz` NTSC scrolling without audio crackles or micro-stutter.

---

## Included `[AX]` Classic Engines

| Engine Directory | Target System & Era | Upstream Base | Key `ax-sdk` & Bridge Integration |
| :--- | :--- | :--- | :--- |
| **`engines/rmg/`** | **Nintendo 64 (1996)** | Rosalie's Mupen GUI (`mupen64plus` + `ParaLLEl-RDP`) | HugeTLB/Large-Page R4300i RDRAM fastmem, `ax::vram` BAR1 staging protection, and `LockFreeStereoRing` audio. |
| **`engines/blastem/`** | **Sega Mega Drive (1988)** | BlastEm (Cycle-Accurate 68000 / Z80 + YM2612) | Cycle-accurate Yamaha YM2612 FM + PSG audio streamed directly into `ax::audio::LockFreeStereoRing` on `Core 0`. |
| **`engines/cannonball/`** | **Sega OutRun Arcade (1986)** | Cannonball (Enhanced Dual-68k + YM2151 C++ Engine) | Locked 60/120 FPS Super Scaler pacing (`ax::topology`) and zero-jitter YM2151 + Sega PCM audio output. |
| **`engines/eduke32/`** | **PC Build Engine FPS (1996)** | EDuke32 (`Duke Nukem 3D` / `Ion Fury` C/C++ Engine) | Polymost/Software rendering pinned to `2:3:1` topology with lock-free stereo PCM audio and SHM frame telemetry. |
| **`engines/frontier/`** | ***Frontier: Elite II* (1993)** | `frontier-fe2` (`ax_frontier_bridge`) | Deterministic frame pacing (`ax::topology`), Core `5` (`0x20`) ALSA RawMIDI listener, and lock-free SPSC Akai MPK Mini MK3 cockpit control drained on Cores `2–4` (`0x1C`). |
| **`engines/flycast/`** | **Sega Dreamcast / NAOMI / Atomiswave (1998)** | Flycast (`flyinghead/flycast`) | SH-4 dynarec (`Cores 2–4`), Vulkan submission (`Core 1`), and AICA stereo PCM via `LockFreeStereoRing` (`Core 0`). |
| **`engines/hatari/`** | **Atari ST / STE / TT / Falcon (1985–1993)** | Hatari (`hatari/hatari`) | Cycle-accurate 68000/68030 + DSP56001 (`Cores 2–4`), YM2149/DMA audio via `LockFreeStereoRing` (`Core 0`), and native ALSA MIDI. |

---

## Akai MPK Mini MK3 Cockpit Bridge (`frontier-fe2`)

`Generation-X-AX` includes a zero-allocation, cache-line-aligned (`alignas(64)`) Single-Producer Single-Consumer (SPSC) USB RawMIDI bridge (`shared/ax_midi_mpk3.hpp` & `shared/ax_midi_mpk3.cpp`) tailored for **Frontier: Elite II** (`frontier-fe2`):

* **Core 5 (`0x20`) ALSA RawMIDI Listener:** Non-blocking `snd_rawmidi_read` loop polls `hw:CARD=MPKmini3,DEV=0` with hot-plug retry, completely isolated from audio (`0x01`), rendering (`0x02`), and physics (`0x1C`).
* **Cores 2–4 (`0x1C`) Bounded Frame Drain:** `ax_frontier_step_frame` drains at most `32` events per frame (`max_events_per_frame = 32`) prior to advancing simulation physics.
* **Flight Controls & Guardrails:** Maps the 4-way thumbstick (`±6%` deadzone + quadratic trim curve) to Roll/Yaw and Pitch, encoders `K1–K8` (`CC 70–77`) to throttle, scanner zoom, power bias, and guarded Stardreamer time compression, MPC Bank A/B pads (`Notes 36–51`) to weapons, autopilot, docking computer, and `CC 64` sustain-guarded jettison/escape, and the 25-note keybed to camera/MFD presets.
* **Full Control Reference:** See [`docs/FRONTIER_MPK_MINI3_MAPPING.md`](docs/FRONTIER_MPK_MINI3_MAPPING.md) and `[frontier.midi]` in [`ax_engine.ini`](ax_engine.ini).

---

## Repository Structure

```text
Generation-X-AX/
├── .github/
│   └── workflows/               # CI release build & subtree workflows (Linux x86_64 Clang/LLD)
├── CMakeLists.txt               # Deterministic CMake entry: resolves ax-sdk (v1_36_2), ThinLTO, PGO & Skylake/BMI2 flags
├── ax_engine.ini                # Shared [AX] 2:3:1 topology, domain masks & [frontier.midi] configuration
├── docs/
│   └── FRONTIER_MPK_MINI3_MAPPING.md  # Akai MPK Mini MK3 hardware mapping & runtime verification guide
├── shared/
│   ├── ax_retro_bridge.hpp      # Shared C++20 adapter header (config, topology, audio ring, SHM telemetry)
│   ├── ax_retro_bridge.cpp      # Core adapter implementation linked against ax::core
│   ├── ax_midi_mpk3.hpp         # Lock-free 256-slot SPSC ring & Frontier command translator header
│   └── ax_midi_mpk3.cpp         # Core 5 (0x20) ALSA RawMIDI listener & deadzone/curve processor
├── engines/
│   ├── rmg/                     # Nintendo 64 engine subtree (GPL-3.0)
│   ├── blastem/                 # Sega Mega Drive engine subtree (GPL-3.0)
│   ├── cannonball/              # Sega OutRun C++ engine subtree
│   ├── eduke32/                 # Build Engine FPS subtree (GPL-2.0 / Build)
│   ├── frontier/                # Frontier: Elite II C/C++ bridge & frontier-fe2 target
│   ├── flycast/                 # Sega Dreamcast / NAOMI / Atomiswave subtree (GPL-2.0)
│   └── hatari/                  # Atari ST / STE / TT / Falcon subtree (GPL-2.0)
└── Scripts/                     # Windows (clang-cl) & Linux build helpers
