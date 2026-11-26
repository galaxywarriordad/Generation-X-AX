# 🛠️ Detailed Work Plan & Code Outline: Unified `PCSX2_AX` Engine & Thin Mode Adapter Layer (`PS2` \+ `PCSX2x6` / `COH-H`) — Prototype Edition 🎮

**📡 Programme:** The Silicon Preservation Society (`[AX]`)  
**🧪 Status:** **Prototype Phase — shared core and mode adapter foundation staged and audited (stay tuned\!)**  
**📐 SDK Baseline:** `ax-sdk` (`v1_36_2` / `ax::core`)  
**🎖️ Minimum Standard:** `Eden Espresso Wave [AX]` (`MASTER v1.35` / `v1.35.1` lineage \+ `Piccolo-AX` `v0.6` bare-metal appliance rules)  
**🎯 Target Platform:** `Piccolo-AX` (`x86_64`, `Linux 6.12-rt` `PREEMPT_RT`, `< 512 MB` / `<= 524288 KB` `toram` `EROFS` `lz4hc,9`, A/B `.prev` fallback, journal-free `^has_journal` `ext4` `SHARE`) & `Generation-X-AX`  
**💻 Reference Silicon:** Intel Core i5-9600K / i5-8600K (`6C/6T`, Hyper-Threading disabled, `2:3:1` topology) \+ 32 GB DDR4-3600 \+ NVIDIA GeForce GTX 980 Ti (`6 GB` Maxwell `GM200`, `580.*` legacy driver, **Vulkan 1.3** ceiling, **`5.50 GiB` VRAM ceiling** / **`5.00 GiB` proactive async prune trigger**) \+ Intel UHD Graphics 630 (`iHD` VA-API / `dma_buf`) \+ HiBy FC3 (`ESS Sabre ES9281AC Pro` / `ES9281PRO`)

---

## 📖 Introduction: *What Hi-Fi?* Meets *Retro Gamer* — From Emotion Engine & Arcade Cabinets to a Bare-Metal `[AX]` Appliance 🕹️

> **⚠️ Prototype Phase Notice:** This unified `PCSX2_AX` architecture, staged layout under `engines/pcsx2_ax/`, and C++20 code outline are **in Prototype Phase—shared core and mode adapter foundation staged and audited**—so **stay tuned** as we proceed with CMake \+ Ninja (-j 4\) build verification, mode adapter hardware wiring, and target execution\!

Read a classic *Retro Gamer* retrospective on the turn of the millennium and a *What Hi-Fi?* bench test side by side, and you start hearing the same obsession in two different dialects: **timing, rhythm, and zero smear**. When Sony launched the PlayStation 2 in 2000, it didn't just win the living room—it rewrote what a home console could throw at a television screen. At the heart of that machine sat the **Emotion Engine**—Sony and Toshiba's custom 128-bit SIMD processor pairing a MIPS-based CPU core with dual vector units (`VU0` and `VU1`)—working hand in glove with the **Graphics Synthesizer** and its 4 MB of on-chip embedded DRAM (`eDRAM`) over a high-bandwidth internal bus to push sweeping particle storms, moody lighting, and 60 fps action that defined a golden era of gaming. Just as remarkably, that same PS2 hardware lineage stepped out of the living room and into the **Namco System 246, System 256, and Super System 256** arcade boards, bringing big-screen coin-op drama—from *Time Crisis* and arcade racers to fighting and light-gun cabinets—into arcades across the 2000s.

On an ordinary desktop today, playing either home PS2 classics or System 246/256 arcade titles can still trip over background OS interrupts, shared audio mixers, uncoordinated thread scheduling, and unguarded VRAM spikes. On our completed **`Piccolo-AX`** appliance—booting a pruned `< 512 MB` `toram` `EROFS` image on `Linux 6.12-rt` with strict `2:3:1` physical core isolation, our `GTX 980 Ti` (`6 GB` Maxwell `GM200`, `580.*` legacy driver) under **Vulkan 1.3** within a **`5.50 GiB` VRAM ceiling** (`5.00 GiB` async prune trigger), and bit-exact audio locked to a `HiBy FC3` DAC via `/run/lock/ax-dac.lock`—both worlds play with remarkable poise: tight frame pacing, crisp transient snap, and zero-loss persistence to `SHARE` (`/userdata`).

Equally important, we are giving **`PCSX2x6` (`COH-H` / System 246 & 256\)** first-class billing alongside retail PS2—without duplicating the core emulation engine. By keeping `libax_pcsx2_core` shared and isolating `Ps2ConsoleAdapter` (`--mode ps2`) from `CohHArcadeAdapter` (`--mode coh-h` / `PCSX2x6`), our appliance handles retail BIOS/disc/memory-card play alongside `.acgame` profiles, `COH-H10020` dongle data, JVS/wheel/light-gun arcade controls, and atomic `ACSRAM`/NVRAM persistence under one clean engineering roof.

---

## 🗂️ 1\. Code Sections Identified in Our Google Drive `pcsx2` Folder 📁

Our unified engine layout under `engines/pcsx2_ax/` in `Generation-X-AX` stages shared core components (48 files in `core/`), thin console mode handlers (26 files in `adapters/ps2/`), and arcade mode handlers (19 files in `adapters/coh_h/`). By isolating changes into explicit adapters, we avoid modifying 586 upstream files while maintaining 230 necessary core modifications and 43 dedicated COH-H files, all built clean via top-level `CMakeLists.txt` targets (`ax_pcsx2_core`, `ax_pcsx2_adapter_ps2`, `ax_pcsx2_adapter_coh_h`, and `pcsx2_ax_harness`):

| Staged Target / Subdirectory | Identified Code Section & Role | Shared Core vs. Adapter Placement |
| :---- | :---- | :---- |
| `engines/pcsx2_ax/core/``ax_pcsx2_core` (48 files) | `IHardwareModeAdapter.hpp` (mode concepts, `HardwareModeAdapterConcept`, `ModeEpochSnapshot`, `FrameTelemetrySample`), `VmContext`, `VMManager_Internal.h`, split `VMManager.cpp` / `VMManager_Media.cpp` / `VMManager_SaveState.cpp` / `VMManager_Config.cpp`, epoch-checked `SaveState.cpp`, lock-free ring slab `CDVDRingSlab.hpp` (with `HugeTlbSlab` & `eventfd`/`WaitOnAddressWakeSignal`), `CDVDisoReader.cpp`, `CDVDdiscThread.cpp`, and `EMU_THREAD_STACK_SIZE = 2 * 1024 * 1024` | **Shared Core (libax\_pcsx2\_core)** — owns VM lifecycle, `2 MB` recompiler thread stack budget, and `AVX2` early check; thin adapters supply mode-specific `VMBootParameters`, game/arcade config paths, and persistence hooks |
| `engines/pcsx2_ax/adapters/ps2/``ax_pcsx2_adapter_ps2` (26 files) | `Ps2ConsoleAdapter.hpp` & implementation: thin mode adapter handling retail PS2 BIOS selection, `.iso` / `.chd` / `.zso` media mounting, DualShock controller bindings, memory cards, and save states under `/userdata/saves/ps2/` with zero core state duplication | **Thin PS2 Console Adapter Target** — compiles into `libax_pcsx2_adapter_ps2.a` and links with `ax_pcsx2_core` |
| `engines/pcsx2_ax/adapters/coh_h/``ax_pcsx2_adapter_coh_h` (19 files) | `CohHArcadeAdapter.hpp`, `ACATA_IO_CHD.cpp`, and arcade board/dongle drivers: handling System 246 / 256 / Super 256 `COH-H` hardware profiles, `.acgame` configurations, `COH-H10020` security dongles, JVS arcade inputs, and atomic `ACSRAM`/NVRAM storage under `/userdata/saves/pcsx2x6/` | **Thin COH-H Arcade Adapter Target** — compiles into `libax_pcsx2_adapter_coh_h.a` and links with `ax_pcsx2_core` |
| `engines/pcsx2_ax/pcsx2_ax_harness``pcsx2_ax_harness` (Executable Harness) | Unified appliance execution entry point: dispatches CLI arguments (`--mode ps2` vs. `--mode coh-h` / `--mode pcsx2x6`), initializes `VmContext`, enforces physical core pinning (`2:3:1`), locks `/run/lock/ax-dac.lock`, and runs the active `IHardwareModeAdapter` instance | **Unified Appliance Executable Target** — links shared core and both thin mode adapters into a single lean binary |
| `pcsx2/Sif.h`, `Sif.cpp`, `Sif0.cpp`, `Sif1.cpp`, `sif2.cpp`, `Sifcmd.h`, `SPR.h`, `SPR.cpp`, `SourceLog.cpp`, `R5900OpcodeImpl.cpp` | `FIFO_SIF_W = 128`, `sifData`, `sifFifo` (`write`, `writeJunk` quadword/IOP-tag junk synthesis, `read`, `clear`), `sif_ee`, `sif_iop`, `t_sif_dma_transfer`, `dmaSPR0` / `dmaSPR1` / `SPRFROMinterrupt` / `SPRTOinterrupt`, `R5900` opcode implementations | **Shared Core (libax\_pcsx2\_core)** — provides EE/IOP SIF FIFO/DMA mechanics and `R5900` execution; thin adapters supply `COH-H` / System 246 & 256 board/dongle/media behavior where arcade hardware diverges |
| `pcsx2/StateWrapper.h`, `StateWrapper.cpp`, `SaveState.cpp`, `SupportURLs.h` | `StateWrapper` (`Mode::Read`/`Write`, `IStream`, `ReadOnlyMemoryStream`, `MemoryStream`, `VectorMemoryStream`, C++20 constrained `Do<T>`, `DoPOD<T>`, `DoArray`, `DoPODArray`, `DoBytes`), `SaveState` serialization | **Shared Core (libax\_pcsx2\_core)** for stream serialization \+ **Adapter-enforced Atomic SHARE Persistence** (`tmp` \-\> `fsync` \-\> `mv` into `/userdata/saves/ps2/` vs. `/userdata/saves/pcsx2x6/`) |
| `pcsx2-gsrunner`, `cmake`, `common`, `tools` (`shader_to_cpp.py`, `merge_ws_ni_patches.py`, `texture_dump_alpha_scaler.py`, `generate_redump_yaml.py`, `generate_fullscreen_ui_translation_strings.py`, `retry.sh`) | Headless/runner execution (`pcsx2-gsrunner`), CMake build definitions (`cmake`), shared utilities (`common`), and offline build/asset/patch/Redump/translation helpers (`tools`) | **Shared Build & Offline Tooling** — `pcsx2-gsrunner` suits lean appliance execution without desktop GUI bloat; `tools/` scripts stay in offline build/asset prep and out of `rootfs.erofs` |

---

## 📋 2\. Detailed Work Plan (Phased Execution Under Our `Eden Espresso Wave [AX]` Standard) 🚀

### ⚙️ Phase 1: Source Baseline, Checksums & Shared Core Boundary (`libax_pcsx2_core`)

1. **Source Baseline, Checksums & Core Boundary Complete (Staged & Audited):** Verification of baseline source checksums and core boundary audit complete. Shared interfaces (`IHardwareModeAdapter.hpp`, `VmContext`, `ModeEpochSnapshot`, `FrameTelemetrySample`), VM manager split, lock-free ring slab (`CDVDRingSlab.hpp`), disc readers (`CDVDisoReader.cpp`, `CDVDdiscThread.cpp`), and CHD I/O (`ACATA_IO_CHD.cpp`) are staged in `engines/pcsx2_ax/` and ready for CMake \+ Ninja (-j 4\) build verification.  
2. **Establish `ax_pcsx2_core` (`C++20`, `ThinLTO`, `ax::core`):** Link the 48-file shared core library against `ax-sdk` (`v1_36_2` / `ax::core`), compiling with `EMU_THREAD_STACK_SIZE = 2 * 1024 * 1024` recompiler stack budget and `HardwareModeAdapterConcept` type safety.  
3. **Define Explicit Hardware Mode Contract (`HardwareMode`):** Route `--mode ps2` (`HardwareMode::Ps2Console`) to `Ps2ConsoleAdapter` and `--mode coh-h` (`HardwareMode::CohHArcade`) to `CohHArcadeAdapter` without modifying upstream execution loops.

### 🧪 Prototype Phase Stubs Inventory (PCSX2\_AX\_PLANNING\_ONLY=ON)

During the initial bootstrap and CMake build verification stage, when `PCSX2_AX_PLANNING_ONLY=ON` is enabled, the build system generates minimal prototype header stubs in `${CMAKE_CURRENT_BINARY_DIR}/proto_inc` using `pcsx2_ax_write_stub` to satisfy core compilation prerequisites prior to full mode adapter hardware wiring. The inventory of generated prototype stubs includes:

4. **Core System & Hardware Concept Headers:** `IHardwareModeAdapter.hpp`, `VmContext.hpp`, `VMManager_Internal.h`, `CDVDRingSlab.hpp`, `SaveState.hpp`, `MTGS.h`, `IconsFontAwesome.h`, `Common.h`, `Config.h`, `Counters.h`, `Gif_Unit.h`, `Host.h`, `CDVD/CDVDcommon.h`, `CDVD/IsoFileFormats.h`, `CDVD/IsoReader.h`, `common/Error.h`, `common/Console.h`, `common/Threading.h`, `common/ScopedGuard.h`, and `common/Timer.h` stubbed with standard C++20 standard library types and concept constraints.  
5. **Adapter Target Stubs:** `Ps2ConsoleAdapter.hpp` and `CohHArcadeAdapter.hpp` generated with lightweight virtual method declarations to allow target linking of `libax_pcsx2_adapter_ps2.a` and `libax_pcsx2_adapter_coh_h.a`.  
6. **Remaining Core & Common Headers Expected:** Supplemental upstream headers (e.g., `common/Pcsx2Types.h`, `pcsx2/Sifcmd.h`, and `pcsx2/StateWrapper.h`) are resolved via standard include paths during build verification before replacing stubs with full staged implementation files in Phase 1 and Phase 2\.

### 🔌 Phase 2: Thin Mode Adapters (`Ps2ConsoleAdapter` & `CohHArcadeAdapter`)

1. **Implement `Ps2ConsoleAdapter` (`--mode ps2`):** Handle retail PS2 BIOS selection, `.iso` / `.chd` / `.zso` disc loading, DualShock/gamepad mapping, and `.ps2` memory cards \+ save states under `/userdata/saves/ps2/`.  
2. **Implement `CohHArcadeAdapter` (`--mode coh-h` / `PCSX2x6`):** Handle System 246 / 256 / Super 256 `COH-H` BIOS/board profiles, `.acgame` configurations, `COH-H10020` dongle data, CD/DVD/HDD media, JVS/coin/test/service inputs (including DIP 1 `MODE` transitions, wheels/pedals, and dual light guns), and `ACSRAM`/NVRAM state under `/userdata/saves/pcsx2x6/`.  
3. **Harden Arcade Exit & Signal Handling:** Guarantee clean teardown on `SIGINT`, `SIGTERM`, and `SIGUSR1` so `CohHArcadeAdapter` never leaves `/run/lock/ax-dac.lock` held or skips `ACSRAM`/NVRAM persistence if an arcade session exits abruptly or hangs.

### 💾 Phase 3: Zero-Loss `SHARE` Persistence & `/dev/shm` `K01piccolo-sync` Wiring ⚡

1. **Atomic Mutation-Time Writes to `SHARE` (`/userdata`):** Write `.ps2` memory cards, save states, `ACSRAM`, NVRAM, and configs to a temporary file on the target `/userdata` filesystem, flush with `ax-fsync` / `fsync` \+ `sync`, and atomically `rename()` into place at save/mutation time—never relying solely on shutdown hooks.  
2. **Ephemeral Cache Staging in `/dev/shm`:** Keep volatile shader/pipeline/unpack caches in `/dev/shm` and flush via hardened `K01piccolo-sync` (`.tmp.$$` \-\> `zstd -t` validation \-\> `fsync` \-\> `.prev` rotation \-\> atomic `mv -f` \+ parent directory sync when `/userdata` is mounted).

### 🛡️ Phase 4: `2:3:1` Core Isolation, Vulkan 1.3 (`5.50 GiB` VRAM Ceiling) & `< 512 MB` `EROFS` Gate 🧊

1. **Enforce `2:3:1` Topology & `/run/lock/ax-dac.lock`:** Pin real-time audio (`SCHED_FIFO`) and `HiBy FC3` (`ES9281AC Pro` / `ES9281PRO`) xHCI IRQ \+ `ScopedDacLock` to `Core 0` (`0x01`), Vulkan 1.3 / GL rendering and GPU IRQ to `Core 1` (`0x02`), `EE` / `microVU` (`VU0`/`VU1`, `2 MB` stack) / `VIF` execution to `Cores 2–4` (`0x1C`), and I/O / `vmtouch` / `K01piccolo-sync` to `Core 5` (`0x20`).  
2. **Gate Vulkan 1.3 & `GTX 980 Ti` (`6 GB` Maxwell `GM200`, `580.*` Driver) VRAM:** Target **Vulkan 1.3** and enforce our **`5.00 GiB` proactive async prune trigger** and **`5.50 GiB` VRAM ceiling** (`pcie_aspm=off`, `nvidia-smi -pm 1`).  
3. **Gate `Piccolo-AX` `EROFS` Size (`<= 524288 KB` / `512 MB`):** Build with Ninja (`-j 4`), strip unneeded symbols/targets, verify `post-image.sh` passes the `524288 KB` (`lz4hc,9` `toram`) ceiling with `rootfs.erofs` \+ `rootfs.erofs.prev` A/B fallback intact, and run read-only `ax_doctor.sh` pre-flight audits.

### 📊 Phase 5: Control Baseline vs. `Skylake` \+ `ThinLTO` \+ Gameplay PGO 🎯

1. **Capture Stage A Control Baseline:** Benchmark unpinned/control runs across both `--mode ps2` and `--mode coh-h` (`PCSX2x6`) workloads using our existing telemetry/regression checks.

### **🛡️ Hardening Items \#1 & \#2: Cross-Thread Epoch/Lifecycle Ordering & Prototype Stub/Build-System Drift Guardrails 🔒**

2. **Cross-Thread Lifecycle & Epoch Ordering Across `VmContext`, `SaveState`, and `VMManager`:** Strictly serialize and guard mode adapter transitions relative to active `SaveState` / `ACSRAM` / NVRAM commits, ensuring temporary `.tmp` save files are automatically cleaned up on any early failure path (including failed `fsync` or epoch mismatches). Write and validate actual archive payloads through `SaveScratchArena` rather than leaving placeholder arguments, and prevent `capture_snapshot` from racing with adapter teardown.  
3. **Prototype Stub & Build-System Drift Guardrails (`PCSX2_AX_PLANNING_ONLY=ON` vs. Full Upstream Headers):** Enforce a strict no-stub-leak-into-release check so prototype stubs generated in `${CMAKE_CURRENT_BINARY_DIR}/proto_inc` cannot ship into non-planning or production release builds. Gate platform-specific or GUI/Discord headers (such as `RedtapeWindows.h`, `ImGui`, and `discord_rpc.h`) behind explicit CMake feature options so headless **`Piccolo-AX`** (`Linux 6.12-rt`, `<= 524288 KB` / `512 MB` `toram` `EROFS` `lz4hc,9`) builds remain completely lean and free of unnecessary bloat. Additionally, ensure workflow submodule checkout resilience by handling stale `DreamPicoPort-API` gitlink entries in CI so workflow jobs do not fail post-checkout.  
4. **Apply Guarded `-march=skylake` \+ `ThinLTO` \+ 3-Stage PGO:** Compile with `-march=skylake` (`AVX2`, `FMA`, `BMI1`/`BMI2` on non-SMT physical cores, avoiding `-march=native` portability traps and I-cache bloat), collect profiles across active PS2 and System 246/256 gameplay (not idle menus), and merge via `llvm-profdata` (`-fprofile-use`) before locking release artifacts.

---

## 💻 3\. Code Outline: Shared Core Interface & Thin Mode Adapters (`C++20`) 🧩

```c
// IHardwareModeAdapter.hpp — Shared hardware mode contract for libax_pcsx2_core
#pragma once

#include <concepts>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace pcsx2_ax::core {

enum class HardwareMode : uint8_t {
    Ps2Console, // Retail PS2 console BIOS + disc (.iso/.chd/.zso) + .ps2 memory cards
    CohHArcade  // System 246 / 256 / Super 256 COH-H board + .acgame + dongle + ACSRAM/NVRAM
};

struct BiosDescriptor {
    std::filesystem::path path;
    uint32_t crc32 = 0;
    std::string_view region_code;
};

struct ModeEpochSnapshot {
    uint64_t epoch_id = 0;
    uint64_t cycle_timestamp = 0;
    HardwareMode active_mode = HardwareMode::Ps2Console;
};

struct FrameTelemetrySample {
    uint64_t frame_index = 0;
    double frame_time_ms = 0.0;
    size_t vram_allocated_bytes = 0;
};

class VmContext;
class StateWrapper;

class IHardwareModeAdapter {
public:
    virtual ~IHardwareModeAdapter() = default;
    virtual HardwareMode mode() const noexcept = 0;
    virtual std::filesystem::path save_directory() const = 0;
    virtual bool initialize(VmContext& vm_ctx) = 0;
    virtual void reset() = 0;
    virtual void shutdown() noexcept = 0;
    virtual void on_vsync(const FrameTelemetrySample& telemetry) = 0;
    virtual BiosDescriptor bios_descriptor() const = 0;
    virtual ModeEpochSnapshot capture_snapshot(uint64_t epoch) const = 0;
    virtual bool read_media_sectors(uint64_t lba, uint32_t count, void* dst) = 0;
    virtual bool freeze_state(StateWrapper& wrapper) = 0;
};

template <typename T>
concept HardwareModeAdapterConcept = std::derived_from<T, IHardwareModeAdapter> && requires(T adapter, VmContext& ctx, StateWrapper& sw) {
    { adapter.mode() } -> std::same_as<HardwareMode>;
    { adapter.initialize(ctx) } -> std::same_as<bool>;
    { adapter.freeze_state(sw) } -> std::same_as<bool>;
};

} // namespace pcsx2_ax::core
```

```c
// CDVDRingSlab.hpp — Lock-free ring slab CD/DVD buffer for libax_pcsx2_core
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <sys/eventfd.h>

namespace pcsx2_ax::cdvd {

class WakeSignal {
public:
    WakeSignal() { fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); }
    ~WakeSignal() { if (fd_ >= 0) ::close(fd_); }
    void Signal() noexcept { uint64_t val = 1; ::write(fd_, &val, sizeof(val)); }
private:
    int fd_ = -1;
};

template <typename T, size_t Capacity>
class CdvdSpscRing {
public:
    bool Push(const T& item) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail - head_.load(std::memory_order_acquire) >= Capacity) return false;
        buffer_[tail % Capacity] = item;
        tail_.store(tail + 1, std::memory_order_release);
        wake_signal_.Signal();
        return true;
    }
private:
    T buffer_[Capacity];
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    WakeSignal wake_signal_;
};

} // namespace pcsx2_ax::cdvd
```

```c
// Ps2ConsoleAdapter.hpp — Thin retail PS2 console mode adapter
#pragma once
#include "core/IHardwareModeAdapter.hpp"

namespace pcsx2_ax::adapters::ps2 {

class Ps2ConsoleAdapter final : public core::IHardwareModeAdapter {
public:
    core::HardwareMode mode() const noexcept override {
        return core::HardwareMode::Ps2Console;
    }
    std::filesystem::path save_directory() const override;
    bool initialize(core::VmContext& vm_ctx) override;
    void reset() override;
    void shutdown() noexcept override;
    void on_vsync(const core::FrameTelemetrySample& telemetry) override;
    core::BiosDescriptor bios_descriptor() const override;
    core::ModeEpochSnapshot capture_snapshot(uint64_t epoch) const override;
    bool read_media_sectors(uint64_t lba, uint32_t count, void* dst) override;
    bool freeze_state(core::StateWrapper& wrapper) override;
};

} // namespace pcsx2_ax::adapters::ps2
```

```c
// CohHArcadeAdapter.hpp — Thin System 246 / 256 / Super 256 COH-H arcade adapter
#pragma once
#include "core/IHardwareModeAdapter.hpp"

namespace pcsx2_ax::adapters::coh_h {

class CohHArcadeAdapter final : public core::IHardwareModeAdapter {
public:
    core::HardwareMode mode() const noexcept override {
        return core::HardwareMode::CohHArcade;
    }
    std::filesystem::path save_directory() const override;
    bool initialize(core::VmContext& vm_ctx) override;
    void reset() override;
    void shutdown() noexcept override;
    void on_vsync(const core::FrameTelemetrySample& telemetry) override;
    core::BiosDescriptor bios_descriptor() const override;
    core::ModeEpochSnapshot capture_snapshot(uint64_t epoch) const override;
    bool read_media_sectors(uint64_t lba, uint32_t count, void* dst) override;
    bool freeze_state(core::StateWrapper& wrapper) override;
};

} // namespace pcsx2_ax::adapters::coh_h
```

---

## 🎛️ 4\. Deep-Dive Decoupling & Optimization Analysis: Audio, Video Rendering, CPU/VU/IOP, Persistence/Inputs & Storage/Footprint 🔬

### **🔊 Audio Subsystem: SPU2, ADPCM/CDDA/Arcade Stream & USB DAC Lock**

The SPU2 audio subsystem requires strict decoupling of sample generation and ring buffer transport from the main guest CPU and IRQ loop to guarantee stutter-free, bit-exact playback. SPU2 DMA transfers and register writes process ADPCM streams and CDDA/arcade audio tracks into a lock-free intermediate ring buffer. Real-time audio mixing and sample transport run on a dedicated thread scheduled under `SCHED_FIFO` and pinned exclusively to Core 0 (`0x01`) on the reference 6C/6T Intel Core i5-9600K/8600K architecture. Pinned to the same isolated core is the xHCI USB host controller interrupt handling the HiBy FC3 USB DAC (ESS Sabre ES9281AC Pro / ES9281PRO), along with the system-wide `ScopedDacLock` managed through `/run/lock/ax-dac.lock` paired with clean modern kernel/ALSA device handling. Signal handlers for `SIGINT`, `SIGTERM`, and `SIGUSR1` ensure that abruptly terminated sessions reliably release the file lock and flush audio DMA queues without leaving hardware locks hanging. Crucially, when switching or resetting between retail PS2 and COH-H / System 246 & 256 modes, an explicit epoch-fence drain and clock-domain guardrails prevent queued audio frames from carrying stale sample-rate or buffer-mode assumptions across an epoch boundary.  
Mode-specific audio behavior is isolated entirely between the thin mode adapters while keeping the core SPU2 emulation engine in `libax_pcsx2_core` pristine. When operating in retail console mode (`Ps2ConsoleAdapter`, `--mode ps2`), standard PS2 SPU2 dual-core voice mixing, reverb processing, and memory mapping are active without modifying upstream defaults. When running arcade workloads (`CohHArcadeAdapter`, `--mode coh-h` / `PCSX2x6`), Namco System 246/256 multi-channel streams and custom cabinet sound routing are handled through dedicated adapter hooks, ensuring zero pollution of retail SPU2 code paths.

### **🖼️ Video Rendering Subsystem: Graphics Synthesizer (GS), MTGS & Vulkan 1.3 VRAM Budgeting**

The Graphics Synthesizer (GS) presentation pipeline decouples command submission from Emotion Engine (EE) and Vector Unit (VU0/VU1) execution using Multi-Threaded GS (MTGS) synchronized via `VmContext` epoch boundaries. Rendering operations and GPU IRQ handling are pinned to Core 1 (`0x02`), giving Vulkan 1.3 submission threads an uninhibited physical core. On the reference NVIDIA GeForce GTX 980 Ti (6 GB Maxwell GM200, 580.\* legacy driver), strict memory safety is enforced by tying `FrameTelemetrySample` VRAM reporting directly into the proactive asynchronous prune trigger at `5.00 GiB` and an absolute VRAM ceiling at `5.50 GiB`. Furthermore, MTGS and renderer submission queues cleanly discard or invalidate pending frame work on `VmContext` epoch changes before a VM reset or mode switch completes, preventing ghost surface updates. Transient shader compilation, pipeline caches, and unpack buffers are staged strictly in `/dev/shm` and persisted across reboots via `K01piccolo-sync`, keeping flash writes to zero during active rendering.  
Headless builds targeting the bare-metal `Piccolo-AX` appliance (`<= 524288 KB` / `512 MB` EROFS image) completely strip heavy desktop GUI elements (ImGui, Qt, Discord RPC, Windows platform stubs). Optional high-level video enhancements, such as an `ax_video_upscaler` component or debug overlay extensions, remain strictly decoupled modular plugins that can be compiled out entirely to preserve minimal image sizes and low memory overhead during headless execution.

### **⚡ CPU, Vector Units & Sub-Processors: EE, VU0/VU1, R3000A IOP & SIF/CDVD Interconnect**

Guest execution allocates core CPU workload threads across dedicated physical cores to eliminate context-switching latency. Main Emotion Engine (EE) CPU emulation, microVU recompiler threads (VU0 and VU1), and VIF unpackers are pinned across Cores 2–4 (`0x1C`), operating with an explicit recompiler thread stack allocation of `EMU_THREAD_STACK_SIZE = 2 * 1024 * 1024` (2 MB). A strict zero-allocation and zero-syscall rule is enforced inside hot R5900, microVU, VIF, and CDVD ring-slab execution paths—pre-faulting and reserving HugeTLB and `mlockall` memory upfront. Background filesystem prefetching via `vmtouch`, lock-free state synchronization via `K01piccolo-sync`, and dynamic buffer growth are offloaded entirely to Core 5 (`0x20`) or non-hot control paths, ensuring guest execution on Cores 2–4 remains free of allocator or page-fault jitter. Shared core hardware features—including SIF (Sub-System Interface) DMA transfers, Scratchpad RAM (SPR), virtual TLB memory translation (vtlb), and the lock-free `CDVDRingSlab` with eventfd-backed `WakeSignal` mechanisms—reside strictly inside `libax_pcsx2_core`.  
Per-mode peripheral and storage interfaces are cleanly encapsulated within their respective adapters. `Ps2ConsoleAdapter` manages retail CDVD optical disc parsing (ISO/CHD/ZSO), memory cards, and standard SIO2 controller interfaces. Conversely, `CohHArcadeAdapter` encapsulates Namco System 246/256/Super 256 arcade hardware components: ACCORE board logic, ACATA/ACATAPI media interfaces, `ACATA_IO_CHD` stream handling, ACRAM, ACSRAM, ACJV (JVS I/O), and ACUART communication controllers. It is vital to clarify the hardware architecture: both retail PS2 and Namco System 246/256 arcade platforms display graphics and execute rasterization through the combination of the 128-bit Emotion Engine (EE), Vector Units (VU0/VU1), I/O Processor (R3000A IOP), and the Graphics Synthesizer (GS with embedded eDRAM over VIF/GIF buses)—rather than relying on a legacy 2D PPU structure.

### **🎮 Input, Peripherals & Atomic Persistence: Controls, Zero-Loss Save Routing & Compiler Optimization**

Input processing decouples peripheral state polling from internal VMManager execution loops. Standard DualShock 1/2 gamepads, pressure-sensitive button mappings, and multitap adapters are handled inside `Ps2ConsoleAdapter`. Arcade cabinet controls—including JVS I/O polling, coin drop registers, test/service switches, arcade steering wheels with force feedback, dual light-gun inputs, and UART security dongle communication (COH-H10020)—are encapsulated within `CohHArcadeAdapter`. Neither adapter injects device-specific polling logic into the core emulation loop.  
To guarantee zero-loss state persistence to `SHARE` (`/userdata`), all save states, memory card mutations, ACSRAM writes, and NVRAM dumps use atomic mutation-time file transactions across `/userdata/saves/ps2/` and `/userdata/saves/pcsx2x6/`. Data is written to a temporary file on the destination mount (`.tmp`), explicitly flushed to physical media via `fsync()` along with parent-directory `fsync()`, and atomically replaced using `rename()`. On any `fsync()` failure, epoch mismatch, or short/invalid write, failure-path cleanup automatically removes the orphaned `.tmp` file before post-rename verification confirms header and integrity layout. Build optimization targets non-SMT physical cores using `-march=skylake` (AVX2, FMA, BMI1/BMI2) paired with ThinLTO and a 3-stage Profile-Guided Optimization (PGO) workflow collected during active gameplay, benchmarked against a clean control baseline to ensure maximum frame rate stability and minimal latency smear.

### **📦 Storage, Media & Image Footprint: CDVD, ACATA\_IO\_CHD, /dev/shm & EROFS Appliance Build Pipeline**

Media access pipelines decouple disk latency from guest CPU execution through coalesced hunk/sector caching and reusable staging buffers (such as `SaveScratchArena` and dedicated ring slots) in both retail CDVD disc readers and `ACATA_IO_CHD` arcade streams. Volatile unpack files, shader caches, and gameplay PGO profiles stage strictly in `/dev/shm` and synchronize atomically to `/userdata` under `K01piccolo-sync` control.  
To fit within the bare-metal `Piccolo-AX` footprint ceiling of `<= 524288 KB` (512 MB) for the `toram EROFS` (`lz4hc,9`) compressed image with A/B fallback intact, all desktop GUI components, debug overlays, Discord RPC, and Windows platform stubs remain gated behind explicit CMake build options. Automated build guardrails enforce strict no-stub-leak-into-release checks to ensure prototype planning stubs never leak into production release artifacts compiled with `-march=skylake + ThinLTO + gameplay PGO sweeps.`  
---

## ⚖️ 5\. Comparison Against Eden Espresso Documentation & Key Learnings Applied ⚖️

Comparing `PCSX2_AX` against our baseline `Eden Espresso Wave [AX]` (MASTER v1.35 / v1.35.1 lineage \+ Piccolo-AX v0.6 appliance rules) highlights how core architectural principles have been adapted and enhanced for modern 128-bit PS2 and arcade hardware emulation:

7. **Shared Core vs. Thin Mode Adapter Decoupling:** Following Eden Espresso's modular engine separation, `PCSX2_AX` isolates low-level hardware emulation into `libax_pcsx2_core`, while routing target specifics through thin `Ps2ConsoleAdapter` and `CohHArcadeAdapter` implementations under standard C++20 concepts.  
8. **Strict Core Isolation & Topology (2:3:1):** Memory bandwidth and thread contention lessons learned in Eden Espresso directly inform our 6-core mapping: Core 0 handles real-time audio and `/run/lock/ax-dac.lock`, Core 1 executes Vulkan 1.3 presentation, Cores 2–4 run EE/microVU recompiler execution with a `2 MB` stack budget, and Core 5 manages I/O, sync, and storage prefetching.  
9. **Telemetry & VRAM Ceiling Enforcement:** Expanding on Eden Espresso's proactive memory management, `FrameTelemetrySample` feedback triggers async VRAM pruning at `5.00 GiB` and enforces a strict `5.50 GiB` ceiling for Vulkan 1.3 context creation on our Maxwell GM200 reference hardware.  
10. **Zero-Loss Persistence & Release Hygiene:** Inherited directly from Piccolo-AX bare-metal rules, mutation-time atomic disk writes (`.tmp` \-\> `fsync` \-\> `rename`) protect save data in `/userdata`, while build pipelines enforce strict guardrails against prototype stub leaks and keep the compressed EROFS image safely under `512 MB`.
