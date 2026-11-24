#include "ax_snes_adapter.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <sched.h>
#include <sstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(GENX_SNES_HAS_AX_SDK)
#if __has_include(<ax/config.hpp>)
#include <ax/config.hpp>
#define GENX_SNES_HAS_AX_CONFIG_HPP 1
#endif
#if __has_include(<ax/telemetry.hpp>)
#include <ax/telemetry.hpp>
#define GENX_SNES_HAS_AX_TELEMETRY_HPP 1
#endif
#if __has_include(<ax/topology.hpp>)
#include <ax/topology.hpp>
#define GENX_SNES_HAS_AX_TOPOLOGY_HPP 1
#endif
#endif

namespace fs = std::filesystem;

namespace ax::genx::snes {

namespace {

std::string Trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string StripInlineComment(const std::string& line) {
    const auto hash = line.find('#');
    const auto semi = line.find(';');
    const auto cut = std::min(hash, semi);
    return (cut == std::string::npos) ? line : line.substr(0, cut);
}

bool ParseBool(const std::string& val, bool fallback) {
    if (val == "true" || val == "1" || val == "yes" || val == "on") return true;
    if (val == "false" || val == "0" || val == "no" || val == "off") return false;
    return fallback;
}

std::vector<int> ParseCoreList(const std::string& csv, const std::vector<int>& fallback) {
    std::vector<int> result;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        const std::string token = Trim(item);
        if (token.empty()) continue;
        result.push_back(std::atoi(token.c_str()));
    }
    return result.empty() ? fallback : result;
}

SnesPreset ParsePreset(const std::string& name) {
    if (name == "coprocessor_boost") return SnesPreset::CoprocessorBoost;
    if (name == "low_latency_input") return SnesPreset::LowLatencyInput;
    return SnesPreset::StockCycleExact;
}

std::string PresetSectionName(SnesPreset p) {
    switch (p) {
        case SnesPreset::CoprocessorBoost: return "snes.presets.coprocessor_boost";
        case SnesPreset::LowLatencyInput:  return "snes.presets.low_latency_input";
        case SnesPreset::StockCycleExact:
        default:                           return "snes.presets.stock_cycle_exact";
    }
}

bool CopyFileAtomic(const fs::path& src, const fs::path& dst) {
    if (!fs::exists(src)) return true;
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    if (ec) return false;

    const fs::path tmp_dst = dst.string() + ".tmp." + std::to_string(::getpid());
    fs::copy_file(src, tmp_dst, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;

    fs::rename(tmp_dst, dst, ec);
    if (ec) {
        fs::remove(tmp_dst, ec);
        return false;
    }
    return true;
}

} // namespace

AxSnesAdapter::~AxSnesAdapter() {
    if (running_.load(std::memory_order_acquire)) {
        ShutdownSession();
    }
    ShutdownSdkTelemetry();
    ReleaseDacLock();
}

bool AxSnesAdapter::Initialize(const std::string& config_path,
                               const std::string& rom_path,
                               const std::string& preset_override,
                               const std::string& sram_override) {
    rom_path_ = rom_path;
    sram_override_ = sram_override;

#if defined(GENX_SNES_HAS_AX_TOPOLOGY_HPP)
    const auto silicon = ax::topology::ProbeHostSiliconOnce();
    (void)silicon;
#endif

#if defined(GENX_SNES_HAS_AX_CONFIG_HPP)
    ax::EngineConfig sdk_cfg{};
    if (ax::LoadEngineConfig(config_path, sdk_cfg)) {
        if (sdk_cfg.vram_shield_prune_trigger_mb > 0) {
            cfg_.max_vram_budget_gib = static_cast<double>(sdk_cfg.vram_shield_prune_trigger_mb) / 1024.0;
        }
        if (sdk_cfg.vram_shield_ceiling_mb > 0) {
            cfg_.hard_vram_ceiling_gib = static_cast<double>(sdk_cfg.vram_shield_ceiling_mb) / 1024.0;
        }
    }
#endif

    std::FILE* fp = std::fopen(config_path.c_str(), "r");
    if (!fp) {
        std::cerr << "[ax-snes] Could not open config '" << config_path
                  << "', using deterministic defaults.\n";
    } else {
        std::string current_section;
        std::string active_preset_name = "stock_cycle_exact";
        char buf[1024];

        while (std::fgets(buf, sizeof(buf), fp)) {
            std::string line = Trim(StripInlineComment(buf));
            if (line.empty()) continue;

            if (line.front() == '[' && line.back() == ']') {
                current_section = Trim(line.substr(1, line.size() - 2));
                continue;
            }

            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            const std::string key = Trim(line.substr(0, eq));
            std::string val = Trim(line.substr(eq + 1));
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                val = val.substr(1, val.size() - 2);
            }

            if (current_section == "snes") {
                if (key == "enabled") cfg_.enabled = ParseBool(val, cfg_.enabled);
                else if (key == "engine_backend") cfg_.engine_backend = val;
                else if (key == "profile_preset") active_preset_name = val;
                else if (key == "auto_detect_coprocessor") cfg_.auto_detect_coprocessor = ParseBool(val, true);
                else if (key == "hardware_profile") cfg_.hardware_profile = val;
                else if (key == "audio_core") cfg_.audio_core = std::atoi(val.c_str());
                else if (key == "audio_sched_fifo_priority") cfg_.audio_sched_fifo_priority = std::atoi(val.c_str());
                else if (key == "video_core") cfg_.video_core = std::atoi(val.c_str());
                else if (key == "emu_cores") cfg_.emu_cores = ParseCoreList(val, cfg_.emu_cores);
                else if (key == "housekeeping_core") cfg_.housekeeping_core = std::atoi(val.c_str());
                else if (key == "alsa_device") cfg_.alsa_device = val;
                else if (key == "alsa_format") cfg_.alsa_format = val;
                else if (key == "alsa_rate") cfg_.alsa_rate = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
                else if (key == "alsa_period_frames") cfg_.alsa_period_frames = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
                else if (key == "alsa_buffer_periods") cfg_.alsa_buffer_periods = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
                else if (key == "alsa_disable_resample") cfg_.alsa_disable_resample = ParseBool(val, true);
                else if (key == "dac_lock_path") cfg_.dac_lock_path = val;
                else if (key == "video_backend") cfg_.video_backend = val;
                else if (key == "integer_scaling") cfg_.integer_scaling = ParseBool(val, true);
                else if (key == "vsync_mode") cfg_.vsync_mode = val;
                else if (key == "max_vram_budget_gib") cfg_.max_vram_budget_gib = std::atof(val.c_str());
                else if (key == "hard_vram_ceiling_gib") cfg_.hard_vram_ceiling_gib = std::atof(val.c_str());
                else if (key == "rate_guard_enabled") cfg_.rate_guard_enabled = ParseBool(val, true);
                else if (key == "rate_guard_max_ppm") cfg_.rate_guard_max_ppm = std::atoi(val.c_str());
                else if (key == "rate_guard_ring_target_pct") cfg_.rate_guard_ring_target_pct = std::atoi(val.c_str());
                else if (key == "rate_guard_hysteresis_pct") cfg_.rate_guard_hysteresis_pct = std::atoi(val.c_str());
                else if (key == "shm_staging_enabled") cfg_.shm_staging_enabled = ParseBool(val, true);
                else if (key == "shm_rom_dir") cfg_.shm_rom_dir = val;
                else if (key == "shm_save_dir") cfg_.shm_save_dir = val;
                else if (key == "persistent_save_dir") cfg_.persistent_save_dir = val;
                else if (key == "persistent_rom_dir") cfg_.persistent_rom_dir = val;
                else if (key == "atomic_flush_on_exit") cfg_.atomic_flush_on_exit = ParseBool(val, true);
                else if (key == "sync_archive_hook") cfg_.sync_archive_hook = val;
                else if (key == "telemetry_enabled") cfg_.telemetry_enabled = ParseBool(val, true);
                else if (key == "telemetry_publish_interval_ms") cfg_.telemetry_publish_interval_ms = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
                else if (key == "telemetry_export_jitter_hist") cfg_.telemetry_export_jitter_hist = ParseBool(val, true);
            }
        }

        if (!preset_override.empty()) {
            active_preset_name = preset_override;
        }
        cfg_.preset = ParsePreset(active_preset_name);
        const std::string target_section = PresetSectionName(cfg_.preset);

        std::rewind(fp);
        current_section.clear();
        while (std::fgets(buf, sizeof(buf), fp)) {
            std::string line = Trim(StripInlineComment(buf));
            if (line.empty()) continue;
            if (line.front() == '[' && line.back() == ']') {
                current_section = Trim(line.substr(1, line.size() - 2));
                continue;
            }
            if (current_section != target_section) continue;

            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = Trim(line.substr(0, eq));
            std::string val = Trim(line.substr(eq + 1));
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                val = val.substr(1, val.size() - 2);
            }

            if (key == "cpu_overclock_pct") cfg_.cpu_overclock_pct = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
            else if (key == "coprocessor_overclock_pct") cfg_.coprocessor_overclock_pct = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
            else if (key == "runahead_frames") cfg_.runahead_frames = std::atoi(val.c_str());
            else if (key == "mid_scanline_strict") cfg_.mid_scanline_strict = ParseBool(val, true);
            else if (key == "thread_boost_coprocessor") cfg_.thread_boost_coprocessor = ParseBool(val, false);
        }

        std::fclose(fp);
    }

    if (!cfg_.enabled) {
        std::cerr << "[ax-snes] SNES target is disabled in ax_engine.ini.\n";
        return false;
    }

    if (cfg_.preset == SnesPreset::StockCycleExact) {
        cfg_.cpu_overclock_pct = 100;
        cfg_.coprocessor_overclock_pct = 100;
        cfg_.runahead_frames = 0;
        cfg_.mid_scanline_strict = true;
        cfg_.thread_boost_coprocessor = false;
    } else if (cfg_.preset == SnesPreset::LowLatencyInput) {
        if (cfg_.runahead_frames <= 0) {
            cfg_.runahead_frames = 1;
        }
        if (cfg_.alsa_buffer_periods > 3) {
            cfg_.alsa_buffer_periods = 3;
        }
    } else if (cfg_.preset == SnesPreset::CoprocessorBoost) {
        cfg_.thread_boost_coprocessor = true;
        if (cfg_.coprocessor_overclock_pct < 125) {
            cfg_.coprocessor_overclock_pct = 125;
        }
    }

    return !rom_path_.empty();
}

bool AxSnesAdapter::InitSdkRuntimeAndTelemetry() {
    if (!cfg_.telemetry_enabled) {
        sdk_telemetry_ready_.store(false, std::memory_order_release);
        return true;
    }

#if defined(GENX_SNES_HAS_AX_TELEMETRY_HPP)
    const bool ok = ax::InitTelemetryShm();
    sdk_telemetry_ready_.store(ok, std::memory_order_release);
    if (!ok) {
        std::cerr << "[ax-snes] ax::InitTelemetryShm() unavailable; using local atomic counters.\n";
    }
#else
    sdk_telemetry_ready_.store(false, std::memory_order_release);
#endif
    return true;
}

void AxSnesAdapter::ShutdownSdkTelemetry() {
#if defined(GENX_SNES_HAS_AX_TELEMETRY_HPP)
    if (sdk_telemetry_ready_.exchange(false, std::memory_order_acq_rel)) {
        ax::ShutdownTelemetryShm();
    }
#else
    sdk_telemetry_ready_.store(false, std::memory_order_release);
#endif
}

void AxSnesAdapter::RecordFrameTelemetry(uint64_t frame_index,
                                         uint32_t frametime_us,
                                         int ring_fill_pct,
                                         int rate_guard_ppm,
                                         bool audio_underrun) noexcept {
    frames_stepped_.store(frame_index, std::memory_order_release);
    last_ring_fill_pct_.store(ring_fill_pct, std::memory_order_release);
    last_rate_guard_ppm_.store(rate_guard_ppm, std::memory_order_release);
    if (rate_guard_ppm != 0) {
        rate_guard_adjustments_.fetch_add(1, std::memory_order_acq_rel);
    }
    if (audio_underrun) {
        audio_underruns_.fetch_add(1, std::memory_order_acq_rel);
    }

#if defined(GENX_SNES_HAS_AX_TELEMETRY_HPP)
    if (sdk_telemetry_ready_.load(std::memory_order_acquire)) {
        auto& counters = ax::GetTelemetry();
        counters.frames_presented.fetch_add(1u, std::memory_order_relaxed);
        counters.last_frametime_us.store(frametime_us, std::memory_order_relaxed);
        if (audio_underrun) {
            counters.audio_underruns.fetch_add(1u, std::memory_order_relaxed);
        }
        const uint32_t total_ring_frames = cfg_.alsa_period_frames * cfg_.alsa_buffer_periods;
        const uint32_t ring_fill_frames = (total_ring_frames * static_cast<uint32_t>(std::clamp(ring_fill_pct, 0, 100))) / 100u;
        ax::RecordAudioCallbackMetrics(frametime_us, ring_fill_frames);
    }
#else
    (void)frametime_us;
#endif
}

SnesTelemetrySnapshot AxSnesAdapter::GetTelemetrySnapshot() const noexcept {
    SnesTelemetrySnapshot snap{};
    snap.frames_stepped = frames_stepped_.load(std::memory_order_acquire);
    snap.last_ring_fill_pct = last_ring_fill_pct_.load(std::memory_order_acquire);
    snap.last_rate_guard_ppm = last_rate_guard_ppm_.load(std::memory_order_acquire);
    snap.rate_guard_adjustments = rate_guard_adjustments_.load(std::memory_order_acquire);
    snap.audio_underruns = audio_underruns_.load(std::memory_order_acquire);
    snap.sdk_telemetry_active = sdk_telemetry_ready_.load(std::memory_order_acquire);

#if defined(GENX_SNES_HAS_AX_TELEMETRY_HPP)
    if (snap.sdk_telemetry_active) {
        const ax::TelemetrySnapshot sdk_snap = ax::GetTelemetrySnapshot();
        snap.vram_reaps = sdk_snap.vram_reaps;
        snap.bar1_stalls = sdk_snap.bar1_stalls;
        snap.audio_jitter_p99_us = sdk_snap.audio_jitter_p99_us;
        if (sdk_snap.audio_underruns > snap.audio_underruns) {
            snap.audio_underruns = sdk_snap.audio_underruns;
        }
    }
#endif
    return snap;
}

bool AxSnesAdapter::AcquireDacLock() {
    if (cfg_.dac_lock_path.empty()) return true;

    std::error_code ec;
    fs::create_directories(fs::path(cfg_.dac_lock_path).parent_path(), ec);

    dac_lock_fd_ = ::open(cfg_.dac_lock_path.c_str(), O_CREAT | O_RDWR, 0660);
    if (dac_lock_fd_ < 0) {
        std::cerr << "[ax-snes] Failed to open DAC lock '" << cfg_.dac_lock_path
                  << "': " << std::strerror(errno) << "\n";
        return false;
    }

    if (::flock(dac_lock_fd_, LOCK_EX | LOCK_NB) != 0) {
        std::cerr << "[ax-snes] DAC lock busy ('" << cfg_.dac_lock_path
                  << "') — another [AX] audio client holds the DAC.\n";
        ::close(dac_lock_fd_);
        dac_lock_fd_ = -1;
        return false;
    }
    return true;
}

void AxSnesAdapter::ReleaseDacLock() {
    if (dac_lock_fd_ >= 0) {
        ::flock(dac_lock_fd_, LOCK_UN);
        ::close(dac_lock_fd_);
        dac_lock_fd_ = -1;
    }
}

bool AxSnesAdapter::StageRomAndSavesToShm() {
    PinThreadToCore(cfg_.housekeeping_core);

    const fs::path rom_src(rom_path_);
    const std::string stem = rom_src.stem().string();
    const fs::path sram_persist = fs::path(cfg_.persistent_save_dir) / (stem + ".srm");

    if (!cfg_.shm_staging_enabled) {
        staged_rom_path_ = rom_path_;
        staged_sram_path_ = sram_override_.empty() ? sram_persist.string() : sram_override_;
        return true;
    }

    std::error_code ec;
    fs::create_directories(cfg_.shm_rom_dir, ec);
    fs::create_directories(cfg_.shm_save_dir, ec);
    if (ec) return false;

    const fs::path rom_dst = fs::path(cfg_.shm_rom_dir) / rom_src.filename();
    if (rom_src != rom_dst) {
        if (!CopyFileAtomic(rom_src, rom_dst)) {
            std::cerr << "[ax-snes] Failed staging ROM to /dev/shm: " << rom_dst << "\n";
            return false;
        }
    }
    staged_rom_path_ = rom_dst.string();

    const fs::path sram_shm = sram_override_.empty()
        ? (fs::path(cfg_.shm_save_dir) / (stem + ".srm"))
        : fs::path(sram_override_);
    staged_sram_path_ = sram_shm.string();

    if (fs::exists(sram_persist) && sram_persist != sram_shm) {
        if (!CopyFileAtomic(sram_persist, sram_shm)) {
            std::cerr << "[ax-snes] Failed staging SRAM to /dev/shm.\n";
            return false;
        }
    }
    return true;
}

bool AxSnesAdapter::FlushSavesAtomicallyToStorage() {
    if (!cfg_.atomic_flush_on_exit) return true;
    if (staged_sram_path_.empty() || !fs::exists(staged_sram_path_)) return true;

    const fs::path sram_persist = fs::path(cfg_.persistent_save_dir) / fs::path(staged_sram_path_).filename();
    if (staged_sram_path_ == sram_persist.string()) return true;

    return CopyFileAtomic(staged_sram_path_, sram_persist);
}

void AxSnesAdapter::RunSyncArchiveHook() {
    if (cfg_.sync_archive_hook != "K01piccolo-sync") return;
    const char* hook_path = "/etc/init.d/K01piccolo-sync";
    if (::access(hook_path, X_OK) != 0) return;

    const pid_t pid = ::fork();
    if (pid == 0) {
        ::execl(hook_path, hook_path, "save", static_cast<char*>(nullptr));
        ::_exit(127);
    }
    if (pid > 0) {
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0) {
            if (errno != EINTR) break;
        }
    }
}

void AxSnesAdapter::PinThreadToCore(int core_id) {
    if (core_id < 0 || core_id >= CPU_SETSIZE) return;
#if defined(GENX_SNES_HAS_AX_TOPOLOGY_HPP)
    if (core_id == static_cast<int>(ax::topology::kHousekeepingCoreId)) {
        if (ax::topology::pin_to_housekeeping_core5()) return;
    } else if (ax::topology::pin_thread_to_core(static_cast<uint32_t>(core_id))) {
        return;
    }
#endif
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core_id, &set);
    ::sched_setaffinity(0, sizeof(set), &set);
}

void AxSnesAdapter::PinThreadToCores(const std::vector<int>& cores) {
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int c : cores) {
        if (c >= 0 && c < CPU_SETSIZE) {
            CPU_SET(c, &set);
        }
    }
    ::sched_setaffinity(0, sizeof(set), &set);
}

void AxSnesAdapter::ApplySessionSchedulingAndAffinity() {
#if defined(GENX_SNES_HAS_AX_TOPOLOGY_HPP)
    (void)ax::topology::PinCurrentThread(ax::topology::Domain::Audio, cfg_.audio_sched_fifo_priority);
    (void)ax::topology::PinCurrentThread(ax::topology::Domain::Jit, 0);
#endif
    PinThreadToCore(cfg_.audio_core);
    if (cfg_.audio_sched_fifo_priority > 0) {
        struct sched_param sp{};
        sp.sched_priority = cfg_.audio_sched_fifo_priority;
        ::sched_setscheduler(0, SCHED_FIFO, &sp);
    }

    struct sched_param normal_sp{};
    normal_sp.sched_priority = 0;
    ::sched_setscheduler(0, SCHED_OTHER, &normal_sp);
    PinThreadToCores(cfg_.emu_cores);
}

int AxSnesAdapter::ComputeRateGuardPpm(int current_ring_fill_pct) const noexcept {
    if (!cfg_.rate_guard_enabled) return 0;

    const int delta = current_ring_fill_pct - cfg_.rate_guard_ring_target_pct;
    if (std::abs(delta) <= cfg_.rate_guard_hysteresis_pct) {
        return 0;
    }

    const int raw_ppm = delta * 3;
    return std::clamp(raw_ppm, -cfg_.rate_guard_max_ppm, cfg_.rate_guard_max_ppm);
}

bool AxSnesAdapter::StartSession() {
    if (!InitSdkRuntimeAndTelemetry()) return false;
    if (!StageRomAndSavesToShm()) return false;
    if (!AcquireDacLock()) return false;

    ApplySessionSchedulingAndAffinity();
    running_.store(true, std::memory_order_release);
    return true;
}

bool AxSnesAdapter::ShutdownSession() {
    running_.store(false, std::memory_order_release);
    PinThreadToCore(cfg_.housekeeping_core);
    const bool flushed = FlushSavesAtomicallyToStorage();
    RunSyncArchiveHook();
    ReleaseDacLock();
    ShutdownSdkTelemetry();
    return flushed;
}

} // namespace ax::genx::snes
