#pragma once

#include "ax_snes_adapter.hpp"

#include <csignal>
#include <cstdint>
#include <memory>
#include <string>

namespace ax::genx::snes {

struct SnesFramePacingState {
    uint64_t frame_index{0};
    uint32_t last_frametime_us{16639};
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

std::unique_ptr<ISnesCoreEngine> CreateSnesCoreEngine(const SnesRuntimeConfig& cfg);

bool RunSnesCoreLoop(AxSnesAdapter& adapter,
                     const std::string& staged_rom_path,
                     const std::string& staged_sram_path,
                     volatile std::sig_atomic_t* stop_flag);

} // namespace ax::genx::snes
