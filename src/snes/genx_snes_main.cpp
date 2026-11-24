#include "ax_snes_adapter.hpp"
#include "snes_core_host.hpp"

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {
volatile std::sig_atomic_t g_stop_flag = 0;

void HandleSignal(int signum) {
    (void)signum;
    g_stop_flag = 1;
}

std::string ParseArg(int argc, char** argv, const std::string& flag) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (flag == argv[i]) {
            return argv[i + 1];
        }
    }
    return {};
}
} // namespace

int main(int argc, char** argv) {
    const std::string config_arg = ParseArg(argc, argv, "--config");
    const std::string config_path = config_arg.empty()
        ? "/userdata/system/piccolo-ax/ax_engine.ini"
        : config_arg;
    const std::string rom_path = ParseArg(argc, argv, "--rom");
    const std::string sram_path = ParseArg(argc, argv, "--sram");
    const std::string preset_override = ParseArg(argc, argv, "--preset");

    if (rom_path.empty()) {
        std::cerr << "[genx-snes-ax] Missing required --rom <path> argument.\n";
        return 2;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    std::signal(SIGUSR1, HandleSignal);

    ax::genx::snes::AxSnesAdapter adapter;
    if (!adapter.Initialize(config_path, rom_path, preset_override, sram_path)) {
        std::cerr << "[genx-snes-ax] Initialization failed.\n";
        return 1;
    }

    if (!preset_override.empty()) {
        std::cerr << "[genx-snes-ax] Active preset: " << preset_override << "\n";
    }

    if (!adapter.StartSession()) {
        std::cerr << "[genx-snes-ax] Failed starting deterministic SNES session.\n";
        return 1;
    }

    const auto& cfg = adapter.config();
    const fs::path rom_fs(rom_path);
    const std::string stem = rom_fs.stem().string();
    const std::string staged_rom = cfg.shm_staging_enabled
        ? (fs::path(cfg.shm_rom_dir) / rom_fs.filename()).string()
        : rom_path;
    const std::string staged_sram = !sram_path.empty()
        ? sram_path
        : (cfg.shm_staging_enabled
            ? (fs::path(cfg.shm_save_dir) / (stem + ".srm")).string()
            : (fs::path(cfg.persistent_save_dir) / (stem + ".srm")).string());

    const bool loop_ok = ax::genx::snes::RunSnesCoreLoop(
        adapter, staged_rom, staged_sram, &g_stop_flag);

    if (!adapter.ShutdownSession()) {
        std::cerr << "[genx-snes-ax] Warning: save flush or DAC lock release reported an issue.\n";
        return 1;
    }

    return loop_ok ? 0 : 1;
}
