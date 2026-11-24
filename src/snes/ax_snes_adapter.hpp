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
    std::string hardware_profile{"x86_64_coffeelake_maxwell_231"};

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

    // Video & GM200 scanout pacing
    std::string video_backend{"vulkan_1_3"};
    bool integer_scaling{true};
    std::string vsync_mode{"fifo"};
    double max_vram_budget_gib{5.00};
    double hard_vram_ceiling_gib{5.50};

    // SPC700 clock-locked rate guard
    bool rate_guard_enabled{true};
    int rate_guard_max_ppm{250};
    int rate_guard_ring_target_pct{50};
    int rate_guard_hysteresis_pct{3};

    // /dev/shm staging & persistence
    bool shm_staging_enabled{true};
    std::string shm_rom_dir{"/dev/shm/piccolo-ax/snes/roms"};
    std::string shm_save_dir{"/dev/shm/piccolo-ax/snes/saves"};
    std::string persistent_save_dir{"/userdata/saves/snes"};
    std::string persistent_rom_dir{"/userdata/roms/snes"};
    bool atomic_flush_on_exit{true};
    std::string sync_archive_hook{"K01piccolo-sync"};

    // Shared-memory telemetry (/dev/shm/ax_telemetry)
    bool telemetry_enabled{true};
    uint32_t telemetry_publish_interval_ms{16};
    bool telemetry_export_jitter_hist{true};

    // Preset overrides
    uint32_t cpu_overclock_pct{100};
    uint32_t coprocessor_overclock_pct{100};
    int runahead_frames{0};
    bool mid_scanline_strict{true};
    bool thread_boost_coprocessor{false};
};

struct SnesTelemetrySnapshot {
    uint64_t frames_stepped{0};
    int last_ring_fill_pct{50};
    int last_rate_guard_ppm{0};
    uint64_t rate_guard_adjustments{0};
    uint64_t audio_underruns{0};
    uint64_t vram_reaps{0};
    uint64_t bar1_stalls{0};
    bool sdk_telemetry_active{false};
};

class AxSnesAdapter {
public:
    AxSnesAdapter() = default;
    ~AxSnesAdapter();

    AxSnesAdapter(const AxSnesAdapter&) = delete;
    AxSnesAdapter& operator=(const AxSnesAdapter&) = delete;

    bool Initialize(const std::string& config_path,
                    const std::string& rom_path,
                    const std::string& preset_override = {},
                    const std::string& sram_override = {});

    bool StartSession();
    int ComputeRateGuardPpm(int current_ring_fill_pct) const noexcept;
    void RecordFrameTelemetry(uint64_t frame_index,
                              int ring_fill_pct,
                              int rate_guard_ppm,
                              bool audio_underrun = false) noexcept;
    SnesTelemetrySnapshot GetTelemetrySnapshot() const noexcept;
    bool ShutdownSession();

    const SnesRuntimeConfig& config() const noexcept { return cfg_; }
    bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

private:
    bool InitSdkRuntimeAndTelemetry();
    void ShutdownSdkTelemetry();
    bool AcquireDacLock();
    void ReleaseDacLock();
    bool StageRomAndSavesToShm();
    bool FlushSavesAtomicallyToStorage();
    void RunSyncArchiveHook();
    void PinThreadToCore(int core_id);
    void PinThreadToCores(const std::vector<int>& cores);
    void ApplySessionSchedulingAndAffinity();

    SnesRuntimeConfig cfg_{};
    std::string rom_path_{};
    std::string sram_override_{};
    std::string staged_rom_path_{};
    std::string staged_sram_path_{};
    int dac_lock_fd_{-1};
    std::atomic<bool> running_{false};

    std::atomic<uint64_t> frames_stepped_{0};
    std::atomic<int> last_ring_fill_pct_{50};
    std::atomic<int> last_rate_guard_ppm_{0};
    std::atomic<uint64_t> rate_guard_adjustments_{0};
    std::atomic<uint64_t> audio_underruns_{0};
    std::atomic<bool> sdk_telemetry_ready_{false};
};

} // namespace ax::genx::snes
