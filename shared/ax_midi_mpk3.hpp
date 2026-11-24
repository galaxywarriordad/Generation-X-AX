#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

namespace ax::retro {

enum class Mpk3EventKind : uint8_t {
  None = 0,
  NoteOn,
  NoteOff,
  ControlChange,
  PitchBend,
  SustainShift
};

struct alignas(64) Mpk3Event {
  Mpk3EventKind kind = Mpk3EventKind::None;
  uint8_t channel = 0;      // 0-indexed (0 => MIDI Ch 1, 9 => MIDI Ch 10)
  uint8_t data1 = 0;        // Note / CC number
  uint8_t data2 = 0;        // Velocity / CC value
  int16_t pitch_bend = 0;   // -8192 .. +8191 centered at 0
  float normalized = 0.0f;  // Deadzone/curve-processed axis or CC value
  uint64_t timestamp_ns = 0;
};

enum class FrontierCommandId : uint8_t {
  None = 0,
  TrimRollYaw,
  TrimPitch,
  ThrottleTrim,
  CycleDriveMode,
  ZoomRadar,
  CycleHudMfd,
  LaserCapBias,
  ShieldDriveBias,
  StepTimeCompression,
  ResetTimeCompression1x,
  MasterAudioTrim,
  FirePrimaryLaser,
  LaunchMissile,
  TargetNearestHostile,
  CycleNextTarget,
  ToggleAutopilot,
  EngageDockingComputer,
  MatchTargetVelocity,
  HyperspaceJump,
  JettisonOrEscapeGuarded,
  ToggleLandingGear,
  ActivateEcm,
  OpenStarMap,
  OpenSystemMap,
  OpenShipStatus,
  ToggleTelemetryOverlay,
  CameraOrMfdPreset
};

struct FrontierInputCommand {
  FrontierCommandId command = FrontierCommandId::None;
  float axis_value = 0.0f;
  int32_t step_delta = 0;
  uint8_t raw_data1 = 0;
  uint8_t raw_data2 = 0;
  bool sustain_held = false;
  uint64_t timestamp_ns = 0;
};

struct MpkMini3Config {
  std::string device = "hw:CARD=MPKmini3,DEV=0";
  uint64_t listener_core_mask = 0x20ULL; // Core 5 (0x20)
  size_t ring_capacity = 256;
  size_t max_events_per_frame = 32;
  float stick_deadzone = 0.06f;
  bool time_compression_guard = true;
  uint8_t sustain_shift_cc = 64;
};

class MpkMini3FrontierBridge {
 public:
  static constexpr size_t kDefaultRingCapacity = 256;
  static constexpr size_t kMaxEventsPerFrame = 32;

  explicit MpkMini3FrontierBridge(const MpkMini3Config& cfg = {});
  ~MpkMini3FrontierBridge();

  MpkMini3FrontierBridge(const MpkMini3FrontierBridge&) = delete;
  MpkMini3FrontierBridge& operator=(const MpkMini3FrontierBridge&) = delete;

  bool Start();
  void Stop();
  bool IsRunning() const noexcept;

  // Safe from non-MIDI/simulation threads (e.g. unit tests or synthetic injection)
  bool PushEvent(const Mpk3Event& ev);

  // Drain up to max_events_per_frame (default 32) on simulation thread (Cores 2-4, 0x1C)
  template <typename Fn>
  size_t DrainFrameEvents(Fn&& on_command, size_t max_events = 0) {
    const size_t limit = (max_events > 0)
        ? std::min(max_events, cfg_.max_events_per_frame)
        : cfg_.max_events_per_frame;
    size_t drained = 0;
    Mpk3Event ev{};
    while (drained < limit && PopEvent(ev)) {
      FrontierInputCommand cmd = TranslateEvent(ev);
      if (cmd.command != FrontierCommandId::None) {
        on_command(cmd);
      }
      ++drained;
    }
    return drained;
  }

  uint64_t DroppedEvents() const noexcept {
    return dropped_events_.load(std::memory_order_acquire);
  }
  uint64_t TotalPolledEvents() const noexcept {
    return total_polled_events_.load(std::memory_order_acquire);
  }

 private:
  bool PopEvent(Mpk3Event& out_ev);
  FrontierInputCommand TranslateEvent(const Mpk3Event& ev);
  float ApplyStickCurve(float raw_norm) const noexcept;
  void ListenerLoop();
  void PinListenerThreadToConfiguredCore();

  MpkMini3Config cfg_;
  std::atomic<bool> running_{false};
  std::thread listener_thread_;

  static constexpr size_t kRingSlots = 256;
  static constexpr size_t kRingMask = kRingSlots - 1;
  std::array<Mpk3Event, kRingSlots> ring_{};
  alignas(64) std::atomic<size_t> head_{0};
  alignas(64) std::atomic<size_t> tail_{0};

  std::atomic<bool> sustain_active_{false};
  std::atomic<bool> pad_bank_b_{false};
  std::atomic<uint8_t> stardreamer_tick_accum_{0};
  std::atomic<uint64_t> dropped_events_{0};
  std::atomic<uint64_t> total_polled_events_{0};
};

} // namespace ax::retro
