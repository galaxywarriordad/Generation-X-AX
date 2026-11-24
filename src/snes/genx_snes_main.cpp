#include "ax_snes_adapter.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

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
    const std::string config_path = ParseArg(argc, argv, "--config").empty()
        ? "/userdata/system/piccolo-ax/ax_engine.ini"
        : ParseArg(argc, argv, "--config");
    const std::string rom_path = ParseArg(argc, argv, "--rom");
    const std::string preset_override = ParseArg(argc, argv, "--preset");

    if (rom_path.empty()) {
        std::cerr << "[genx-snes-ax] Missing required --rom <path> argument.\n";
        return 2;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    std::signal(SIGUSR1, HandleSignal);

    ax::genx::snes::AxSnesAdapter adapter;
    if (!adapter.Initialize(config_path, rom_path)) {
        std::cerr << "[genx-snes-ax] Initialization failed.\n";
        return 1;
    }

    if (!preset_override.empty()) {
        std::cerr << "[genx-snes-ax] Preset override requested: " << preset_override << "\n";
    }

    if (!adapter.StartSession()) {
        std::cerr << "[genx-snes-ax] Failed starting deterministic SNES session.\n";
        return 1;
    }

    while (!g_stop_flag) {
        const int ppm = adapter.ComputeRateGuardPpm(50);
        (void)ppm;
        break;
    }

    if (!adapter.ShutdownSession()) {
        std::cerr << "[genx-snes-ax] Warning: save flush or DAC lock release reported an issue.\n";
        return 1;
    }

    return 0;
}
