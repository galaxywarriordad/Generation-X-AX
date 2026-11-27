// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Prototype CLI harness for PCSX2_AX (Prototype Phase Ã¢â‚¬â€ stay tuned!).
// Parses --mode ps2 or --mode coh-h and exercises IHardwareModeAdapter
// against ax-sdk v1_36_2 runtime guardrails.

#include "core/FlacAxAudioBridge.hpp"
#include "core/IHardwareModeAdapter.hpp"
#include "core/VMManager_Internal.h"

#if __has_include("adapters/ps2/Ps2ConsoleAdapter.hpp")
#include "adapters/ps2/Ps2ConsoleAdapter.hpp"
#define PCSX2_AX_HAS_PS2_ADAPTER_HDR 1
#else
#define PCSX2_AX_HAS_PS2_ADAPTER_HDR 0
#endif

#if __has_include("adapters/coh_h/CohHArcadeAdapter.hpp")
#include "adapters/coh_h/CohHArcadeAdapter.hpp"
#define PCSX2_AX_HAS_COHH_ADAPTER_HDR 1
#else
#define PCSX2_AX_HAS_COHH_ADAPTER_HDR 0
#endif

#if __has_include(<ax/runtime/guardrails.hpp>)
#include <ax/runtime/guardrails.hpp>
#define PCSX2_AX_HAS_AX_GUARDRAILS 1
#else
#define PCSX2_AX_HAS_AX_GUARDRAILS 0
#endif

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace {

void print_usage(std::string_view argv0) noexcept {
std::cerr << "Usage: " << argv0 << " --mode <ps2|coh-h> [--dac <spec>] [--dac-conf <path>] [--dop <dsd64|dsd128>] [--epoch <N>] [--dry-run]\n"
          << "  --mode ps2         Retail PlayStation 2 adapter (/userdata/saves/ps2/)\n"
          << "  --mode coh-h       Namco System 246/256 adapter (/userdata/saves/pcsx2x6/)\n"
          << "  --dac <spec>       DAC profile (fc3, fc3_1v3, es9281pro, ka11, blackpearl, dawnpro2, a2155)\n"
          << "  --dac-conf <path>  Optional AX_DAC_VOL config file path\n"
          << "  --dop <rate>       Exercise push_dop_stereo_bytes with dsd64 (176.4kHz) or dsd128 (352.8kHz)\n"
          << "  --epoch <N>        Starting non-zero VM epoch (default: 1)\n"
          << "  --dry-run          Initialize and shut down cleanly without running vsync/reset/epoch step\n";
}

pcsx2_ax::audio::DsdRateKind parse_dop_rate(std::string_view s, bool& ok) noexcept {
ok = true;
if (s == "dsd64" || s == "DSD64") {
return pcsx2_ax::audio::DsdRateKind::Dsd64;
}
if (s == "dsd128" || s == "DSD128") {
return pcsx2_ax::audio::DsdRateKind::Dsd128;
}
ok = false;
return pcsx2_ax::audio::DsdRateKind::Unsupported;
}

bool parse_positive_epoch(std::string_view text, uint64_t& out_epoch) noexcept {
if (text.empty()) {
return false;
}
uint64_t parsed = 0;
const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), parsed);
if (ec != std::errc{} || ptr != text.data() + text.size() || parsed == 0) {
return false;
}
out_epoch = parsed;
return true;
}

void apply_ax_sdk_v1_36_2_guardrails() noexcept {
#if PCSX2_AX_HAS_AX_GUARDRAILS
// Delegates 2:3:1 core isolation, HugeTLB/mlockall, HiBy FC3 /run/lock/ax-dac.lock,
// and Vulkan 1.3 VRAM limits (5.00 GiB prune / 5.50 GiB ceiling) to ax-sdk v1_36_2.
ax::runtime::apply_appliance_guardrails(ax::runtime::GuardrailConfig{
.sdk_version_tag  = "v1_36_2",
.vram_prune_gib   = pcsx2_ax::core::kVramPruneGib,
.vram_ceiling_gib = pcsx2_ax::core::kVramCeilingGib,
.shm_sync_daemon  = "K01piccolo-sync",
});
#endif
}

class HarnessModeAdapter final : public pcsx2_ax::core::IHardwareModeAdapter {
public:
HarnessModeAdapter(pcsx2_ax::core::HardwareMode mode,
                   std::string_view save_dir,
                   std::shared_ptr<pcsx2_ax::core::IMediaProvider> media) noexcept
: mode_(mode), save_dir_(save_dir), media_(std::move(media)) {}

[[nodiscard]] pcsx2_ax::core::HardwareMode mode() const noexcept override { return mode_; }
[[nodiscard]] std::string_view save_directory() const noexcept override { return save_dir_; }

[[nodiscard]] bool initialize() override { return true; }
[[nodiscard]] bool reset() override { return true; }
void shutdown() noexcept override { media_.reset(); }
void on_vsync() noexcept override {}

[[nodiscard]] pcsx2_ax::core::BiosDescriptor bios_descriptor() const noexcept override {
if (media_ != nullptr) {
return media_->bios_descriptor();
}
return {};
}

[[nodiscard]] bool read_media_sectors(uint32_t lba,
                                      uint32_t sector_count,
                                      std::span<uint8_t> out_bytes,
                                      uint16_t& out_sector_bytes) override {
out_sector_bytes = 2048u;
if (media_ == nullptr || out_bytes.empty()) {
return false;
}
return media_->read_sectors(lba, sector_count, out_bytes.data());
}

[[nodiscard]] bool freeze_state(StateWrapper&) override { return true; }

private:
pcsx2_ax::core::HardwareMode mode_{pcsx2_ax::core::HardwareMode::Ps2Console};
std::string_view save_dir_{"/userdata/saves/ps2/"};
std::shared_ptr<pcsx2_ax::core::IMediaProvider> media_{};
};
} // namespace

int main(int argc, char** argv) {
std::string_view mode_arg{};
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
print_usage(argv[0]);
return EXIT_FAILURE;
}
has_dop_arg = true;
} else if (arg.starts_with("--dop=")) {
bool ok = false;
dop_rate_arg = parse_dop_rate(arg.substr(sizeof("--dop=") - 1), ok);
if (!ok) {
std::cerr << "Error: --dop requires 'dsd64' or 'dsd128'.\n";
print_usage(argv[0]);
return EXIT_FAILURE;
}
has_dop_arg = true;
} else if (arg == "--epoch") {
if (i + 1 >= argc || !parse_positive_epoch(argv[++i], initial_epoch)) {
std::cerr << "Error: --epoch requires a positive integer (>= 1).\n";
print_usage(argv[0]);
return EXIT_FAILURE;
}
} else if (arg.starts_with("--epoch=")) {
if (!parse_positive_epoch(arg.substr(sizeof("--epoch=") - 1), initial_epoch)) {
std::cerr << "Error: --epoch requires a positive integer (>= 1).\n";
print_usage(argv[0]);
return EXIT_FAILURE;
}
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

pcsx2_ax::core::VmContext ctx{};
ctx.epoch.store(initial_epoch, std::memory_order_release);
pcsx2_ax::vm_bridge::set_active_context(&ctx);

std::shared_ptr<pcsx2_ax::core::IMediaProvider> mode_media;
std::unique_ptr<pcsx2_ax::core::IHardwareModeAdapter> adapter;
if (mode_arg == "ps2") {
#if PCSX2_AX_HAS_PS2_ADAPTER_HDR
mode_media = std::make_shared<pcsx2_ax::adapters::ps2::Ps2ConsoleAdapter>(pcsx2_ax::core::BiosDescriptor{}, std::filesystem::path{});
adapter = std::make_unique<HarnessModeAdapter>(pcsx2_ax::core::HardwareMode::Ps2Console, "/userdata/saves/ps2/", mode_media);
#else
std::cerr << "Ps2ConsoleAdapter.hpp not yet present; running VmContext/epoch smoke check.\n";
#endif
} else if (mode_arg == "coh-h") {
#if PCSX2_AX_HAS_COHH_ADAPTER_HDR
mode_media = std::make_shared<pcsx2_ax::adapters::coh_h::CohHArcadeAdapter>(pcsx2_ax::core::BiosDescriptor{}, std::filesystem::path{}, std::filesystem::path{});
adapter = std::make_unique<HarnessModeAdapter>(pcsx2_ax::core::HardwareMode::CohHArcade, "/userdata/saves/pcsx2x6/", mode_media);
#else
std::cerr << "CohHArcadeAdapter.hpp not yet present; running VmContext/epoch smoke check.\n";
#endif
} else {
std::cerr << "Error: --mode must be 'ps2' or 'coh-h'.\n";
print_usage(argv[0]);
pcsx2_ax::vm_bridge::set_active_context(nullptr);
return EXIT_FAILURE;
}

apply_ax_sdk_v1_36_2_guardrails();
pcsx2_ax::vm_bridge::bind_mode_adapter(adapter.get(), ctx);

if (adapter != nullptr && !pcsx2_ax::vm_bridge::init_mode_subsystems(ctx)) {
std::cerr << "Failed to initialize mode adapter for --mode " << mode_arg << "\n";
pcsx2_ax::vm_bridge::set_active_context(nullptr);
return EXIT_FAILURE;
}

pcsx2_ax::audio::FlacAxAudioBridge audio_probe{};
const auto active_mode = (adapter != nullptr)
? adapter->mode()
: pcsx2_ax::core::HardwareMode::Ps2Console;
audio_probe.configure(active_mode, dac_arg, pcsx2_ax::audio::AudioLoadTarget::Speakers, dac_conf_arg);
(void)audio_probe.initialize(initial_epoch, true);
if (has_dop_arg) {
// 16 stereo DSD byte pairs -> 8 packed DoP stereo frames (0x05 / 0xFA markers)
std::array<std::uint8_t, 16U * 2U> dsd_lr_bytes{};
for (std::size_t idx = 0U; idx < dsd_lr_bytes.size(); ++idx) {
dsd_lr_bytes[idx] = static_cast<std::uint8_t>(0xA5U ^ static_cast<std::uint8_t>(idx & 0x7FU));
}
const std::size_t dop_pushed = audio_probe.push_dop_stereo_bytes(dsd_lr_bytes.data(), 16U, dop_rate_arg);
std::array<std::int32_t, 16U * 2U> dop_out{};
const std::size_t dop_drained = audio_probe.drain_to_s32(dop_out.data(), 16U);
std::cout << "DoP Probe:      rate=" << pcsx2_ax::audio::ToString(dop_rate_arg)
          << ", pushed_frames=" << dop_pushed
          << ", drained_frames=" << dop_drained
          << ", sample0_hi_byte=0x" << std::hex
          << ((static_cast<std::uint32_t>(dop_out[0]) >> 24) & 0xFFU) << std::dec << "\n";
}

const auto snap = pcsx2_ax::vm_bridge::current_snapshot(ctx);
const auto& ap = audio_probe.mode_profile();
const auto init_at = audio_probe.telemetry();
std::cout << "[PCSX2_AX Prototype Phase | stay tuned!]\n"
          << "Mode:           " << mode_arg << "\n"
          << "Epoch:          " << snap.epoch << " (ctx.epoch=" << ctx.epoch.load(std::memory_order_acquire) << ")\n"
          << "Dry Run:        " << (dry_run ? "yes" : "no") << "\n"
          << "Save Directory: " << snap.save_directory << "\n"
          << "Preload Tier:   " << pcsx2_ax::audio::ToString(init_at.preload_mode)
          << " (cap=" << (audio_probe.preload_policy().max_total_bytes / (1024U * 1024U)) << " MiB)\n"
          << "Audio Sink:     FLAC_AX (" << audio_probe.chip_profile().chip_name
          << " @ " << ap.sample_rate_hz << " Hz, " << ap.period_frames << "x" << ap.buffer_periods
          << " frames, vol=" << audio_probe.effective_volume()
          << ", dsd=" << pcsx2_ax::audio::ToString(init_at.dsd_rate)
          << ", pts_delta_us=" << init_at.av_delta_us
          << ", dac_lock=" << (audio_probe.is_dac_locked() ? "locked" : "busy") << ")\n"
          << "Shared Cache:   /dev/shm (synced via K01piccolo-sync)\n";

if (!dry_run && adapter != nullptr) {
std::array<float, 96U * 2U> spu2_block{};
spu2_block[0] = 0.25f;
spu2_block[1] = -0.25f;
const std::size_t pushed = audio_probe.push_spu2_float_frames(spu2_block.data(), 96U);
std::array<std::int32_t, 96U * 2U> dac_s32{};
const std::size_t drained = audio_probe.drain_to_s32(dac_s32.data(), 96U);
audio_probe.set_pts_drift_us(-14);

pcsx2_ax::vm_bridge::notify_mode_vsync(ctx);
const uint64_t next_epoch = pcsx2_ax::vm_bridge::advance_epoch(ctx);
audio_probe.on_epoch_transition(next_epoch);
const auto next_snap = pcsx2_ax::vm_bridge::current_snapshot(ctx);
const auto at = audio_probe.telemetry();
std::cout << "Advanced Epoch: " << next_snap.epoch << " (ctx.epoch=" << next_epoch
          << ", audio_pushed=" << pushed << ", audio_drained=" << drained
          << ", pts_delta_us=" << at.av_delta_us
          << ", epoch_flushes=" << at.epoch_flushes << ")\n";

if (!pcsx2_ax::vm_bridge::reset_mode_subsystems(ctx)) {
std::cerr << "Failed to reset mode adapter for --mode " << mode_arg << "\n";
audio_probe.shutdown();
pcsx2_ax::vm_bridge::shutdown_mode_subsystems(ctx);
pcsx2_ax::vm_bridge::set_active_context(nullptr);
return EXIT_FAILURE;
}
}

audio_probe.shutdown();
pcsx2_ax::vm_bridge::shutdown_mode_subsystems(ctx);
pcsx2_ax::vm_bridge::set_active_context(nullptr);
return EXIT_SUCCESS;
}

