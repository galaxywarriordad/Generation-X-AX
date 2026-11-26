#pragma once

#include "snes/snes_core_host.hpp"
#include "ui/ax_input_binder.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ax::genx::snes {

enum class SnesPreset {
    StockCycleExact,
    CoprocessorBoost,
    LowLatencyInput,
};

enum class AudioFilterMode {
    Default,      // Bit-accurate pass-through (zero modification)
    ZsnesWarm,    // 11.5 kHz gentle low-pass roll-off + 8% stereo crossfeed
    CrtNearfield, // 7.5 kHz -2.5 dB high-shelf tame for nearfield/DAC comfort
};

struct SnesAudioFilterState {
    double b0{1.0};
    double b1{0.0};
    double b2{0.0};
    double a1{0.0};
    double a2{0.0};
    double z1_l{0.0};
    double z2_l{0.0};
    double z1_r{0.0};
    double z2_r{0.0};
    double crossfeed_mix{0.0};
};

struct SnesEngineConfig {
    bool enabled{true};
    std::string engine_backend{"bsnes"};
    SnesPreset preset{SnesPreset::StockCycleExact};
    AudioFilterMode audio_filter_mode{AudioFilterMode::Default};
    bool auto_detect_coprocessor{true};

    // 2:3:1 core topology on x86_64
    int audio_core{0};
    int audio_sched_fifo_priority{80};
    int video_core{1};
    int housekeeping_core{5};

    // Exclusive ALSA / DAC settings
    std::string alsa_device{"hw:CARD=FC3,DEV=0"};
    std::string alsa_format{"S32_LE"};
    int alsa_rate{48000};
    int alsa_period_frames{96};
    int alsa_buffer_periods{4};
    std::string dac_lock_path{"/run/lock/ax-dac.lock"};

    // Timing & rate guard
    bool rate_guard_enabled{true};
    int rate_guard_max_ppm{150};
    int rate_guard_ring_target_pct{50};
    int rate_guard_hysteresis_pct{5};

    // Preset overrides
    int cpu_overclock_pct{100};
    int coprocessor_overclock_pct{100};
    int runahead_frames{0};
    bool thread_boost_coprocessor{false};

    // Staging & atomic persistence
    bool shm_staging_enabled{true};
    std::string controls_config_path{"/userdata/system/piccolo-ax/snes_controls.cfg"};
    std::string shm_rom_dir{"/dev/shm/piccolo-ax/snes/roms"};
    std::string shm_save_dir{"/dev/shm/piccolo-ax/snes/saves"};
    std::string persistent_save_dir{"/userdata/saves/snes"};
    std::string persistent_rom_dir{"/userdata/roms/snes"};
    bool atomic_flush_on_exit{true};
};

class AxSnesAdapter {
public:
    AxSnesAdapter() = default;
    ~AxSnesAdapter();

    AxSnesAdapter(const AxSnesAdapter&) = delete;
    AxSnesAdapter& operator=(const AxSnesAdapter&) = delete;

    bool Initialize(const std::string& config_path, const std::string& rom_path);
    bool StartSession();
    bool ShutdownSession();

    // Audio DSP filter control & real-time stereo processing (run on Cores 2-4, never Core 0)
    void ConfigureAudioFilter(AudioFilterMode mode) noexcept;
    void ProcessAudioFilterStereo(float* interleaved_lr, std::size_t frame_count) noexcept;
    AudioFilterMode audio_filter_mode() const noexcept { return cfg_.audio_filter_mode; }

    // Rate-guard helper bounded by [-rate_guard_max_ppm, +rate_guard_max_ppm]
    int ComputeRateGuardPpm(int current_ring_fill_pct) const noexcept;

    // Atomic file persistence (tmp -> fsync -> rename -> parent dir fsync)
    bool WriteFileAtomic(const std::string& target_path, const std::string& content) const;
    bool SaveSnesControlsAtomic(const ax::ui::retro::SnesControllerMap& map,
                                const std::string& target_path) const;

    const SnesEngineConfig& config() const noexcept { return cfg_; }
    const std::string& staged_rom_path() const noexcept { return staged_rom_path_; }
    SnesCoreHost& core_host() noexcept { return core_host_; }
    const SnesCoreHost& core_host() const noexcept { return core_host_; }

private:
    bool AcquireDacLock();
    void ReleaseDacLock();
    bool StageRomAndSavesToShm();
    bool FlushSavesAtomicallyToStorage();
    void ApplyCoreAffinities();

    SnesEngineConfig cfg_{};
    SnesAudioFilterState filter_state_{};
    SnesCoreHost core_host_{};
    std::string rom_path_{};
    std::string staged_rom_path_{};
    std::string staged_sram_path_{};
    int dac_lock_fd_{-1};
    std::atomic<bool> running_{false};
};

} // namespace ax::genx::snes
