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
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

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
    ReleaseDacLock();
}

bool AxSnesAdapter::Initialize(const std::string& config_path, const std::string& rom_path) {
    rom_path_ = rom_path;

    // Note: In full ax-sdk v1_36_2 builds, ax::LoadEngineConfig() parses
    // ax_engine.ini and binds hardware_profiles.json (x86_64_coffeelake_maxwell_231).
    // This standalone-safe INI reader mirrors those exact [snes] and [snes.presets.*] keys.
    std::FILE* fp = std::fopen(config_path.c_str(), "r");
    if (!fp) {
        std::cerr << "[ax-snes] Could not open config '" << config_path
                  << "', using deterministic defaults.\n";
        return !rom_path_.empty();
    }

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
            else if (key == "audio_core") cfg_.audio_core = std::atoi(val.c_str());
            else if (key == "audio_sched_fifo_priority") cfg_.audio_sched_fifo_priority = std::atoi(val.c_str());
            else if (key == "video_core") cfg_.video_core = std::atoi(val.c_str());
            else if (key == "housekeeping_core") cfg_.housekeeping_core = std::atoi(val.c_str());
            else if (key == "alsa_device") cfg_.alsa_device = val;
            else if (key == "alsa_format") cfg_.alsa_format = val;
            else if (key == "alsa_rate") cfg_.alsa_rate = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
            else if (key == "alsa_period_frames") cfg_.alsa_period_frames = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
            else if (key == "alsa_buffer_periods") cfg_.alsa_buffer_periods = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
            else if (key == "alsa_disable_resample") cfg_.alsa_disable_resample = ParseBool(val, true);
            else if (key == "dac_lock_path") cfg_.dac_lock_path = val;
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
        }
    }

    cfg_.preset = ParsePreset(active_preset_name);
    const std::string target_section = PresetSectionName(cfg_.preset);

    // Second pass for active preset section overrides
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

        if (key == "cpu_overclock_pct") cfg_.cpu_overclock_pct = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        else if (key == "coprocessor_overclock_pct") cfg_.coprocessor_overclock_pct = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        else if (key == "runahead_frames") cfg_.runahead_frames = std::atoi(val.c_str());
        else if (key == "thread_boost_coprocessor") cfg_.thread_boost_coprocessor = ParseBool(val, false);
    }

    std::fclose(fp);

    if (!cfg_.enabled) {
        std::cerr << "[ax-snes] SNES target is disabled in ax_engine.ini.\n";
        return false;
    }

    // Enforce cycle-exact integrity on stock_cycle_exact
    if (cfg_.preset == SnesPreset::StockCycleExact) {
        cfg_.cpu_overclock_pct = 100;
        cfg_.coprocessor_overclock_pct = 100;
        cfg_.runahead_frames = 0;
        cfg_.thread_boost_coprocessor = false;
    }

    return !rom_path_.empty();
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
    if (!cfg_.shm_staging_enabled) {
        staged_rom_path_ = rom_path_;
        return true;
    }

    std::error_code ec;
    fs::create_directories(cfg_.shm_rom_dir, ec);
    fs::create_directories(cfg_.shm_save_dir, ec);
    if (ec) return false;

    const fs::path rom_src(rom_path_);
    const fs::path rom_dst = fs::path(cfg_.shm_rom_dir) / rom_src.filename();
    if (!CopyFileAtomic(rom_src, rom_dst)) {
        std::cerr << "[ax-snes] Failed staging ROM to /dev/shm: " << rom_dst << "\n";
        return false;
    }
    staged_rom_path_ = rom_dst.string();

    const std::string stem = rom_src.stem().string();
    const fs::path sram_persist = fs::path(cfg_.persistent_save_dir) / (stem + ".srm");
    const fs::path sram_shm = fs::path(cfg_.shm_save_dir) / (stem + ".srm");
    staged_sram_path_ = sram_shm.string();

    if (fs::exists(sram_persist)) {
        if (!CopyFileAtomic(sram_persist, sram_shm)) {
            std::cerr << "[ax-snes] Failed staging SRAM to /dev/shm.\n";
            return false;
        }
    }
    return true;
}

bool AxSnesAdapter::FlushSavesAtomicallyToStorage() {
    if (!cfg_.shm_staging_enabled || !cfg_.atomic_flush_on_exit) return true;
    if (staged_sram_path_.empty() || !fs::exists(staged_sram_path_)) return true;

    const fs::path sram_persist = fs::path(cfg_.persistent_save_dir) / fs::path(staged_sram_path_).filename();
    return CopyFileAtomic(staged_sram_path_, sram_persist);
}

void AxSnesAdapter::ApplyCoreAffinities() {
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int c : cfg_.emu_cores) {
        if (c >= 0 && c < CPU_SETSIZE) {
            CPU_SET(c, &set);
        }
    }
    ::sched_setaffinity(0, sizeof(set), &set);
}

int AxSnesAdapter::ComputeRateGuardPpm(int current_ring_fill_pct) const noexcept {
    if (!cfg_.rate_guard_enabled) return 0;

    const int delta = current_ring_fill_pct - cfg_.rate_guard_ring_target_pct;
    if (std::abs(delta) <= cfg_.rate_guard_hysteresis_pct) {
        return 0;
    }

    const int raw_ppm = delta * 4;
    return std::clamp(raw_ppm, -cfg_.rate_guard_max_ppm, cfg_.rate_guard_max_ppm);
}

bool AxSnesAdapter::StartSession() {
    if (!StageRomAndSavesToShm()) return false;
    if (!AcquireDacLock()) return false;

    ApplyCoreAffinities();
    running_.store(true, std::memory_order_release);
    return true;
}

bool AxSnesAdapter::ShutdownSession() {
    running_.store(false, std::memory_order_release);
    const bool flushed = FlushSavesAtomicallyToStorage();
    ReleaseDacLock();
    return flushed;
}

} // namespace ax::genx::snes
