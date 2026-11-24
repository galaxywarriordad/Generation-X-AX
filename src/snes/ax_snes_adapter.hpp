#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace ax::genx::snes {

enum class SnesPreset {
    StockCycleExact,
    CoprocessorBoost,
    LowLatencyInput,
};

struct SnesRuntimeConfig {
    bool enabled{true};
    std::string engine_backend{"bsnes"};
    SnesPreset preset{SnesPreset::StockCycleExact};
    bool auto_detect_coprocessor{true};

    // 2:3:1 topology mapping (x86_64_coffeelake_maxwell_231)
    int audio_core{0};
    int audio_sched_fifo_priority{80};
    int video_core{1};
    std::vector<int> emu_cores{2, 3, 4};
    int housekeeping_core{5};

    // Audio & DAC lock
    std::string alsa_device{"hw:CARD=FC3,DEV=0"};
    std::string alsa_format{"S32_LE"};
    uint32_t alsa_rate{48000};
    uint32_t alsa_period_frames{96};
    uint32_t alsa_buffer_periods{4};
    bool alsa_disable_resample{true};
    std::string dac_lock_path{"/run/lock/ax-dac.lock"};

    // SPC700 clock-locked rate guard
    bool rate_guard_enabled{true};
    int rate_guard_max_ppm{150};
    int rate_guard_ring_target_pct{50};
    int rate_guard_hysteresis_pct{5};

    // /dev/shm staging & persistence
    bool shm_staging_enabled{true};
    std::string shm_rom_dir{"/dev/shm/piccolo-ax/snes/roms"};
    std::string shm_save_dir{"/dev/shm/piccolo-ax/snes/saves"};
    std::string persistent_save_dir{"/userdata/saves/snes"};
    std::string persistent_rom_dir{"/userdata/roms/snes"};
    bool atomic_flush_on_exit{true};

    // Preset overrides
    uint32_t cpu_overclock_pct{100};
    uint32_t coprocessor_overclock_pct{100};
    int runahead_frames{0};
    bool thread_boost_coprocessor{false};
};

class AxSnesAdapter {
public:
    AxSnesAdapter() = default;
    ~AxSnesAdapter();

    AxSnesAdapter(const AxSnesAdapter&) = delete;
    AxSnesAdapter& operator=(const AxSnesAdapter&) = delete;

    // Loads ax_engine.ini via ax::LoadEngineConfig(), applies active preset,
    // and resolves topology/VRAM ceilings from ax-sdk v1_36_2.
    bool Initialize(const std::string& config_path, const std::string& rom_path);

    // Stages ROM & SRAM into /dev/shm on housekeeping core (Core 5) and
    // acquires exclusive /run/lock/ax-dac.lock + Core 0 SCHED_FIFO priority 80.
    bool StartSession();

    // Computes bounded PPM rate adjustment from current audio ring fill %
    // without allocating memory or blocking on Core 0.
    int ComputeRateGuardPpm(int current_ring_fill_pct) const noexcept;

    // Flushes /dev/shm SRAM/saves atomically to /userdata/saves/snes,
    // triggers K01piccolo-sync hook if enabled, and releases DAC lock.
    bool ShutdownSession();

    const SnesRuntimeConfig& config() const noexcept { return cfg_; }
    bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

private:
    bool AcquireDacLock();
    void ReleaseDacLock();
    bool StageRomAndSavesToShm();
    bool FlushSavesAtomicallyToStorage();
    void ApplyCoreAffinities();

    SnesRuntimeConfig cfg_{};
    std::string rom_path_{};
    std::string staged_rom_path_{};
    std::string staged_sram_path_{};
    int dac_lock_fd_{-1};
    std::atomic<bool> running_{false};
};

} // namespace ax::genx::snes
