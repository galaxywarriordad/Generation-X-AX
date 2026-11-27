// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ax_engine_registry.hpp"
#include "core/AxSdkSiliconBridge.hpp"
#include "core/FlacAxAudioBridge.hpp"
#include "core/VMManager_Internal.h"
#include "adapters/ps2/Ps2ConsoleAdapter.hpp"
#include "adapters/coh_h/CohHArcadeAdapter.hpp"
#include "flycast/core/ax_flycast_hook.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

inline const char* dop_label(pcsx2_ax::audio::DsdRateKind rate, bool has_dop) noexcept {
  if (!has_dop) {
    return "none";
  }
  switch (rate) {
    case pcsx2_ax::audio::DsdRateKind::Dsd64:  return "dsd64";
    case pcsx2_ax::audio::DsdRateKind::Dsd128: return "dsd128";
    default:                                   return "unsupported";
  }
}

inline pcsx2_ax::audio::DsdRateKind parse_dop(std::string_view s, bool& ok) noexcept {
  ok = true;
  if (s == "dsd64")  return pcsx2_ax::audio::DsdRateKind::Dsd64;
  if (s == "dsd128") return pcsx2_ax::audio::DsdRateKind::Dsd128;
  ok = false;
  return pcsx2_ax::audio::DsdRateKind::Unsupported;
}

inline pcsx2_ax::core::HardwareMode resolve_mode(const ax::engines::EngineDescriptor& eng,
                                                 std::string_view mode_arg,
                                                 bool& ok) noexcept {
  ok = true;
  if (mode_arg.empty()) {
    return eng.bridge_mode;
  }
  if (mode_arg == "ps2") {
    return pcsx2_ax::core::HardwareMode::Ps2Console;
  }
  if (mode_arg == "coh-h" || mode_arg == "pcsx2x6") {
    return pcsx2_ax::core::HardwareMode::CohHArcade;
  }
  ok = false;
  return eng.bridge_mode;
}

inline void print_list() {
  for (const auto& eng : ax::engines::kEngineCatalog) {
    std::cout << eng.cli_name
              << " | " << eng.display_name
              << " | hz=" << eng.default_hz
              << " | save_dir=" << eng.save_dir
              << " | midi_mpk3=" << (eng.supports_midi_mpk3 ? eng.default_midi_device : "no")
              << "\n";
  }
}

inline void print_usage(const char* prog) {
  std::cout << "Usage: " << (prog ? prog : "ax_all_engines_verify")
            << " [--list] [--engine <id>] [--mode ps2|coh-h] [--profile <id>]"
            << " [--dac <name>] [--midi-device <dev>] [--dop dsd64|dsd128]"
            << " [--epoch <u64>] [--doctor] [--dry-run]\n";
}

struct FrontierMidiSummary {
  size_t drained_events{0};
  ax_frontier_midi_cmd_t last_cmd{AX_FRONTIER_MIDI_CMD_NONE};
  float last_axis{0.0f};
  int32_t warp_steps{0};
};

inline void sample_frontier_midi_cb(const ax_frontier_midi_event_t* ev, void* user_data) {
  auto* out = static_cast<FrontierMidiSummary*>(user_data);
  if (!ev || !out) {
    return;
  }
  ++out->drained_events;
  out->last_cmd = ev->command;
  out->last_axis = ev->axis_value;
  if (ev->command == AX_FRONTIER_MIDI_CMD_STEP_TIME_COMPRESSION) {
    out->warp_steps += ev->step_delta;
  } else if (ev->command == AX_FRONTIER_MIDI_CMD_RESET_TIME_COMPRESSION_1X) {
    out->warp_steps = 0;
  }
}

} // namespace

int main(int argc, char** argv) {
  bool list_only = false;
  std::string_view engine_arg{"pcsx2_ax"};
  std::string_view mode_arg{};
  std::string_view profile_arg{"auto"};
  std::string_view dac_arg{"fc3"};
  std::string_view midi_dev_arg{"hw:CARD=MPKmini3,DEV=0"};
  pcsx2_ax::audio::DsdRateKind dop_rate{pcsx2_ax::audio::DsdRateKind::Unsupported};
  bool has_dop = false;
  uint64_t epoch = 1u;
  bool doctor = false;
  bool dry_run = false;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "--list") {
      list_only = true;
    } else if (arg == "--engine" && i + 1 < argc) {
      engine_arg = argv[++i];
    } else if (arg.starts_with("--engine=")) {
      engine_arg = arg.substr(sizeof("--engine=") - 1);
    } else if (arg == "--mode" && i + 1 < argc) {
      mode_arg = argv[++i];
    } else if (arg.starts_with("--mode=")) {
      mode_arg = arg.substr(sizeof("--mode=") - 1);
    } else if (arg == "--profile" && i + 1 < argc) {
      profile_arg = argv[++i];
    } else if (arg.starts_with("--profile=")) {
      profile_arg = arg.substr(sizeof("--profile=") - 1);
    } else if (arg == "--dac" && i + 1 < argc) {
      dac_arg = argv[++i];
    } else if (arg.starts_with("--dac=")) {
      dac_arg = arg.substr(sizeof("--dac=") - 1);
    } else if (arg == "--midi-device" && i + 1 < argc) {
      midi_dev_arg = argv[++i];
    } else if (arg.starts_with("--midi-device=")) {
      midi_dev_arg = arg.substr(sizeof("--midi-device=") - 1);
    } else if (arg == "--dop" && i + 1 < argc) {
      bool ok = false;
      dop_rate = parse_dop(argv[++i], ok);
      if (!ok) {
        std::cerr << "Error: --dop requires dsd64 or dsd128.\n";
        return EXIT_FAILURE;
      }
      has_dop = true;
    } else if (arg.starts_with("--dop=")) {
      bool ok = false;
      dop_rate = parse_dop(arg.substr(sizeof("--dop=") - 1), ok);
      if (!ok) {
        std::cerr << "Error: --dop requires dsd64 or dsd128.\n";
        return EXIT_FAILURE;
      }
      has_dop = true;
    } else if (arg == "--epoch" && i + 1 < argc) {
      epoch = static_cast<uint64_t>(std::strtoull(argv[++i], nullptr, 10));
    } else if (arg.starts_with("--epoch=")) {
      const std::string tmp{arg.substr(sizeof("--epoch=") - 1)};
      epoch = static_cast<uint64_t>(std::strtoull(tmp.c_str(), nullptr, 10));
    } else if (arg == "--doctor") {
      doctor = true;
    } else if (arg == "--dry-run") {
      dry_run = true;
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return EXIT_SUCCESS;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      print_usage(argv[0]);
      return EXIT_FAILURE;
    }
  }

  if (list_only) {
    print_list();
    return EXIT_SUCCESS;
  }

  const auto* eng = ax::engines::find_engine(engine_arg);
  if (eng == nullptr) {
    std::cerr << "Error: unknown engine '" << engine_arg << "'.\n";
    print_list();
    return EXIT_FAILURE;
  }

  bool mode_ok = false;
  const auto hw_mode = resolve_mode(*eng, mode_arg, mode_ok);
  if (!mode_ok) {
    std::cerr << "Error: unsupported mode '" << mode_arg << "'.\n";
    return EXIT_FAILURE;
  }

  pcsx2_ax::sdk::AxSdkSiliconBridge bridge{};
  bridge.configure(hw_mode, profile_arg);
  (void)bridge.initialize(epoch);

  auto& ctx = pcsx2_ax::vm_bridge::default_context();
  ctx.epoch.store(epoch == 0u ? 1u : epoch, std::memory_order_release);
  pcsx2_ax::vm_bridge::set_active_context(&ctx);

  pcsx2_ax::core::BiosDescriptor bios{};
  std::filesystem::path save_root{eng->save_dir};
  double hz = eng->default_hz;
  bool env_complete = true;
  bool coh_ready = true;
  bool flycast_hook_ok = false;

  if (eng->id == ax::engines::EngineId::Pcsx2Ax) {
    if (hw_mode == pcsx2_ax::core::HardwareMode::CohHArcade) {
      pcsx2_ax::adapters::coh_h::CohHArcadeAdapter coh(
          bios, {}, {}, std::filesystem::path{"/userdata/saves/pcsx2x6"}, 60.0);
      const auto env = coh.make_environment();
      env_complete = env.is_complete();
      hz = env.clock_irq ? env.clock_irq->nominal_refresh_hz() : 60.0;
      save_root = env.persistence ? env.persistence->save_directory() : save_root;
      coh_ready = coh.run_preflight().ready_to_boot();
    } else {
      pcsx2_ax::adapters::ps2::Ps2ConsoleAdapter ps2(
          bios, {}, std::filesystem::path{"/userdata/saves/ps2"}, false);
      const auto env = ps2.make_environment();
      env_complete = env.is_complete();
      hz = env.clock_irq ? env.clock_irq->nominal_refresh_hz() : 59.94;
      save_root = env.persistence ? env.persistence->save_directory() : save_root;
    }
  } else if (eng->id == ax::engines::EngineId::Flycast) {
    ax::flycast_hook::Init();
    ax::flycast_hook::PinHousekeepingThread();
    ax::flycast_hook::PinVulkanThread();
    ax::flycast_hook::PinJitThread();
    ax::flycast_hook::BeginFrame(0u);
    std::array<std::int16_t, 32> silence{};
    ax::flycast_hook::PushAicaSamples(silence.data(), 16u);
    ax::flycast_hook::EndFrame(0u, 16.67);
    ax::flycast_hook::Shutdown();
    flycast_hook_ok = true;
  }

  FrontierMidiSummary midi_summary{};
  if (eng->supports_midi_mpk3) {
    const std::string dev_str{midi_dev_arg};
    (void)ax_frontier_pin_thread(AX_FRONTIER_DOMAIN_GUEST_JIT, 0);
    const int midi_rc = ax_frontier_midi_init(dev_str.c_str(), 0x20ULL, 32);
    if (midi_rc == 0) {
      (void)ax_frontier_step_frame(sample_frontier_midi_cb, &midi_summary, 32);
      ax_frontier_midi_shutdown();
    }
  }

  const auto& prof = bridge.profile();
  std::cout << (doctor ? "[engine-doctor] " : "[engine-harness] ")
            << "engine=" << eng->cli_name
            << " profile=" << prof.profile_name
            << " topology=" << prof.topology_label
            << " hz=" << hz
            << " save_dir=" << save_root.generic_string()
            << " env_complete=" << (env_complete ? "true" : "false")
            << " coh_ready=" << (coh_ready ? "true" : "false")
            << " flycast_hook=" << (flycast_hook_ok ? "true" : "n/a")
            << " dac=" << dac_arg
            << " dop=" << dop_label(dop_rate, has_dop)
            << " midi_mpk3=" << (eng->supports_midi_mpk3 ? "enabled" : "n/a")
            << " midi_dev=" << (eng->supports_midi_mpk3 ? midi_dev_arg : std::string_view{"none"})
            << " midi_events=" << midi_summary.drained_events
            << " midi_cmd=" << ax::engines::midi_cmd_name(midi_summary.last_cmd)
            << " midi_axis=" << midi_summary.last_axis
            << " epoch=" << bridge.active_epoch()
            << " dry_run=" << (dry_run ? "true" : "false") << "\n";

  return EXIT_SUCCESS;
}