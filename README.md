# 🕹️ Generation-X `[AX]` — Unified Retro Engines Suite

**Programme:** The Silicon Preservation Society (`[AX]`)  
**SDK Baseline:** `ax-sdk` `v1_36_1` (`v1.36.1`)  
**License:** `GPL-3.0` (preserving upstream engine licenses in `engines/*/`)

**Generation-X `[AX]`** consolidates classic 1986–1996 arcade and console C/C++ engines behind a single shared C++20 hardware-abstraction adapter (`ax_retro_bridge`) powered by **`ax-sdk` (`ax::core`)**—delivering deterministic `2:3:1` physical-core isolation, HugeTLB/Large-Page fastmem, and zero-underrun USB DAC audio across **Windows 10/11** and bare-metal **`Piccolo [AX]`** (`Linux 6.12-rt`).

---

## 🌐 Ecosystem Repository Network

| Repository | Role | Host & Canonical Repo | Lineage |
| :--- | :--- | :--- | :--- |
| **`Generation-X-AX`** | **Unified Retro Engines `[AX]` Suite** (N64, Mega Drive, 1986 OutRun) | GitHub: `galaxywarriordad/Generation-X-AX` | Upstream: `RMG`, `BlastEm`, `Cannonball` |
| **`ax-sdk`** | Shared C++20 Silicon Core (`libax_core` / `ax::core`) | GitHub: `galaxywarriordad/ax-sdk` | Dependency-Free C++20 Core (`MIT`) |
| **`Piccolo-AX`** | Bare-Metal Linux Console OS (`< 512 MB` `toram` EROFS) | GitHub: `galaxywarriordad/Piccolo-AX` | OS Base: Batocera Linux (`Linux 6.12-rt`) |
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

---

## 📁 Repository Structure

```text
Generation-X-AX/
├── CMakeLists.txt                  # Unified CMake entry: resolves ax-sdk (v1_36_1) once & builds enabled engines
├── ax_engine.ini                   # Shared [AX] CoreRatio=2:3:1, audio ring & telemetry configuration
├── shared/
│   ├── ax_retro_bridge.hpp         # Shared C++20 adapter header (config, topology, audio ring, SHM telemetry)
│   └── ax_retro_bridge.cpp         # Implementation linked against ax::core
├── engines/
│   ├── rmg/                        # Nintendo 64 engine subtree (GPL-3.0)
│   ├── blastem/                    # Sega Mega Drive engine subtree (GPL-3.0)
│   └── cannonball/                 # Sega OutRun C++ engine subtree
└── Scripts/                        # Windows (clang-cl) & Linux build helpers
```

---

## ⚖️ Legal & ROM Ownership Policy

In strict alignment with **The Silicon Preservation Society** policy and upstream licenses:

* **Zero Copyrighted Assets Bundled:** This repository contains **no** copyrighted game ROMs, arcade PCB dumps, BIOS images, or proprietary assets.
* **Original Hardware Required:** You **must legally own original Nintendo 64 cartridges, Sega Mega Drive cartridges, and/or Sega OutRun arcade PCBs** and dump your own files directly from your own hardware.

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
  -DBUILD_RMG=ON \
  -DBUILD_BLASTEM=ON \
  -DBUILD_CANNONBALL=ON
cmake --build build
```

### 3. Bare-Metal `Piccolo [AX]` Integration
`Piccolo-AX` builds each engine target directly from `Generation-X-AX` via `batocera-ax-external/package/{rmg-ax,blastem-ax,cannonball-ax}/` and routes launches through `axGenerator.py` inside isolated `eden.slice` cores.
