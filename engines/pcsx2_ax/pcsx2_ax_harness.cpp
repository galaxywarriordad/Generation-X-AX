// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AxSdkSiliconBridge.hpp"
#include "FlacAxAudioBridge.hpp"
#include "IHardwareModeAdapter.hpp"
#include "ModeCapabilities.hpp"
#include "VMManager_Internal.h"
#include "adapters/ps2/Ps2ConsoleAdapter.hpp"
#include "adapters/coh_h/CohHArcadeAdapter.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

inline const char* dop_rate_label(pcsx2_ax::audio::DsdRateKind rate, bool has_dop) noexcept {
if (!has_dop) {
return "none";
}
switch (rate) {
case pcsx2_ax::audio::DsdRateKind::Dsd64:  return "dsd64";
case pcsx2_ax::audio::DsdRateKind::Dsd128: return "dsd128";
default:                                   return "unsupported";
}
}

inline pcsx2_ax::core::HardwareMode resolve_mode_enum(std::string_view mode_arg, bool& ok) noexcept {
ok = true;
if (mode_arg.empty() || mode_arg == "ps2") {
return pcsx2_ax::core::HardwareMode::Ps2Console;
}
if (mode_arg == "coh-h" || mode_arg == "pcsx2x6") {
return pcsx2_ax::core::HardwareMode::CohHArcade;
}
ok = false;
return pcsx2_ax::core::HardwareMode::Ps2Console;
}

inline int run_harness_doctor(pcsx2_ax::core::HardwareMode hw_mode,
                              std::string_view mode_label,
                              std::string_view profile_arg,
                              std::string_view dac_arg,
                              std::string_view dac_conf_arg,
                              pcsx2_ax::audio::DsdRateKind dop_rate,
                              bool has_dop,
                              uint64_t epoch) {
pcsx2_ax::sdk::AxSdkSiliconBridge bridge{};
bridge.configure(hw_mode, profile_arg);
(void)bridge.initialize(epoch);

const auto& prof = bridge.profile();
const auto& host = bridge.host();
const auto pf = bridge.preflight();

std::cout << "[doctor] mode=" << mode_label
          << " profile=" << prof.profile_name
          << " topology=" << prof.topology_label
          << " cores=" << host.physical_cores << "/" << host.logical_processors
          << " vram_prune_gib=" << prof.vram_prune_gib
          << " vram_ceiling_gib=" << prof.vram_ceiling_gib
          << " bar1_staging_mib=" << prof.bar1_staging_mib
          << " preflight=" << pf.summary
          << " dac=" << dac_arg
          << " dac_conf=" << dac_conf_arg
          << " dop=" << dop_rate_label(dop_rate, has_dop)
          << " epoch=" << bridge.active_epoch() << "\n";
return EXIT_SUCCESS;
}

inline void print_usage(const char* prog) {
std::cout << "Usage: " << (prog ? prog : "pcsx2_ax_harness")
          << " [--mode ps2|coh-h] [--profile <id>] [--dac <name>] [--dac-conf <path>]"
          << " [--dop dsd64|dsd128] [--epoch <u64>] [--doctor] [--dry-run]\n";
}

inline pcsx2_ax::audio::DsdRateKind parse_dop_rate(std::string_view s, bool& ok) noexcept {
ok = true;
if (s == "dsd64") {
return pcsx2_ax::audio::DsdRateKind::Dsd64;
}
if (s == "dsd128") {
return pcsx2_ax::audio::DsdRateKind::Dsd128;
}
ok = false;
return pcsx2_ax::audio::DsdRateKind::Unsupported;
}

} // namespace

int main(int argc, char** argv) {
std::string_view mode_arg{};
std::string_view sdk_arg{"auto"};
bool run_doctor = false;
std::string_view dac_arg{"fc3"};
std::string_view dac_conf_arg{"/userdata/system/ax-dac.conf"};
pcsx2_ax::audio::DsdRateKind dop_rate_arg{pcsx2_ax::audio::DsdRateKind::Unsupported};
bool has_dop_arg = false;
uint64_t initial_epoch = 1u;
bool dry_run = false;

for (int i = 1; i < argc; ++i) {
const std::string_view arg{argv[i]};
if (arg == "--mode") {
if (i + 1 >= argc) {
print_usage(argv[0]);
return EXIT_FAILURE;
}
mode_arg = argv[++i];
} else if (arg.starts_with("--mode=")) {
mode_arg = arg.substr(sizeof("--mode=") - 1);
} else if (arg == "--profile") {
if (i + 1 >= argc) {
print_usage(argv[0]);
return EXIT_FAILURE;
}
sdk_arg = argv[++i];
} else if (arg.starts_with("--profile=")) {
sdk_arg = arg.substr(sizeof("--profile=") - 1);
} else if (arg == "--doctor") {
run_doctor = true;
} else if (arg == "--dac") {
if (i + 1 >= argc) {
print_usage(argv[0]);
return EXIT_FAILURE;
}
dac_arg = argv[++i];
} else if (arg.starts_with("--dac=")) {
dac_arg = arg.substr(sizeof("--dac=") - 1);
} else if (arg == "--dac-conf") {
if (i + 1 >= argc) {
print_usage(argv[0]);
return EXIT_FAILURE;
}
dac_conf_arg = argv[++i];
} else if (arg.starts_with("--dac-conf=")) {
dac_conf_arg = arg.substr(sizeof("--dac-conf=") - 1);
} else if (arg == "--dop") {
if (i + 1 >= argc) {
print_usage(argv[0]);
return EXIT_FAILURE;
}
bool ok = false;
dop_rate_arg = parse_dop_rate(argv[++i], ok);
if (!ok) {
std::cerr << "Error: --dop requires 'dsd64' or 'dsd128'.\n";
return EXIT_FAILURE;
}
has_dop_arg = true;
} else if (arg.starts_with("--dop=")) {
bool ok = false;
dop_rate_arg = parse_dop_rate(arg.substr(sizeof("--dop=") - 1), ok);
if (!ok) {
std::cerr << "Error: --dop requires 'dsd64' or 'dsd128'.\n";
return EXIT_FAILURE;
}
has_dop_arg = true;
} else if (arg == "--epoch") {
if (i + 1 >= argc) {
print_usage(argv[0]);
return EXIT_FAILURE;
}
initial_epoch = static_cast<uint64_t>(std::strtoull(argv[++i], nullptr, 10));
} else if (arg.starts_with("--epoch=")) {
const auto sv = arg.substr(sizeof("--epoch=") - 1);
std::string tmp(sv);
initial_epoch = static_cast<uint64_t>(std::strtoull(tmp.c_str(), nullptr, 10));
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

bool mode_ok = false;
const auto active_mode = resolve_mode_enum(mode_arg, mode_ok);
const std::string_view mode_label = mode_arg.empty() ? "ps2" : mode_arg;
if (!mode_ok) {
std::cerr << "Error: unsupported --mode '" << mode_arg << "' (expected ps2 or coh-h).\n";
return EXIT_FAILURE;
}

if (run_doctor) {
return run_harness_doctor(active_mode, mode_label, sdk_arg, dac_arg, dac_conf_arg, dop_rate_arg, has_dop_arg, initial_epoch);
}

pcsx2_ax::sdk::AxSdkSiliconBridge sdk_bridge{};
sdk_bridge.configure(active_mode, sdk_arg);
(void)sdk_bridge.initialize(initial_epoch);

auto& ctx = pcsx2_ax::vm_bridge::default_context();
ctx.epoch.store(initial_epoch, std::memory_order_release);
pcsx2_ax::vm_bridge::set_active_context(&ctx);

pcsx2_ax::core::BiosDescriptor bios{};
std::unique_ptr<pcsx2_ax::adapters::ps2::Ps2ConsoleAdapter> ps2_adapter{};
std::unique_ptr<pcsx2_ax::adapters::coh_h::CohHArcadeAdapter> coh_adapter{};
pcsx2_ax::core::ModeEnvironment env{};
const char* adapter_name = "Ps2ConsoleAdapter";
bool coh_ready = true;
double nominal_hz = 60.0;
std::filesystem::path save_dir{};

if (active_mode == pcsx2_ax::core::HardwareMode::Ps2Console) {
ps2_adapter = std::make_unique<pcsx2_ax::adapters::ps2::Ps2ConsoleAdapter>(
bios,
std::filesystem::path{},
std::filesystem::path{"/userdata/saves/ps2"},
false);
env = ps2_adapter->make_environment();
adapter_name = "Ps2ConsoleAdapter";
} else {
coh_adapter = std::make_unique<pcsx2_ax::adapters::coh_h::CohHArcadeAdapter>(
bios,
std::filesystem::path{},
std::filesystem::path{},
std::filesystem::path{"/userdata/saves/pcsx2x6"},
60.0);
env = coh_adapter->make_environment();
adapter_name = "CohHArcadeAdapter";
const auto pf = coh_adapter->run_preflight();
coh_ready = pf.ready_to_boot();
}

if (env.persistence != nullptr) {
save_dir = env.persistence->save_directory();
}
if (env.clock_irq != nullptr) {
nominal_hz = env.clock_irq->nominal_refresh_hz();
if (!dry_run) {
env.clock_irq->on_epoch_drain(initial_epoch);
}
}

std::cout << "[harness] mode=" << mode_label
          << " adapter=" << adapter_name
          << " env_complete=" << (env.is_complete() ? "true" : "false")
          << " hz=" << nominal_hz
          << " save_dir=" << save_dir.generic_string()
          << " profile=" << sdk_bridge.profile().profile_name
          << " coh_preflight_ready=" << (coh_ready ? "true" : "false")
          << " dac=" << dac_arg
          << " dop=" << dop_rate_label(dop_rate_arg, has_dop_arg)
          << " epoch=" << initial_epoch
          << " dry_run=" << (dry_run ? "true" : "false") << "\n";

return EXIT_SUCCESS;
}