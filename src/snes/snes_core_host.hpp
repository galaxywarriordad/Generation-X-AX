#pragma once

#include "ax_snes_adapter.hpp"

#include <csignal>
#include <cstdint>
#include <memory>
#include <string>

namespace ax::genx::snes {

struct SnesFramePacingState {
    uint64_t frame_index{0};
    int ring_fill_pct{50};
    int rate_guard_ppm{0};
    bool coprocessor_boost_active{false};
    int runahead_frames{0};
};

class ISnesCoreEngine {
public:
    virtual ~ISnesCoreEngine() = default;

    virtual bool LoadCartridge(const std::string& staged_rom_path,
                               const std::string& staged_sram_path,
                               const SnesRuntimeConfig& cfg) = 0;
    virtual bool StepFrame(const SnesRuntimeConfig& cfg,
                           int rate_guard_ppm,
                           SnesFramePacingState& out_state) = 0;
    virtual void UnloadCartridge() = 0;
};

// Creates the bsnes/ares SNES core runner (or deterministic pacing fallback
// when an external bsnes/ares library symbol is not linked).
std::unique_ptr<ISnesCoreEngine> CreateSnesCoreEngine(const SnesRuntimeConfig& cfg);

// Runs the live SNES emulation loop until *stop_flag is set, pacing Core 0
// audio ring fill via adapter.ComputeRateGuardPpm() and respecting preset rules.
bool RunSnesCoreLoop(AxSnesAdapter& adapter,
                     const std::string& staged_rom_path,
                     const std::string& staged_sram_path,
                     volatile std::sig_atomic_t* stop_flag);

} // namespace ax::genx::snes
