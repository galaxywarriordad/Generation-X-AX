# 🕹️ Generation-X `[AX]` — Unified Retro Engines Suite

**Programme:** The Silicon Preservation Society (`[AX]`)  
**SDK Baseline:** `ax-sdk` `v1_36_1` (`v1.36.1`)  
**License:** `GPL-3.0` (preserving upstream engine licenses in `engines/*/`)

**Generation-X `[AX]`** consolidates classic 1985–1998 arcade, console, and home-computer C/C++ engines behind a single shared C++20 hardware-abstraction adapter (`ax_retro_bridge`) powered by **`ax-sdk` (`ax::core`)**—delivering deterministic `2:3:1` physical-core isolation, HugeTLB/Large-Page fastmem, and zero-underrun USB DAC audio across **Windows 10/11** and bare-metal **`Piccolo [AX]`** (`Linux 6.12-rt`).

---

## 🌐 Ecosystem Repository Network

| Repository | Role | Host & Canonical Repo | Lineage |
| :--- | :--- | :--- | :--- |
| **`Generation-X-AX`** | **Unified Retro Engines `[AX]` Suite** (N64, Mega Drive, OutRun, EDuke32, Frontier, Dreamcast, Atari ST) | GitHub: `galaxywarriordad/Generation-X-AX` | Upstream: `RMG`, `BlastEm`, `Cannonball`, `EDuke32`, `glfrontier`, `Flycast`, `Hatari` |
| **`ax-sdk`** | Shared C++20 Silicon Core (`libax_core` / `ax::core`) | GitHub: `galaxywarriordad/ax-sdk` | Dependency-Free C++20 Core (`MIT`) |
| **`Piccolo-AX`** | Bare-Metal Linux Console OS (`< 512 MB` `toram` EROFS) | GitHub: `galaxywarriordad/Piccolo-AX` | OS Base: Batocera Linux (`Linux 6.12-rt`) |
| **`piccolo-ax-desktop`** | Optional `< 35 MB` Native C/C++ Workbench & Diagnostics Layer | GitHub: `galaxywarriordad/piccolo-ax-desktop` | `LXQt`/`XFCE`, `GrafX2`, `LibreSprite`, `mtPaint`, `AzPainter`, `GHex`, `CPU-X` |
| **`Eden-Espresso-Wave-AX`** | Nintendo Switch Engine (Windows 10/11 Master) | Forgejo: `galaxywarriordad/Eden-Espresso-Wave-AX` | Upstream: `Eden` |
| **`Cemu-Latte-AX`** | Nintendo Wii U Engine (`Win10/11` + `Piccolo-AX`) | GitHub: `galaxywarriordad/Cemu-Latte-AX` | Upstream: `Cemu` |
| **`FLAC_AX`** | Bare-Metal Semi-Pro ESS Sabre Audio Appliance | GitHub: `galaxywarriordad/FLAC_AX` | `libFLAC++` + Direct ALSA `SCHED_FIFO` |

---

## 🎮 Included `[AX]` Classic Engines

| Engine Directory | Target System & Era | Upstream Base | Key `ax-sdk` Integration |
| :--- | :--- | :--- | :--- |
| **`engines/rmg/`** | **Nintendo 64 (1996)** | Rosalie's Mupen GUI (`mupen64plus` + `ParaLLEl-RDP`) | HugeTLB/Large-Page R4300i RDRAM fastmem, `ax::vram` BAR1 staging protection, and `LockFreeStereoRing` audio. |
| **`engines/blastem/`** | **Sega Mega Drive (1988)** | BlastEm (Cycle-Accurate 68000 / Z80 + YM2612) | Cycle-accurate Yamaha YM2612 FM + PSG audio streamed directly into `ax::audio::LockFreeStereoRing` on `Core 0`. |
| **`engines/cannonball/`** | **Sega OutRun Arcade (1986)** | Cannonball (Enhanced Dual-68k + YM2151 C++ Engine) | Locked 60/120 FPS Super Scaler pacing (`ax::topology`) and zero-jitter YM2151 + Sega PCM audio output. |
| **`engines/eduke32/`** | **PC Build Engine FPS (1996)** | EDuke32 (`Duke Nukem 3D` / `Ion Fury` C/C++ Engine) | Polymost/Software rendering pinned to `2:3:1` topology with lock-free stereo PCM audio and SHM frame telemetry. |
| **`engines/frontier/`** | ***Frontier: Elite II* (1993)** | `glfrontier` (68k-to-C OpenGL Space Sim Engine) | Deterministic frame pacing (`ax::topology`), `LockFreeStereoRing` audio, and Core `5` USB MIDI cockpit hook readiness. |
| **`engines/flycast/`** | **Sega Dreamcast / NAOMI / Atomiswave (1998)** | Flycast (`flyinghead/flycast`) | SH-4 dynarec (`Cores 2–4`), Vulkan submission (`Core 1`), and AICA stereo PCM via `LockFreeStereoRing` (`Core 0`). |
| **`engines/hatari/`** | **Atari ST / STE / TT / Falcon (1985–1993)** | Hatari (`hatari/hatari`) | Cycle-accurate 68000/68030 + DSP56001 (`Cores 2–4`), YM2149/DMA audio via `LockFreeStereoRing` (`Core 0`), and native ALSA MIDI. |

---

## 📁 Repository Structure

```text
Generation-X-AX/
├── .github/
│   └── workflows/                  # Subtree import & CI build workflows
├── CMakeLists.txt                  # Unified CMake entry: resolves ax-sdk (v1_36_1) once & builds enabled engines
├── ax_engine.ini                   # Shared [AX] CoreRatio=2:3:1, audio ring & telemetry configuration
├── shared/
│   ├── ax_retro_bridge.hpp         # Shared C++20 adapter header (config, topology, audio ring, SHM telemetry)
│   └── ax_retro_bridge.cpp         # Implementation linked against ax::core
├── engines/
│   ├── rmg/                        # Nintendo 64 engine subtree (GPL-3.0)
│   ├── blastem/                    # Sega Mega Drive engine subtree (GPL-3.0)
│   ├── cannonball/                 # Sega OutRun C++ engine subtree
│   ├── eduke32/                    # Build Engine FPS subtree (GPL-2.0 / Build)
│   ├── frontier/                   # Frontier: Elite II (glfrontier) engine subtree
│   ├── flycast/                    # Sega Dreamcast / NAOMI / Atomiswave subtree (GPL-2.0)
│   └── hatari/                     # Atari ST / STE / TT / Falcon subtree (GPL-2.0)
└── Scripts/                        # Windows (clang-cl) & Linux build helpers
```

---

## ⚖️ Legal & ROM Ownership Policy

In strict alignment with **The Silicon Preservation Society** policy and upstream licenses:

* **Zero Copyrighted Assets Bundled:** This repository contains **no** copyrighted game ROMs, arcade PCB dumps, disc images, BIOS/TOS images, or proprietary assets.
* **Original Hardware Required:** You **must legally own original Nintendo 64 cartridges, Sega Mega Drive cartridges, Sega OutRun arcade PCBs, Build Engine game data, *Frontier: Elite II* disks, Sega Dreamcast discs/carts, and/or Atari ST/TOS hardware** and dump your own files directly from your own hardware.

---

## 🔨 Building

### 1. Automatic `ax-sdk` Resolution
`CMakeLists.txt` automatically resolves `ax-sdk` (`v1_36_1`) in the following priority order:
1. `-DFETCHCONTENT_SOURCE_DIR_AX_SDK=<path>` (used by `Piccolo-AX` Buildroot)
2. `$ENV{AX_SDK_DIR}`
3. Sibling checkout (`../ax-sdk`)
4. Automatic CMake `FetchContent` (`GIT_TAG v1_36_1`)

### 2. Standalone CMake Build (Windows `clang-cl` / Linux `Clang` or `GCC`)
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DAX_BUILD_RMG=ON \
  -DAX_BUILD_BLASTEM=ON \
  -DAX_BUILD_CANNONBALL=ON \
  -DAX_BUILD_EDUKE32=ON \
  -DAX_BUILD_FRONTIER=ON \
  -DAX_BUILD_FLYCAST=ON \
  -DAX_BUILD_HATARI=ON
cmake --build build
```

### 3. Bare-Metal `Piccolo [AX]` Integration
`Piccolo-AX` builds each engine target directly from `Generation-X-AX` via `batocera-ax-external/package/{rmg-ax,blastem-ax,cannonball-ax,eduke32-ax,frontier-ax,flycast-ax,hatari-ax}/` and routes launches through `axGenerator.py` inside isolated `eden.slice` cores.
