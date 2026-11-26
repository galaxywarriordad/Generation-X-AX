// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal prototype CLI harness for PCSX2_AX (planning stage: stay tuned!).
// Parses --mode ps2 or --mode coh-h and initializes IHardwareModeAdapter
// against ax-sdk v1_36_2 runtime guardrails.

#include "adapters/coh_h/CohHArcadeAdapter.hpp"
#include "adapters/ps2/Ps2ConsoleAdapter.hpp"
#include "core/IHardwareModeAdapter.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

#if __has_include(<ax/runtime/guardrails.hpp>)
#include <ax/runtime/guardrails.hpp>
#define PCSX2_AX_HAS_AX_GUARDRAILS 1
#else
#define PCSX2_AX_HAS_AX_GUARDRAILS 0
#endif

namespace {

void print_usage(std::string_view argv0) noexcept {
    std::cerr << "Usage: " << argv0 << " --mode <ps2|coh-h> [--dry-run]\n"
              << "  --mode ps2    Retail PlayStation 2 adapter (/userdata/saves/ps2/)\n"
              << "  --mode coh-h  Namco System 246/256 adapter (/userdata/saves/pcsx2x6/)\n"
              << "  --dry-run     Initialize and cleanly shut down without running ticks\n";
}

void apply_ax_sdk_v1_36_2_guardrails() noexcept {
#if PCSX2_AX_HAS_AX_GUARDRAILS
    // Delegates 2:3:1 core isolation, HugeTLB/mlockall, HiBy FC3 /run/lock/ax-dac.lock,
    // and Vulkan 1.3 VRAM limits (5.00 GiB prune / 5.50 GiB ceiling) to ax-sdk v1_36_2.
    ax::runtime::apply_appliance_guardrails(ax::runtime::GuardrailConfig{
        .sdk_version_tag   = "v1_36_2",
        .vram_prune_gib    = 5.00,
        .vram_ceiling_gib  = 5.50,
        .shm_sync_daemon   = "K01piccolo-sync",
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

    std::unique_ptr<pcsx2_ax::core::IHardwareModeAdapter> adapter;
    if (mode_arg == "ps2") {
        adapter = std::make_unique<pcsx2_ax::adapters::ps2::Ps2ConsoleAdapter>();
    } else if (mode_arg == "coh-h") {
        adapter = std::make_unique<pcsx2_ax::adapters::coh_h::CohHArcadeAdapter>();
    } else {
        std::cerr << "Error: --mode must be 'ps2' or 'coh-h'.\n";
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    apply_ax_sdk_v1_36_2_guardrails();

    if (!adapter->initialize()) {
        std::cerr << "Failed to initialize mode adapter for --mode " << mode_arg << "\n";
        return EXIT_FAILURE;
    }

    const auto bios = adapter->bios_descriptor();
    std::cout << "[PCSX2_AX prototype | planning phase: stay tuned!]\n"
              << "Mode:           " << mode_arg << "\n"
              << "Region/Board:   " << bios.region_code << " (ROM 0x" << std::hex << bios.rom_version << std::dec << ")\n"
              << "Save Directory: " << adapter->save_directory() << "\n"
              << "Shared Cache:   /dev/shm (synced via K01piccolo-sync)\n";

    if (!dry_run && !adapter->reset()) {
        std::cerr << "Failed to reset mode adapter for --mode " << mode_arg << "\n";
        adapter->shutdown();
        return EXIT_FAILURE;
    }

    adapter->shutdown();
    return EXIT_SUCCESS;
}
