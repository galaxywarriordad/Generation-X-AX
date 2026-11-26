#include "snes/ax_snes_adapter.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <pthread.h>
#include <sched.h>
#include <sstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace ax::genx::snes {

namespace {

constexpr double kPi = 3.14159265358979323846;

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

unsigned ParseUnsigned(const std::string& val, unsigned fallback) {
    try {
        const unsigned long parsed = std::stoul(val);
        return static_cast<unsigned>(parsed);
    } catch (...) {
        return fallback;
    }
}

int ParseInt(const std::string& val, int fallback) {
    try {
        return std::stoi(val);
    } catch (...) {
        return fallback;
    }
}

SnesPreset ParsePreset(const std::string& name) {
    if (name == "coprocessor_boost") return SnesPreset::CoprocessorBoost;
    if (name == "low_latency_input") return SnesPreset::LowLatencyInput;
    return SnesPreset::StockCycleExact;
}

AudioFilterMode ParseAudioFilterMode(const std::string& name) {
    if (name == "zsnes_warm") return AudioFilterMode::ZsnesWarm;
    if (name == "crt_nearfield") return AudioFilterMode::CrtNearfield;
    return AudioFilterMode::Default;
}

const char* ButtonName(ax::ui::retro::SnesButton btn) noexcept {
    using ax::ui::retro::SnesButton;
    switch (btn) {
        case SnesButton::Up: return "UP";
        case SnesButton::Down: return "DOWN";
        case SnesButton::Left: return "LEFT";
        case SnesButton::Right: return "RIGHT";
        case SnesButton::Start: return "START";
        case SnesButton::Select: return "SELECT";
        case SnesButton::A: return "A";
        case SnesButton::B: return "B";
        case SnesButton::X: return "X";
        case SnesButton::Y: return "Y";
        case SnesButton::L: return "L";
        case SnesButton::R: return "R";
        default: return "UNKNOWN";
    }
}

bool FsyncDirectory(const fs::path& dir_path) noexcept {
    if (dir_path.empty()) return true;
    const int dir_fd = ::open(dir_path.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd < 0) {
        return false;
    }
    const int rc = ::fsync(dir_fd);
    ::close(dir_fd);
    return rc == 0;
}

void ComputeLowPassCoeffs(double sample_rate, double cutoff_hz, double q, SnesAudioFilterState& st) noexcept {
    const double sr = std::max(8000.0, sample_rate);
    const double fc = std::clamp(cutoff_hz, 20.0, sr * 0.45);
    const double omega = 2.0 * kPi * fc / sr;
    const double sin_w = std::sin(omega);
    const double cos_w = std::cos(omega);
    const double alpha = sin_w / (2.0 * std::max(0.1, q));

    const double a0 = 1.0 + alpha;
    st.b0 = ((1.0 - cos_w) * 0.5) / a0;
    st.b1 = (1.0 - cos_w) / a0;
    st.b2 = ((1.0 - cos_w) * 0.5) / a0;
    st.a1 = (-2.0 * cos_w) / a0;
    st.a2 = (1.0 - alpha) / a0;
}

void ComputeHighShelfCoeffs(double sample_rate, double freq_hz, double gain_db, double q, SnesAudioFilterState& st) noexcept {
    const double sr = std::max(8000.0, sample_rate);
    const double fc = std::clamp(freq_hz, 100.0, sr * 0.45);
    const double a_amp = std::pow(10.0, gain_db / 40.0);
    const double omega = 2.0 * kPi * fc / sr;
    const double sin_w = std::sin(omega);
    const double cos_w = std::cos(omega);
    const double alpha = sin_w / (2.0 * std::max(0.1, q));
    const double two_sqrt_a_alpha = 2.0 * std::sqrt(a_amp) * alpha;

    const double b0 = a_amp * ((a_amp + 1.0) + (a_amp - 1.0) * cos_w + two_sqrt_a_alpha);
    const double b1 = -2.0 * a_amp * ((a_amp - 1.0) + (a_amp + 1.0) * cos_w);
    const double b2 = a_amp * ((a_amp + 1.0) + (a_amp - 1.0) * cos_w - two_sqrt_a_alpha);
    const double a0 = (a_amp + 1.0) - (a_amp - 1.0) * cos_w + two_sqrt_a_alpha;
    const double a1 = 2.0 * ((a_amp - 1.0) - (a_amp + 1.0) * cos_w);
    const double a2 = (a_amp + 1.0) - (a_amp - 1.0) * cos_w - two_sqrt_a_alpha;

    st.b0 = b0 / a0;
    st.b1 = b1 / a0;
    st.b2 = b2 / a0;
    st.a1 = a1 / a0;
    st.a2 = a2 / a0;
}

} // namespace

AxSnesAdapter::~AxSnesAdapter() {
    if (running_.load(std::memory_order_acquire)) {
        (void)ShutdownSession();
    } else {
        ReleaseDacLock();
    }
}

bool AxSnesAdapter::Initialize(const std::string& config_path, const std::string& rom_path) {
    rom_path_ = rom_path;

    std::ifstream in(config_path);
    std::string current_section;
    std::string line;
    while (in.is_open() && std::getline(in, line)) {
        const std::string clean = Trim(StripInlineComment(line));
        if (clean.empty()) continue;
        if (clean.front() == '[' && clean.back() == ']') {
            current_section = Trim(clean.substr(1, clean.size() - 2));
            continue;
        }
        const auto eq = clean.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(clean.substr(0, eq));
        const std::string val = Trim(clean.substr(eq + 1));
        const std::string unquoted = (val.size() >= 2 && val.front() == '"' && val.back() == '"')
            ? val.substr(1, val.size() - 2)
            : val;

        if (current_section == "snes") {
            if (key == "enabled") extras_.enabled = ParseBool(unquoted, extras_.enabled);
            else if (key == "engine_backend") runtime_cfg_.engine_backend = unquoted;
            else if (key == "hardware_profile") runtime_cfg_.hardware_profile = unquoted;
            else if (key == "profile_preset") runtime_cfg_.preset = ParsePreset(unquoted);
            else if (key == "audio_filter_mode") extras_.audio_filter_mode = ParseAudioFilterMode(unquoted);
            else if (key == "auto_detect_coprocessor") extras_.auto_detect_coprocessor = ParseBool(unquoted, extras_.auto_detect_coprocessor);
            else if (key == "alsa_device") runtime_cfg_.alsa_device = unquoted;
            else if (key == "alsa_format") extras_.alsa_format = unquoted;
            else if (key == "alsa_rate") runtime_cfg_.alsa_rate = ParseUnsigned(unquoted, runtime_cfg_.alsa_rate);
            else if (key == "alsa_period_frames") runtime_cfg_.alsa_period_frames = ParseUnsigned(unquoted, runtime_cfg_.alsa_period_frames);
            else if (key == "alsa_buffer_periods") runtime_cfg_.alsa_buffer_periods = ParseUnsigned(unquoted, runtime_cfg_.alsa_buffer_periods);
            else if (key == "rate_guard_max_ppm") runtime_cfg_.rate_guard_max_ppm = ParseInt(unquoted, runtime_cfg_.rate_guard_max_ppm);
            else if (key == "rate_guard_ring_target_pct") runtime_cfg_.rate_guard_ring_target_pct = ParseInt(unquoted, runtime_cfg_.rate_guard_ring_target_pct);
            else if (key == "dac_lock_path") extras_.dac_lock_path = unquoted;
            else if (key == "shm_staging_enabled") extras_.shm_staging_enabled = ParseBool(unquoted, extras_.shm_staging_enabled);
            else if (key == "controls_config_path") extras_.controls_config_path = unquoted;
            else if (key == "persistent_save_dir") extras_.persistent_save_dir = unquoted;
            else if (key == "persistent_rom_dir") extras_.persistent_rom_dir = unquoted;
            else if (key == "shm_rom_dir") extras_.shm_rom_dir = unquoted;
            else if (key == "shm_save_dir") extras_.shm_save_dir = unquoted;
            else if (key == "atomic_flush_on_exit") extras_.atomic_flush_on_exit = ParseBool(unquoted, extras_.atomic_flush_on_exit);
        }
    }

    switch (runtime_cfg_.preset) {
        case SnesPreset::CoprocessorBoost:
            runtime_cfg_.cpu_overclock_pct = 100;
            runtime_cfg_.coprocessor_overclock_pct = 125;
            runtime_cfg_.runahead_frames = 0;
            runtime_cfg_.thread_boost_coprocessor = true;
            break;
        case SnesPreset::LowLatencyInput:
            runtime_cfg_.cpu_overclock_pct = 100;
            runtime_cfg_.coprocessor_overclock_pct = 100;
            runtime_cfg_.runahead_frames = 1;
            runtime_cfg_.thread_boost_coprocessor = false;
            break;
        case SnesPreset::StockCycleExact:
        default:
            runtime_cfg_.cpu_overclock_pct = 100;
            runtime_cfg_.coprocessor_overclock_pct = 100;
            runtime_cfg_.runahead_frames = 0;
            runtime_cfg_.thread_boost_coprocessor = false;
            break;
    }

    ConfigureAudioFilter(extras_.audio_filter_mode);
    return extras_.enabled && !rom_path_.empty();
}

void AxSnesAdapter::ConfigureAudioFilter(AudioFilterMode mode) noexcept {
    extras_.audio_filter_mode = mode;
    filter_state_.z1_l = 0.0;
    filter_state_.z2_l = 0.0;
    filter_state_.z1_r = 0.0;
    filter_state_.z2_r = 0.0;
    filter_state_.crossfeed_mix = 0.0;

    const double sr = static_cast<double>(runtime_cfg_.alsa_rate);
    switch (mode) {
        case AudioFilterMode::ZsnesWarm:
            ComputeLowPassCoeffs(sr, 11500.0, 0.707, filter_state_);
            filter_state_.crossfeed_mix = 0.08;
            break;
        case AudioFilterMode::CrtNearfield:
            ComputeHighShelfCoeffs(sr, 7500.0, -2.5, 0.707, filter_state_);
            filter_state_.crossfeed_mix = 0.0;
            break;
        case AudioFilterMode::Default:
        default:
            filter_state_.b0 = 1.0;
            filter_state_.b1 = 0.0;
            filter_state_.b2 = 0.0;
            filter_state_.a1 = 0.0;
            filter_state_.a2 = 0.0;
            filter_state_.crossfeed_mix = 0.0;
            break;
    }
}

void AxSnesAdapter::ProcessAudioFilterStereo(float* interleaved_lr, std::size_t frame_count) noexcept {
    if (!interleaved_lr || frame_count == 0) return;
    if (extras_.audio_filter_mode == AudioFilterMode::Default) {
        return;
    }

    const double b0 = filter_state_.b0;
    const double b1 = filter_state_.b1;
    const double b2 = filter_state_.b2;
    const double a1 = filter_state_.a1;
    const double a2 = filter_state_.a2;
    const double xf = filter_state_.crossfeed_mix;
    double z1_l = filter_state_.z1_l;
    double z2_l = filter_state_.z2_l;
    double z1_r = filter_state_.z1_r;
    double z2_r = filter_state_.z2_r;

    for (std::size_t i = 0; i < frame_count; ++i) {
        const double in_l = static_cast<double>(interleaved_lr[i * 2 + 0]);
        const double in_r = static_cast<double>(interleaved_lr[i * 2 + 1]);

        const double src_l = (xf > 0.0) ? ((1.0 - xf) * in_l + xf * in_r) : in_l;
        const double src_r = (xf > 0.0) ? ((1.0 - xf) * in_r + xf * in_l) : in_r;

        const double out_l = b0 * src_l + z1_l;
        z1_l = b1 * src_l - a1 * out_l + z2_l;
        z2_l = b2 * src_l - a2 * out_l;

        const double out_r = b0 * src_r + z1_r;
        z1_r = b1 * src_r - a1 * out_r + z2_r;
        z2_r = b2 * src_r - a2 * out_r;

        interleaved_lr[i * 2 + 0] = static_cast<float>(std::clamp(out_l, -1.0, 1.0));
        interleaved_lr[i * 2 + 1] = static_cast<float>(std::clamp(out_r, -1.0, 1.0));
    }

    filter_state_.z1_l = z1_l;
    filter_state_.z2_l = z2_l;
    filter_state_.z1_r = z1_r;
    filter_state_.z2_r = z2_r;
}

int AxSnesAdapter::ComputeRateGuardPpm(int current_ring_fill_pct) const noexcept {
    const int err_pct = current_ring_fill_pct - runtime_cfg_.rate_guard_ring_target_pct;
    if (std::abs(err_pct) <= 5) {
        return 0;
    }
    const int raw_ppm = err_pct * 10;
    return std::clamp(raw_ppm, -runtime_cfg_.rate_guard_max_ppm, runtime_cfg_.rate_guard_max_ppm);
}

void AxSnesAdapter::RecordFrameTelemetry(std::uint64_t frame_index,
                                         std::uint32_t last_frametime_us,
                                         int ring_fill_pct,
                                         int rate_guard_ppm,
                                         bool xrun) noexcept {
    telemetry_.frame_index = frame_index;
    telemetry_.last_frametime_us = last_frametime_us;
    telemetry_.ring_fill_pct = ring_fill_pct;
    telemetry_.rate_guard_ppm = rate_guard_ppm;
    telemetry_.xrun = xrun;
}

bool AxSnesAdapter::WriteFileAtomic(const std::string& target_path, const std::string& content) const {
    const fs::path target(target_path);
    const fs::path parent = target.parent_path();
    std::error_code ec;
    if (!parent.empty()) {
        fs::create_directories(parent, ec);
    }

    const fs::path tmp_path = parent / (target.filename().string() + ".tmp." + std::to_string(::getpid()));
    const int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }

    std::size_t total_written = 0;
    while (total_written < content.size()) {
        const ssize_t n = ::write(fd, content.data() + total_written, content.size() - total_written);
        if (n <= 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            ::unlink(tmp_path.c_str());
            return false;
        }
        total_written += static_cast<std::size_t>(n);
    }

    if (::fsync(fd) != 0) {
        ::close(fd);
        ::unlink(tmp_path.c_str());
        return false;
    }
    ::close(fd);

    if (::rename(tmp_path.c_str(), target.c_str()) != 0) {
        ::unlink(tmp_path.c_str());
        return false;
    }

    (void)FsyncDirectory(parent);
    return true;
}

bool AxSnesAdapter::SaveSnesControlsAtomic(const ax::ui::retro::SnesControllerMap& map,
                                           const std::string& target_path) const {
    std::ostringstream out;
    out << "# Piccolo-AX SNES Controller Bindings\n";
    for (std::size_t i = 0; i < static_cast<std::size_t>(ax::ui::retro::SnesButton::Count); ++i) {
        const auto btn = static_cast<ax::ui::retro::SnesButton>(i);
        out << ButtonName(btn) << " = " << map.codes[i] << "\n";
    }
    return WriteFileAtomic(target_path, out.str());
}

bool AxSnesAdapter::AcquireDacLock() {
    if (dac_lock_fd_ >= 0) return true;
    const fs::path lock_path(extras_.dac_lock_path);
    std::error_code ec;
    if (!lock_path.parent_path().empty()) {
        fs::create_directories(lock_path.parent_path(), ec);
    }
    const int fd = ::open(extras_.dac_lock_path.c_str(), O_RDWR | O_CREAT, 0666);
    if (fd < 0) return false;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return false;
    }
    dac_lock_fd_ = fd;
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
    if (!extras_.shm_staging_enabled) {
        staged_rom_path_ = rom_path_;
        return true;
    }
    std::error_code ec;
    fs::create_directories(extras_.shm_rom_dir, ec);
    fs::create_directories(extras_.shm_save_dir, ec);

    const fs::path src_rom(rom_path_);
    staged_rom_path_ = (fs::path(extras_.shm_rom_dir) / src_rom.filename()).string();
    if (fs::exists(src_rom, ec)) {
        fs::copy_file(src_rom, staged_rom_path_, fs::copy_options::overwrite_existing, ec);
    }

    const fs::path sram_name = src_rom.stem().string() + ".srm";
    const fs::path persistent_sram = fs::path(extras_.persistent_save_dir) / sram_name;
    staged_sram_path_ = (fs::path(extras_.shm_save_dir) / sram_name).string();
    if (fs::exists(persistent_sram, ec)) {
        fs::copy_file(persistent_sram, staged_sram_path_, fs::copy_options::overwrite_existing, ec);
    }
    return true;
}

bool AxSnesAdapter::FlushSavesAtomicallyToStorage() {
    if (!extras_.atomic_flush_on_exit || staged_sram_path_.empty()) {
        return true;
    }
    std::error_code ec;
    if (!fs::exists(staged_sram_path_, ec)) {
        return true;
    }
    std::ifstream in(staged_sram_path_, std::ios::binary);
    if (!in.is_open()) return false;
    std::ostringstream buf;
    buf << in.rdbuf();

    const fs::path dst = fs::path(extras_.persistent_save_dir) / fs::path(staged_sram_path_).filename();
    return WriteFileAtomic(dst.string(), buf.str());
}

void AxSnesAdapter::ApplyCoreAffinities() {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (extras_.housekeeping_core >= 0 && extras_.housekeeping_core < CPU_SETSIZE) {
        CPU_SET(extras_.housekeeping_core, &set);
        (void)::pthread_setaffinity_np(::pthread_self(), sizeof(set), &set);
    }
}

bool AxSnesAdapter::StartSession() {
    ApplyCoreAffinities();
    if (!StageRomAndSavesToShm()) {
        return false;
    }
    if (!AcquireDacLock()) {
        return false;
    }
    running_.store(true, std::memory_order_release);
    return true;
}

bool AxSnesAdapter::ShutdownSession() {
    const bool flushed = FlushSavesAtomicallyToStorage();
    ReleaseDacLock();
    running_.store(false, std::memory_order_release);
    return flushed;
}

} // namespace ax::genx::snes
