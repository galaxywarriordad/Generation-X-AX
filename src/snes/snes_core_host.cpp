#include "snes_core_host.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

namespace ax::genx::snes {

namespace {

class DeterministicSnesCoreRunner final : public ISnesCoreEngine {
public:
    bool LoadCartridge(const std::string& staged_rom_path,
                       const std::string& staged_sram_path,
                       const SnesRuntimeConfig& cfg) override {
        if (staged_rom_path.empty()) {
            std::cerr << "[genx-snes-core] Empty staged ROM path.\n";
            return false;
        }
        rom_path_ = staged_rom_path;
        sram_path_ = staged_sram_path;
        loaded_ = true;

        std::cerr << "[genx-snes-core] Backend=" << cfg.engine_backend
                  << " Profile=" << cfg.hardware_profile
                  << " ROM=" << rom_path_
                  << " SRAM=" << (sram_path_.empty() ? "<none>" : sram_path_)
                  << " coproc_overclock=" << cfg.coprocessor_overclock_pct << "%"
                  << " boost=" << (cfg.thread_boost_coprocessor ? "on" : "off")
                  << " runahead=" << cfg.runahead_frames << "\n";
        return true;
    }

    bool StepFrame(const SnesRuntimeConfig& cfg,
                   int rate_guard_ppm,
                   SnesFramePacingState& out_state) override {
        if (!loaded_) return false;

        const auto t0 = std::chrono::steady_clock::now();
        const double ppm_scale = 1.0 - (static_cast<double>(rate_guard_ppm) / 1'000'000.0);
        const auto base_period = std::chrono::microseconds(16639);
        const auto paced_period = std::chrono::microseconds(
            std::max<long long>(1000, static_cast<long long>(base_period.count() * ppm_scale)));

        std::this_thread::sleep_for(paced_period);
        const auto t1 = std::chrono::steady_clock::now();

        ++frame_counter_;
        const int wave = static_cast<int>((frame_counter_ % 17) - 8);
        const int fill_pct = std::clamp(
            cfg.rate_guard_ring_target_pct + wave, 0, 100);

        out_state.frame_index = frame_counter_;
        out_state.last_frametime_us = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        out_state.ring_fill_pct = fill_pct;
        out_state.rate_guard_ppm = rate_guard_ppm;
        out_state.coprocessor_boost_active = cfg.thread_boost_coprocessor;
        out_state.runahead_frames = cfg.runahead_frames;
        return true;
    }

    void UnloadCartridge() override {
        loaded_ = false;
        rom_path_.clear();
        sram_path_.clear();
    }

private:
    std::string rom_path_{};
    std::string sram_path_{};
    bool loaded_{false};
    uint64_t frame_counter_{0};
};

} // namespace

std::unique_ptr<ISnesCoreEngine> CreateSnesCoreEngine(const SnesRuntimeConfig& cfg) {
    (void)cfg;
    return std::make_unique<DeterministicSnesCoreRunner>();
}

bool RunSnesCoreLoop(AxSnesAdapter& adapter,
                     const std::string& staged_rom_path,
                     const std::string& staged_sram_path,
                     volatile std::sig_atomic_t* stop_flag) {
    const SnesRuntimeConfig& cfg = adapter.config();
    auto engine = CreateSnesCoreEngine(cfg);
    if (!engine) {
        std::cerr << "[genx-snes-core] Failed creating SNES core engine.\n";
        return false;
    }

    if (!engine->LoadCartridge(staged_rom_path, staged_sram_path, cfg)) {
        return false;
    }

    int ring_fill_pct = cfg.rate_guard_ring_target_pct;
    SnesFramePacingState state{};

    while (stop_flag == nullptr || *stop_flag == 0) {
        const int ppm = adapter.ComputeRateGuardPpm(ring_fill_pct);
        if (!engine->StepFrame(cfg, ppm, state)) {
            engine->UnloadCartridge();
            return false;
        }
        ring_fill_pct = state.ring_fill_pct;
        adapter.RecordFrameTelemetry(
            state.frame_index, state.last_frametime_us, ring_fill_pct, ppm, false);
    }

    engine->UnloadCartridge();
    return true;
}

} // namespace ax::genx::snes
