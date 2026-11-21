#include "ax_hatari_hook.h"

#include <algorithm>
#include <array>

#include "ax/topology.hpp"
#include "ax_retro_bridge.hpp"

namespace {
ax::retro::RetroBridge g_bridge;
}  // namespace

extern "C" {

void ax_hatari_init(uint32_t sample_rate) {
  if (g_bridge.is_initialized()) {
    return;
  }
  ax::retro::BridgeConfig cfg{};
  cfg.engine = ax::retro::EngineId::Hatari;
  cfg.config_path = "ax_engine.ini";
  cfg.sample_rate = sample_rate > 0 ? sample_rate : 48000;
  cfg.channels = 2;
  g_bridge.initialize(cfg);
  ax_hatari_pin_jit_thread();
}

void ax_hatari_shutdown(void) {
  g_bridge.shutdown();
}

void ax_hatari_pin_jit_thread(void) {
  (void)ax::topology::PinCurrentThread(ax::topology::Domain::GuestJit, 0);
}

void ax_hatari_pin_housekeeping_thread(void) {
  (void)ax::topology::PinCurrentThread(ax::topology::Domain::Housekeeping, 0);
}

void ax_hatari_begin_frame(uint64_t frame_index) {
  g_bridge.begin_frame(frame_index);
}

void ax_hatari_end_frame(uint64_t frame_index, double frame_time_ms) {
  g_bridge.end_frame(frame_index, frame_time_ms);
}

void ax_hatari_push_audio_s16(const int16_t *stereo_s16, size_t frame_count) {
  if (!g_bridge.is_initialized() || stereo_s16 == nullptr || frame_count == 0) {
    return;
  }
  constexpr float kScale = 1.0f / 32768.0f;
  std::array<float, 512> stereo_f32{};
  size_t offset = 0;

  while (offset < frame_count) {
    const size_t batch = std::min<size_t>(frame_count - offset, 256);
    for (size_t i = 0; i < batch * 2; ++i) {
      stereo_f32[i] = static_cast<float>(stereo_s16[offset * 2 + i]) * kScale;
    }
    (void)g_bridge.push_audio_interleaved(stereo_f32.data(), batch);
    offset += batch;
  }
}

}  // extern "C"
