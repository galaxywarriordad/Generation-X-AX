// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Prototype CLI harness for PCSX2_AX (Prototype Phase — stay tuned!).
// Parses --mode ps2 or --mode coh-h and exercises IHardwareModeAdapter
// against ax-sdk v1_36_2 runtime guardrails.

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

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace {

void print_usage(std::string_view argv0) noexcept {
	std::cerr << "Usage: " << argv0 << " --mode <ps2|coh-h> [--dry-run]\n"
	          << "  --mode ps2    Retail PlayStation 2 adapter (/userdata/saves/ps2/)\n"
	          << "  --mode coh-h  Namco System 246/256 adapter (/userdata/saves/pcsx2x6/)\n"
	          << "  --dry-run     Initialize and shut down cleanly without running reset/tick pass\n";
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

} // namespace

int main(int argc, char** argv) {
	std::string_view mode_arg{};
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
	pcsx2_ax::vm_bridge::set_active_context(&ctx);

	std::unique_ptr<pcsx2_ax::core::IHardwareModeAdapter> adapter;
	if (mode_arg == "ps2") {
#if PCSX2_AX_HAS_PS2_ADAPTER_HDR
		adapter = std::make_unique<pcsx2_ax::adapters::ps2::Ps2ConsoleAdapter>();
#else
		std::cerr << "Ps2ConsoleAdapter.hpp not yet present; running VmContext/epoch smoke check.\n";
#endif
	} else if (mode_arg == "coh-h") {
#if PCSX2_AX_HAS_COHH_ADAPTER_HDR
		adapter = std::make_unique<pcsx2_ax::adapters::coh_h::CohHArcadeAdapter>();
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

	const auto snap = pcsx2_ax::vm_bridge::current_snapshot(ctx);
	std::cout << "[PCSX2_AX Prototype Phase | stay tuned!]\n"
	          << "Mode:           " << mode_arg << "\n"
	          << "Epoch:          " << snap.epoch << "\n"
	          << "Save Directory: " << snap.save_directory << "\n"
	          << "Shared Cache:   /dev/shm (synced via K01piccolo-sync)\n";

	if (!dry_run && adapter != nullptr) {
		pcsx2_ax::vm_bridge::notify_mode_vsync(ctx);
		if (!pcsx2_ax::vm_bridge::reset_mode_subsystems(ctx)) {
			std::cerr << "Failed to reset mode adapter for --mode " << mode_arg << "\n";
			pcsx2_ax::vm_bridge::shutdown_mode_subsystems(ctx);
			pcsx2_ax::vm_bridge::set_active_context(nullptr);
			return EXIT_FAILURE;
		}
	}

	pcsx2_ax::vm_bridge::shutdown_mode_subsystems(ctx);
	pcsx2_ax::vm_bridge::set_active_context(nullptr);
	return EXIT_SUCCESS;
}
