#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace ax::retro {

enum class EngineId : std::uint8_t {
  Rmg = 0,
  BlastEm = 1,
  Cannonball = 2,
  EDuke32 = 3,
  Frontier = 4,
};

struct BridgeConfig {
  EngineId engine{EngineId::Rmg};
  std::string config_path{"ax_engine.ini"};
  std::uint32_t sample_rate{48000};
  std::uint16_t channels{2};
};

class RetroBridge {
public:
  RetroBridge();
  ~RetroBridge();

  RetroBridge(const RetroBridge&) = delete;
  RetroBridge& operator=(const RetroBridge&) = delete;

  bool initialize(const BridgeConfig& cfg);
  void shutdown();

  void begin_frame(std::uint64_t frame_index);
  void end_frame(std::uint64_t frame_index, double frame_time_ms);

  // Interleaved stereo PCM samples (left, right, left, right...)
  std::size_t push_audio_interleaved(const float* samples, std::size_t frame_count);

  [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }
  [[nodiscard]] EngineId engine() const noexcept { return config_.engine; }
  [[nodiscard]] std::string_view engine_name() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  BridgeConfig config_{};
  bool initialized_{false};
  std::uint64_t last_frame_{0};
};

}  // namespace ax::retro
