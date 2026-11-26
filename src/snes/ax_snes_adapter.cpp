#include "snes_core_host.hpp"

#include "snes/ax_snes_adapter.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#if defined(GENX_SNES_HAS_ARES_CORE)
// Drop-in hook point for ares SNES core integration (e.g. ares/sfc + mia).
// Add #include headers here when GENX_SNES_ARES_SOURCE_DIR is wired in CMake.
#endif

namespace ax::genx::snes {

SnesActiveLoopPolicy BuildActiveLoopPolicy(const SnesRuntimeConfig& cfg) noexcept {
    SnesActiveLoopPolicy policy{};
    policy.preset = cfg.preset;
    policy.cpu_overclock_pct = cfg.cpu_overclock_pct;
    policy.coprocessor_overclock_pct = cfg.coprocessor_overclock_pct;
    policy.coprocessor_boost_active = cfg.thread_boost_coprocessor;
    policy.runahead_frames = cfg.runahead_frames;
    policy.effective_audio_buffer_periods =
        (cfg.alsa_buffer_periods == 0) ? 1u : cfg.alsa_buffer_periods;

    if (cfg.preset == SnesPreset::StockCycleExact) {
        policy.cpu_overclock_pct = 100;
        policy.coprocessor_overclock_pct = 100;
        policy.coprocessor_budget_factor = 1.0;
        policy.coprocessor_boost_active = false;
        policy.runahead_frames = 0;
    } else if (cfg.preset == SnesPreset::CoprocessorBoost) {
        if (policy.coprocessor_overclock_pct < 125) {
            policy.coprocessor_overclock_pct = 125;
        }
        policy.coprocessor_boost_active = true;
        policy.coprocessor_budget_factor =
            static_cast<double>(policy.coprocessor_overclock_pct) / 100.0;
        policy.runahead_frames = 0;
    } else if (cfg.preset == SnesPreset::LowLatencyInput) {
        if (policy.runahead_frames <= 0) {
            policy.runahead_frames = 1;
        }
        if (policy.effective_audio_buffer_periods > 3u) {
            policy.effective_audio_buffer_periods = 3u;
        }
        policy.coprocessor_budget_factor =
            static_cast<double>(policy.coprocessor_overclock_pct) / 100.0;
    }

    if (cfg.alsa_rate > 0u) {
        const uint64_t total_frames =
            static_cast<uint64_t>(cfg.alsa_period_frames) *
            static_cast<uint64_t>(policy.effective_audio_buffer_periods);
        policy.effective_audio_latency_us = static_cast<uint32_t>(
            (total_frames * 1'000'000ULL) / static_cast<uint64_t>(cfg.alsa_rate));
    }

    return policy;
}

namespace {

class DeterministicSnesCoreRunner final : public ISnesCoreEngine {
public:
    explicit DeterministicSnesCoreRunner(const char* backend_tag) noexcept
        : backend_tag_(backend_tag ? backend_tag : "deterministic") {}

    bool LoadCartridge(const std::string& staged_rom_path,
                       const std::string& staged_sram_path,
                       const SnesRuntimeConfig& cfg) override {
        if (staged_rom_path.empty()) {
            std::cerr << "[genx-snes-core:" << backend_tag_ << "] Empty staged ROM path.\n";
            return false;
        }
        rom_path_ = staged_rom_path;
        sram_path_ = staged_sram_path;
        loaded_ = true;

        const SnesActiveLoopPolicy initial_policy = BuildActiveLoopPolicy(cfg);
        std::cerr << "[genx-snes-core:" << backend_tag_ << "] Backend=" << cfg.engine_backend
                  << " Profile=" << cfg.hardware_profile
                  << " ROM=" << rom_path_
                  << " SRAM=" << (sram_path_.empty() ? "<none>" : sram_path_)
                  << " coproc_overclock=" << initial_policy.coprocessor_overclock_pct << "%"
                  << " (" << initial_policy.coprocessor_budget_factor << "x)"
                  << " boost=" << (initial_policy.coprocessor_boost_active ? "on" : "off")
                  << " runahead=" << initial_policy.runahead_frames
                  << " periods=" << initial_policy.effective_audio_buffer_periods
                  << " (" << initial_policy.effective_audio_latency_us << " us)\n";
        return true;
    }

    bool StepFrame(const SnesRuntimeConfig& cfg,
                   const SnesActiveLoopPolicy& policy,
                   int rate_guard_ppm,
                   SnesFramePacingState& out_state) override {
        if (!loaded_) return false;

        const auto t0 = std::chrono::steady_clock::now();
        const double ppm_scale = 1.0 - (static_cast<double>(rate_guard_ppm) / 1'000'000.0);
        const auto base_period = std::chrono::microseconds(16639);
        const auto paced_period = std::chrono::microseconds(
            std::max<long long>(1000, static_cast<long long>(base_period.count() * ppm_scale)));

        if (policy.runahead_frames > 0) {
            std::this_thread::yield();
        }

        std::this_thread::sleep_for(paced_period);
        const auto t1 = std::chrono::steady_clock::now();

        ++frame_counter_;
        const int wave = static_cast<int>((frame_counter_ % 17) - 8);
        const int low_latency_bias =
            (policy.effective_audio_buffer_periods <= 3u) ? -1 : 0;
        const int fill_pct = std::clamp(
            cfg.rate_guard_ring_target_pct + wave + low_latency_bias, 0, 100);

        out_state.frame_index = frame_counter_;
        out_state.last_frametime_us = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        out_state.ring_fill_pct = fill_pct;
        out_state.rate_guard_ppm = rate_guard_ppm;
        out_state.coprocessor_budget_factor = policy.coprocessor_budget_factor;
        out_state.coprocessor_boost_active = policy.coprocessor_boost_active;
        out_state.runahead_frames = policy.runahead_frames;
        out_state.effective_audio_buffer_periods = policy.effective_audio_buffer_periods;
        out_state.effective_audio_latency_us = policy.effective_audio_latency_us;
        return true;
    }

    void UnloadCartridge() override {
        loaded_ = false;
        rom_path_.clear();
        sram_path_.clear();
    }

private:
    const char* backend_tag_{"deterministic"};
    std::string rom_path_{};
    std::string sram_path_{};
    bool loaded_{false};
    uint64_t frame_counter_{0};
};

#if defined(GENX_SNES_HAS_ARES_CORE)
class AresSnesCoreRunner final : public ISnesCoreEngine {
public:
    bool LoadCartridge(const std::string& staged_rom_path,
                       const std::string& staged_sram_path,
                       const SnesRuntimeConfig& cfg) override {
        return fallback_.LoadCartridge(staged_rom_path, staged_sram_path, cfg);
    }

    bool StepFrame(const SnesRuntimeConfig& cfg,
                   const SnesActiveLoopPolicy& policy,
                   int rate_guard_ppm,
                   SnesFramePacingState& out_state) override {
        return fallback_.StepFrame(cfg, policy, rate_guard_ppm, out_state);
    }

    void UnloadCartridge() override {
        fallback_.UnloadCartridge();
    }

private:
    DeterministicSnesCoreRunner fallback_{"ares-stub"};
};
#endif

} // namespace

std::unique_ptr<ISnesCoreEngine> CreateSnesCoreEngine(const SnesRuntimeConfig& cfg) {
#if defined(GENX_SNES_HAS_ARES_CORE)
    if (cfg.engine_backend == "ares") {
        return std::make_unique<AresSnesCoreRunner>();
    }
#endif
    return std::make_unique<DeterministicSnesCoreRunner>("snes-pacing");
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
        const SnesActiveLoopPolicy active_policy = BuildActiveLoopPolicy(cfg);
        const int ppm = adapter.ComputeRateGuardPpm(ring_fill_pct);
        if (!engine->StepFrame(cfg, active_policy, ppm, state)) {
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
