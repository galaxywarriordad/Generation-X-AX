#include "ax_midi_mpk3.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

#if defined(__linux__) && defined(AX_FRONTIER_HAS_ALSA)
#include <alsa/asoundlib.h>
#define AX_MPK3_USE_ALSA 1
#else
#define AX_MPK3_USE_ALSA 0
#endif

namespace ax::retro {

namespace {

uint64_t NowNanoseconds() noexcept {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

} // namespace

MpkMini3FrontierBridge::MpkMini3FrontierBridge(const MpkMini3Config& cfg)
    : cfg_(cfg) {
  if (cfg_.max_events_per_frame == 0) {
    cfg_.max_events_per_frame = kMaxEventsPerFrame;
  }
  if (cfg_.stick_deadzone < 0.0f || cfg_.stick_deadzone >= 0.5f) {
    cfg_.stick_deadzone = 0.06f;
  }
}

MpkMini3FrontierBridge::~MpkMini3FrontierBridge() {
  Stop();
}

bool MpkMini3FrontierBridge::Start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    return true;
  }
  head_.store(0, std::memory_order_relaxed);
  tail_.store(0, std::memory_order_relaxed);
  sustain_active_.store(false, std::memory_order_relaxed);
  pad_bank_b_.store(false, std::memory_order_relaxed);
  stardreamer_tick_accum_.store(0, std::memory_order_relaxed);

  listener_thread_ = std::thread(&MpkMini3FrontierBridge::ListenerLoop, this);
  return true;
}

void MpkMini3FrontierBridge::Stop() {
  if (!running_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  if (listener_thread_.joinable()) {
    listener_thread_.join();
  }
}

bool MpkMini3FrontierBridge::IsRunning() const noexcept {
  return running_.load(std::memory_order_acquire);
}

bool MpkMini3FrontierBridge::PushEvent(const Mpk3Event& ev) {
  const size_t current_head = head_.load(std::memory_order_relaxed);
  const size_t next_head = (current_head + 1) & kRingMask;
  if (next_head == tail_.load(std::memory_order_acquire)) {
    dropped_events_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  ring_[current_head] = ev;
  head_.store(next_head, std::memory_order_release);
  return true;
}

bool MpkMini3FrontierBridge::PopEvent(Mpk3Event& out_ev) {
  const size_t current_tail = tail_.load(std::memory_order_relaxed);
  if (current_tail == head_.load(std::memory_order_acquire)) {
    return false;
  }
  out_ev = ring_[current_tail];
  tail_.store((current_tail + 1) & kRingMask, std::memory_order_release);
  return true;
}

float MpkMini3FrontierBridge::ApplyStickCurve(float raw_norm) const noexcept {
  const float clamped = std::clamp(raw_norm, -1.0f, 1.0f);
  const float abs_v = std::fabs(clamped);
  if (abs_v <= cfg_.stick_deadzone) {
    return 0.0f;
  }
  const float sign = (clamped >= 0.0f) ? 1.0f : -1.0f;
  const float scaled = (abs_v - cfg_.stick_deadzone) / (1.0f - cfg_.stick_deadzone);
  // Exponential trim response for fine dogfight and docking alignment
  return sign * (scaled * scaled);
}

FrontierInputCommand MpkMini3FrontierBridge::TranslateEvent(const Mpk3Event& ev) {
  FrontierInputCommand cmd{};
  cmd.raw_data1 = ev.data1;
  cmd.raw_data2 = ev.data2;
  cmd.sustain_held = sustain_active_.load(std::memory_order_acquire);
  cmd.timestamp_ns = (ev.timestamp_ns != 0) ? ev.timestamp_ns : NowNanoseconds();

  if (ev.kind == Mpk3EventKind::PitchBend && ev.channel == 0) {
    const float norm = static_cast<float>(ev.pitch_bend) / 8192.0f;
    cmd.command = FrontierCommandId::TrimRollYaw;
    cmd.axis_value = ApplyStickCurve(norm);
    return cmd;
  }

  if (ev.kind == Mpk3EventKind::ControlChange) {
    if (ev.data1 == cfg_.sustain_shift_cc) {
      const bool held = (ev.data2 >= 64);
      sustain_active_.store(held, std::memory_order_release);
      cmd.command = FrontierCommandId::None;
      return cmd;
    }

    if (ev.channel == 0) {
      switch (ev.data1) {
        case 1: { // Thumbstick Y (Mod Wheel CC 1) -> Pitch Trim
          const float centered = (static_cast<float>(ev.data2) - 64.0f) / 64.0f;
          cmd.command = FrontierCommandId::TrimPitch;
          cmd.axis_value = ApplyStickCurve(centered);
          return cmd;
        }
        case 70: // Encoder K1 -> Main Thruster / Set-Speed Trim
          cmd.command = FrontierCommandId::ThrottleTrim;
          cmd.axis_value = static_cast<float>(ev.data2) / 127.0f;
          return cmd;
        case 71: // Encoder K2 -> Drive Mode Selector
          cmd.command = FrontierCommandId::CycleDriveMode;
          cmd.step_delta = (ev.data2 >= 64) ? 1 : -1;
          return cmd;
        case 72: // Encoder K3 -> Scanner / Radar Zoom
          cmd.command = FrontierCommandId::ZoomRadar;
          cmd.step_delta = (ev.data2 >= 64) ? 1 : -1;
          return cmd;
        case 73: // Encoder K4 -> HUD / MFD Panel Cycle
          cmd.command = FrontierCommandId::CycleHudMfd;
          cmd.step_delta = (ev.data2 >= 64) ? 1 : -1;
          return cmd;
        case 74: // Encoder K5 -> Weapon / Laser Capacitor Bias
          cmd.command = FrontierCommandId::LaserCapBias;
          cmd.axis_value = static_cast<float>(ev.data2) / 127.0f;
          return cmd;
        case 75: // Encoder K6 -> Shield / Drive Power Balance
          cmd.command = FrontierCommandId::ShieldDriveBias;
          cmd.axis_value = static_cast<float>(ev.data2) / 127.0f;
          return cmd;
        case 76: { // Encoder K7 -> Time Compression (Stardreamer) with guard
          const int32_t dir = (ev.data2 >= 64) ? 1 : -1;
          if (!cfg_.time_compression_guard) {
            cmd.command = FrontierCommandId::StepTimeCompression;
            cmd.step_delta = dir;
            return cmd;
          }
          const uint8_t prev = stardreamer_tick_accum_.load(std::memory_order_relaxed);
          const uint8_t next = static_cast<uint8_t>(prev + 1u);
          stardreamer_tick_accum_.store(next, std::memory_order_release);
          if ((next % 2u) == 0u) {
            cmd.command = FrontierCommandId::StepTimeCompression;
            cmd.step_delta = dir;
          }
          return cmd;
        }
        case 77: // Encoder K8 -> Master / Cockpit Audio Trim
          cmd.command = FrontierCommandId::MasterAudioTrim;
          cmd.axis_value = static_cast<float>(ev.data2) / 127.0f;
          return cmd;
        default:
          break;
      }
    }
  }

  if (ev.kind == Mpk3EventKind::NoteOn && ev.data2 > 0) {
    // Channel 10 (index 9): MPC Pads Bank A / Bank B
    if (ev.channel == 9) {
      const uint8_t pad_idx = static_cast<uint8_t>(ev.data1 % 8u);
      const bool bank_b = (ev.data1 >= 44) || pad_bank_b_.load(std::memory_order_acquire);
      if (!bank_b) {
        static constexpr FrontierCommandId kBankACmds[8] = {
            FrontierCommandId::FirePrimaryLaser,
            FrontierCommandId::LaunchMissile,
            FrontierCommandId::TargetNearestHostile,
            FrontierCommandId::CycleNextTarget,
            FrontierCommandId::ToggleAutopilot,
            FrontierCommandId::EngageDockingComputer,
            FrontierCommandId::MatchTargetVelocity,
            FrontierCommandId::ResetTimeCompression1x,
        };
        cmd.command = kBankACmds[pad_idx];
      } else {
        static constexpr FrontierCommandId kBankBCmds[8] = {
            FrontierCommandId::HyperspaceJump,
            FrontierCommandId::JettisonOrEscapeGuarded,
            FrontierCommandId::ToggleLandingGear,
            FrontierCommandId::ActivateEcm,
            FrontierCommandId::OpenStarMap,
            FrontierCommandId::OpenSystemMap,
            FrontierCommandId::OpenShipStatus,
            FrontierCommandId::ToggleTelemetryOverlay,
        };
        cmd.command = kBankBCmds[pad_idx];
      }
      cmd.axis_value = static_cast<float>(ev.data2) / 127.0f;
      return cmd;
    }

    // Channel 1 (index 0): 25-note keybed presets
    if (ev.channel == 0) {
      cmd.command = FrontierCommandId::CameraOrMfdPreset;
      cmd.step_delta = static_cast<int32_t>(ev.data1);
      cmd.axis_value = static_cast<float>(ev.data2) / 127.0f;
      return cmd;
    }
  }

  return cmd;
}

void MpkMini3FrontierBridge::PinListenerThreadToConfiguredCore() {
#if defined(__linux__)
  if (cfg_.listener_core_mask == 0) {
    return;
  }
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  for (int cpu = 0; cpu < 64; ++cpu) {
    if (cfg_.listener_core_mask & (1ULL << cpu)) {
      CPU_SET(cpu, &cpuset);
    }
  }
  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

void MpkMini3FrontierBridge::ListenerLoop() {
  PinListenerThreadToConfiguredCore();

#if AX_MPK3_USE_ALSA
  while (running_.load(std::memory_order_acquire)) {
    snd_rawmidi_t* midi_in = nullptr;
    const int open_rc = snd_rawmidi_open(&midi_in, cfg_.device.c_str(), nullptr, SND_RAWMIDI_NONBLOCK);
    if (open_rc < 0 || !midi_in) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      continue;
    }

    uint8_t buf[64];
    uint8_t running_status = 0;
    while (running_.load(std::memory_order_acquire)) {
      const ssize_t n = snd_rawmidi_read(midi_in, buf, sizeof(buf));
      if (n < 0) {
        if (n == -EAGAIN) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          continue;
        }
        break;
      }
      if (n == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }

      const uint64_t ts = NowNanoseconds();
      for (ssize_t i = 0; i < n; ) {
        uint8_t b = buf[i];
        if (b >= 0xF8) {
          // Real-time system message: single byte, does not alter running status
          ++i;
          continue;
        }
        if (b & 0x80) {
          if (b < 0xF0) {
            running_status = b;
          }
          ++i;
          continue;
        }
        if (running_status == 0) {
          ++i;
          continue;
        }

        const uint8_t msg_type = running_status & 0xF0;
        const uint8_t ch = running_status & 0x0F;
        if (msg_type == 0xE0) {
          if (i + 1 >= n) break;
          const uint8_t lsb = buf[i] & 0x7F;
          const uint8_t msb = buf[i + 1] & 0x7F;
          i += 2;
          const int16_t bend = static_cast<int16_t>((static_cast<int32_t>(msb) << 7) | lsb) - 8192;
          Mpk3Event ev{};
          ev.kind = Mpk3EventKind::PitchBend;
          ev.channel = ch;
          ev.data1 = lsb;
          ev.data2 = msb;
          ev.pitch_bend = bend;
          ev.timestamp_ns = ts;
          total_polled_events_.fetch_add(1, std::memory_order_relaxed);
          PushEvent(ev);
        } else {
          if (i + 1 >= n) break;
          const uint8_t d1 = buf[i] & 0x7F;
          const uint8_t d2 = buf[i + 1] & 0x7F;
          i += 2;
          Mpk3Event ev{};
          ev.channel = ch;
          ev.data1 = d1;
          ev.data2 = d2;
          ev.timestamp_ns = ts;
          if (msg_type == 0x90) {
            ev.kind = (d2 > 0) ? Mpk3EventKind::NoteOn : Mpk3EventKind::NoteOff;
          } else if (msg_type == 0x80) {
            ev.kind = Mpk3EventKind::NoteOff;
          } else if (msg_type == 0xB0) {
            ev.kind = Mpk3EventKind::ControlChange;
          } else {
            continue;
          }
          total_polled_events_.fetch_add(1, std::memory_order_relaxed);
          PushEvent(ev);
        }
      }
    }

    snd_rawmidi_close(midi_in);
  }
#else
  while (running_.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
#endif
}

} // namespace ax::retro
