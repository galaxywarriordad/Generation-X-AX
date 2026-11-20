#include "ax_retro_bridge.hpp"

#include <algorithm>
#include <iostream>

// Align these includes with ax-sdk (v1_36_1) include/ax/ headers once verified:
// #include "ax/config.hpp"
// #include "ax/topology.hpp"
// #include "ax/memory.hpp"
// #include "ax/audio.hpp"
// #include "ax/telemetry.hpp"

namespace ax::retro {

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

  // 1. Load ax_engine.ini via ax::config
  // 2. Apply 2:3:1 core pinning & SCHED_FIFO/MMCSS via ax::topology
  // 3. Reserve large-page / fastmem pools via ax::memory
  // 4. Initialize LockFreeStereoRing via ax::audio
  // 5. Register 64-byte aligned telemetry counters / SHM via ax::telemetry

  initialized_ = true;
  last_frame_ = 0;
  return true;
}

void RetroBridge::shutdown() {
  if (!initialized_) {
    return;
  }

  // Teardown telemetry SHM, audio ring, memory pools, and topology state cleanly.
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
  (void)frame_time_ms;
}

std::size_t RetroBridge::push_audio_interleaved(const float* samples, std::size_t frame_count) {
  if (!initialized_ || samples == nullptr || frame_count == 0) {
    return 0;
  }

  // Route stereo frames into ax::audio LockFreeStereoRing.
  return frame_count;
}

}  // namespace ax::retro
