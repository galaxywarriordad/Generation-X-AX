#include "ax_retro_bridge.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

#include "ax/audio.hpp"
#include "ax/config.hpp"
#include "ax/memory.hpp"
#include "ax/telemetry.hpp"
#include "ax/topology.hpp"

namespace ax::retro {

namespace {

constexpr std::size_t kDefaultRingFrames = 2048;
constexpr std::size_t kDefaultArenaBytes = 64ULL * 1024ULL * 1024ULL;

}  // namespace

struct RetroBridge::Impl {
  ax::EngineConfig engine_config{};
  ax::topology::SiliconProfile silicon{};
  ax::memory::ArenaHandle fastmem_arena{};
  ax::audio::LockFreeStereoRing<kDefaultRingFrames, float> stereo_ring{};
  bool config_loaded{false};
  bool audio_thread_pinned{false};
  bool telemetry_initialized{false};
};

RetroBridge::RetroBridge() : impl_(std::make_unique<Impl>()) {}

RetroBridge::~RetroBridge() {
  if (initialized_) {
    shutdown();
  }
}

std::string_view RetroBridge::engine_name() const noexcept {
  switch (config_.engine) {
    case EngineId::Rmg:
      return "rmg";
    case EngineId::BlastEm:
      return "blastem";
    case EngineId::Cannonball:
      return "cannonball";
  }
  return "unknown";
}

bool RetroBridge::initialize(const BridgeConfig& cfg) {
  if (initialized_) {
    shutdown();
  }

  config_ = cfg;
  impl_->config_loaded = ax::LoadEngineConfig(config_.config_path, impl_->engine_config);
  impl_->silicon = ax::topology::DetectHostSilicon();
  impl_->audio_thread_pinned = false;

  // Pin calling/main emulation thread to GuestJit domain.
  (void)ax::topology::PinCurrentThread(ax::topology::Domain::GuestJit, 0);

  // Reserve fastmem arena via ax::memory header API.
  impl_->fastmem_arena = ax::memory::AllocateFastmemArena(kDefaultArenaBytes, true);

  // Initialize shared-memory telemetry if enabled (or default-enabled when config wasn't found).
  const bool enable_shm =
      !impl_->config_loaded || impl_->engine_config.enable_shared_memory_export;
  impl_->telemetry_initialized = enable_shm ? ax::InitTelemetryShm() : false;
  if (impl_->telemetry_initialized) {
    ax::ResetTelemetry();
  }

  initialized_ = true;
  last_frame_ = 0;
  return true;
}

void RetroBridge::shutdown() {
  if (!initialized_) {
    return;
  }

  if (impl_->telemetry_initialized) {
    ax::ShutdownTelemetryShm();
    impl_->telemetry_initialized = false;
  }

  ax::memory::FreeFastmemArena(impl_->fastmem_arena);
  impl_->audio_thread_pinned = false;
  impl_->config_loaded = false;
  initialized_ = false;
}

void RetroBridge::begin_frame(std::uint64_t frame_index) {
  if (!initialized_) {
    return;
  }
  last_frame_ = frame_index;
}

void RetroBridge::end_frame(std::uint64_t frame_index, double frame_time_ms) {
  if (!initialized_) {
    return;
  }
  last_frame_ = frame_index;

  auto& counters = ax::GetTelemetry();
  counters.frames_presented.fetch_add(1, std::memory_order_relaxed);
  if (frame_time_ms > 0.0) {
    const auto frametime_us = static_cast<std::uint32_t>(
        std::min<double>(frame_time_ms * 1000.0, 4294967295.0));
    counters.last_frametime_us.store(frametime_us, std::memory_order_relaxed);
  }
}

std::size_t RetroBridge::push_audio_interleaved(const float* samples, std::size_t frame_count) {
  if (!initialized_ || samples == nullptr || frame_count == 0) {
    return 0;
  }

  if (!impl_->audio_thread_pinned) {
    const int prio = impl_->config_loaded ? impl_->engine_config.alsa_period_frames > 0 ? 75 : 0 : 75;
    impl_->audio_thread_pinned =
        ax::topology::PinCurrentThread(ax::topology::Domain::AudioTiming, prio);
  }

  const auto start = std::chrono::steady_clock::now();
  const std::size_t pushed = impl_->stereo_ring.Push(samples, frame_count);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  const auto duration_us = static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
  const auto fill_frames = static_cast<std::uint32_t>(impl_->stereo_ring.AvailableFrames());
  ax::RecordAudioCallbackMetrics(duration_us, fill_frames);

  if (pushed < frame_count) {
    ax::GetTelemetry().audio_buffer_switches.fetch_add(1, std::memory_order_relaxed);
  }

  return pushed;
}

}  // namespace ax::retro
```[cite: 6, 7, 9, 11, 13]

### What to Check Next
* If your `ax-sdk` checkout has `allocate_fastmem_arena` in `src/memory.cpp` rather than `AllocateFastmemArena`, check `include/ax/memory.hpp` vs `src/memory.cpp` when compiling so the linker sees matching symbols[cite: 6, 16].
* Also check `include/ax/config.hpp` for the exact field name of `enable_shared_memory_export` (in `config.cpp` it's parsed into `out_config.enable_shared_memory_export`)[cite: 13].

With **Step 2** wired up, you're ready for **Step 3** (`Piccolo-AX` `.mk` recipes). Would you like the updated `rmg-ax.mk`, `blastem-ax.mk`, and `cannonball-ax.mk` recipes next?
