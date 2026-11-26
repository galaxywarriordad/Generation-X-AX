#include "ax_retro_bridge.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

#if defined(AX_HAS_CORE_TARGET)
#include "ax/audio.hpp"
#include "ax/config.hpp"
#include "ax/memory.hpp"
#include "ax/telemetry.hpp"
#include "ax/topology.hpp"
#endif

namespace ax::retro {

namespace {

constexpr std::size_t kDefaultRingFrames = 2048;

#if defined(AX_HAS_CORE_TARGET)
constexpr std::size_t kDefaultArenaBytes = 64ULL * 1024ULL * 1024ULL;
#else
struct FallbackTelemetryCounters {
  std::atomic<std::uint64_t> frames_presented{0};
  std::atomic<std::uint32_t> last_frametime_us{0};
  std::atomic<std::uint64_t> audio_buffer_switches{0};
};

FallbackTelemetryCounters& FallbackTelemetry() {
  static FallbackTelemetryCounters counters{};
  return counters;
}
#endif

}  // namespace

struct RetroBridge::Impl {
#if defined(AX_HAS_CORE_TARGET)
  ax::EngineConfig engine_config{};
  ax::topology::SiliconProfile silicon{};
  ax::memory::ArenaHandle fastmem_arena{};
  ax::audio::LockFreeStereoRing<kDefaultRingFrames, float> stereo_ring{};
  bool config_loaded{false};
  bool audio_thread_pinned{false};
  bool telemetry_initialized{false};
#else
  std::array<float, kDefaultRingFrames * 2> fallback_ring{};
  std::size_t fallback_frames{0};
#endif
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
    case EngineId::EDuke32:
      return "eduke32";
    case EngineId::Frontier:
      return "frontier";
    case EngineId::Flycast:
      return "flycast";
    case EngineId::Hatari:
      return "hatari";
  }
  return "unknown";
}

bool RetroBridge::initialize(const BridgeConfig& cfg) {
  if (initialized_) {
    shutdown();
  }

  config_ = cfg;

#if defined(AX_HAS_CORE_TARGET)
  impl_->config_loaded = ax::LoadEngineConfig(config_.config_path, impl_->engine_config);
  impl_->silicon = ax::topology::DetectHostSilicon();
  impl_->audio_thread_pinned = false;

  (void)ax::topology::PinCurrentThread(ax::topology::Domain::GuestJit, 0);
  impl_->fastmem_arena = ax::memory::AllocateFastmemArena(kDefaultArenaBytes, true);

  const bool enable_shm =
      !impl_->config_loaded || impl_->engine_config.enable_shared_memory_export;
  impl_->telemetry_initialized = enable_shm ? ax::InitTelemetryShm() : false;
  if (impl_->telemetry_initialized) {
    ax::ResetTelemetry();
  }
#else
  impl_->fallback_frames = 0;
#endif

  initialized_ = true;
  last_frame_ = 0;
  return true;
}

void RetroBridge::shutdown() {
  if (!initialized_) {
    return;
  }

#if defined(AX_HAS_CORE_TARGET)
  if (impl_->telemetry_initialized) {
    ax::ShutdownTelemetryShm();
    impl_->telemetry_initialized = false;
  }

  ax::memory::FreeFastmemArena(impl_->fastmem_arena);
  impl_->audio_thread_pinned = false;
  impl_->config_loaded = false;
#else
  impl_->fallback_frames = 0;
#endif

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

#if defined(AX_HAS_CORE_TARGET)
  auto& counters = ax::GetTelemetry();
  counters.frames_presented.fetch_add(1, std::memory_order_relaxed);
  if (frame_time_ms > 0.0) {
    const auto frametime_us = static_cast<std::uint32_t>(
        std::min<double>(frame_time_ms * 1000.0, 4294967295.0));
    counters.last_frametime_us.store(frametime_us, std::memory_order_relaxed);
  }
#else
  auto& counters = FallbackTelemetry();
  counters.frames_presented.fetch_add(1, std::memory_order_relaxed);
  if (frame_time_ms > 0.0) {
    const auto frametime_us = static_cast<std::uint32_t>(
        std::min<double>(frame_time_ms * 1000.0, 4294967295.0));
    counters.last_frametime_us.store(frametime_us, std::memory_order_relaxed);
  }
#endif
}

std::size_t RetroBridge::push_audio_interleaved(const float* samples, std::size_t frame_count) {
  if (!initialized_ || samples == nullptr || frame_count == 0) {
    return 0;
  }

#if defined(AX_HAS_CORE_TARGET)
  if (!impl_->audio_thread_pinned) {
    const int prio =
        impl_->config_loaded ? (impl_->engine_config.alsa_period_frames > 0 ? 75 : 0) : 75;
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
#else
  const std::size_t space = (kDefaultRingFrames > impl_->fallback_frames)
      ? (kDefaultRingFrames - impl_->fallback_frames)
      : 0;
  const std::size_t pushed = std::min(frame_count, space);
  for (std::size_t i = 0; i < pushed; ++i) {
    const std::size_t dst = (impl_->fallback_frames + i) * 2;
    impl_->fallback_ring[dst + 0] = samples[i * 2 + 0];
    impl_->fallback_ring[dst + 1] = samples[i * 2 + 1];
  }
  impl_->fallback_frames += pushed;
  if (pushed < frame_count) {
    FallbackTelemetry().audio_buffer_switches.fetch_add(1, std::memory_order_relaxed);
  }
  return pushed;
#endif
}

}  // namespace ax::retro
