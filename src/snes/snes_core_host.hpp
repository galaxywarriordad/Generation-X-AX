#pragma once

#include <csignal>
#include <cstdint>
#include <memory>
#include <string>

namespace ax::genx::snes {

class AxSnesAdapter;

enum class SnesPreset {
    StockCycleExact,
    CoprocessorBoost,
    LowLatencyInput,
};

struct SnesRuntimeConfig {
    std::string engine_backend{"bsnes"};
    std::string hardware_profile{"x86_64_coffeelake_maxwell_231"};
    std::string alsa_device{"hw:CARD=FC3,DEV=0"};
    SnesPreset preset{SnesPreset::StockCycleExact};

    unsigned alsa_rate{48000u};
    unsigned alsa_period_frames{96u};
    unsigned alsa_buffer_periods{4u};

    int cpu_overclock_pct{100};
    int coprocessor_overclock_pct{100};
    int runahead_frames{0};
    bool thread_boost_coprocessor{false};

    int rate_guard_max_ppm{150};
    int rate_guard_ring_target_pct{50};
};

struct SnesActiveLoopPolicy {
    SnesPreset preset{SnesPreset::StockCycleExact};
    int cpu_overclock_pct{100};
    int coprocessor_overclock_pct{100};
    double coprocessor_budget_factor{1.0};
    bool coprocessor_boost_active{false};
    int runahead_frames{0};
    unsigned effective_audio_buffer_periods{4u};
    uint32_t effective_audio_latency_us{8000u};
};

struct SnesFramePacingState {
    uint64_t frame_index{0};
    uint32_t last_frametime_us{0};
    int ring_fill_pct{50};
    int rate_guard_ppm{0};
    double coprocessor_budget_factor{1.0};
    bool coprocessor_boost_active{false};
    int runahead_frames{0};
    unsigned effective_audio_buffer_periods{4u};
    uint32_t effective_audio_latency_us{8000u};
};

class ISnesCoreEngine {
public:
    virtual ~ISnesCoreEngine() = default;

    virtual bool LoadCartridge(const std::string& staged_rom_path,
                               const std::string& staged_sram_path,
                               const SnesRuntimeConfig& cfg) = 0;

    virtual bool StepFrame(const SnesRuntimeConfig& cfg,
                           const SnesActiveLoopPolicy& policy,
                           int rate_guard_ppm,
                           SnesFramePacingState& out_state) = 0;

    virtual void UnloadCartridge() = 0;
};

SnesActiveLoopPolicy BuildActiveLoopPolicy(const SnesRuntimeConfig& cfg) noexcept;
std::unique_ptr<ISnesCoreEngine> CreateSnesCoreEngine(const SnesRuntimeConfig& cfg);

bool RunSnesCoreLoop(AxSnesAdapter& adapter,
                     const std::string& staged_rom_path,
                     const std::string& staged_sram_path,
                     volatile std::sig_atomic_t* stop_flag);

} // namespace ax::genx::snes
