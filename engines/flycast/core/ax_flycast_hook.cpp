#include "ax_flycast_hook.hpp"

#include <algorithm>
#include <array>

#include "ax/topology.hpp"
#include "ax_retro_bridge.hpp"

namespace ax::flycast_hook {

namespace {
ax::retro::RetroBridge g_bridge;
}  // namespace

void Init() {
  if (g_bridge.is_initialized()) {
    return;
  }
  ax::retro::BridgeConfig cfg{};
  cfg.engine = ax::retro::EngineId::Flycast;
  cfg.config_path = "ax_engine.ini";
  cfg.sample_rate = 48000;
  cfg.channels = 2;
  g_bridge.initialize(cfg);
  PinHousekeepingThread();
}

void Shutdown() {
  g_bridge.shutdown();
}

void PinHousekeepingThread() {
  (void)ax::topology::PinCurrentThread(ax::topology::Domain::Housekeeping, 0);
}

void PinVulkanThread() {
  (void)ax::topology::PinCurrentThread(ax::topology::Domain::VulkanSubmission, 0);
}

void PinJitThread() {
  (void)ax::topology::PinCurrentThread(ax::topology::Domain::GuestJit, 0);
}

void BeginFrame(std::uint64_t frame_index) {
  g_bridge.begin_frame(frame_index);
}

void EndFrame(std::uint64_t frame_index, double frame_time_ms) {
  g_bridge.end_frame(frame_index, frame_time_ms);
}

void PushAicaSamples(const std::int16_t* stereo_s16, std::size_t frame_count) {
  if (!g_bridge.is_initialized() || stereo_s16 == nullptr || frame_count == 0) {
    return;
  }
  constexpr float kScale = 1.0f / 32768.0f;
  std::array<float, 512> stereo_f32{};
  std::size_t offset = 0;

  while (offset < frame_count) {
    const std::size_t batch = std::min<std::size_t>(frame_count - offset, 256);
    for (std::size_t i = 0; i < batch * 2; ++i) {
      stereo_f32[i] = static_cast<float>(stereo_s16[offset * 2 + i]) * kScale;
    }
    (void)g_bridge.push_audio_interleaved(stereo_f32.data(), batch);
    offset += batch;
  }
}

}  // namespace ax::flycast_hook
