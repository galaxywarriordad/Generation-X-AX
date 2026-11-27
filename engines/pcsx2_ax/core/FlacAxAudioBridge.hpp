// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Complete FLAC_AX Audio Transport, RAII DAC Lock, Multi-DAC & ESS Sabre Profiles,
// AX_DAC_VOL Config Parser, Preload Tiering, DSD/DoP Packer, PTS Drift Telemetry,
// and Dual-Mode SPU2 Sink Bridge for PCSX2_AX (Ps2Console & CohHArcade).

#pragma once

#include "IHardwareModeAdapter.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if !defined(_WIN32)
#include <alsa/asoundlib.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#else
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#if __has_include("audio/ax_stereo_ring.hpp")
#include "audio/ax_stereo_ring.hpp"
#define PCSX2_AX_HAS_EXTERNAL_FLAC_RING 1
#else
#define PCSX2_AX_HAS_EXTERNAL_FLAC_RING 0
#endif

#if __has_include("audio/ax_dac_lock.hpp")
#include "audio/ax_dac_lock.hpp"
#define PCSX2_AX_HAS_EXTERNAL_FLAC_LOCK 1
#else
#define PCSX2_AX_HAS_EXTERNAL_FLAC_LOCK 0
#endif

#if __has_include("ess_dac_hooks.hpp")
#include "ess_dac_hooks.hpp"
#define PCSX2_AX_HAS_EXTERNAL_ESS_HOOKS 1
#else
#define PCSX2_AX_HAS_EXTERNAL_ESS_HOOKS 0
#endif

namespace pcsx2_ax::audio {

// -----------------------------------------------------------------------------
// 1. Canonical Cache-Line-Aligned SPSC Stereo Ring (FLAC_AX StereoSampleRing)
// -----------------------------------------------------------------------------
#if PCSX2_AX_HAS_EXTERNAL_FLAC_RING
using ::flac_ax::audio::ConvertStereoFloatToS32;
using ::flac_ax::audio::FloatSampleToS32;
using ::flac_ax::audio::StereoFloatRing;
using ::flac_ax::audio::StereoFrameRing32;
using ::flac_ax::audio::StereoSampleRing;
#else
template <typename SampleT>
class StereoSampleRing {
public:
static constexpr std::size_t kDefaultCapacityFrames = 262144U;

explicit StereoSampleRing(std::size_t capacity_frames = kDefaultCapacityFrames)
: capacity_(NextPowerOfTwo(capacity_frames < 2U ? 2U : capacity_frames)),
  mask_(capacity_ - 1U),
  storage_(capacity_ * 2U, SampleT{}) {}

[[nodiscard]] std::size_t capacity_frames() const noexcept { return capacity_; }

void PrefaultStorage() noexcept {
volatile SampleT sink{};
const std::size_t stride = 4096U / sizeof(SampleT);
const std::size_t step = stride > 0U ? stride : 1U;
for (std::size_t i = 0U; i < storage_.size(); i += step) {
sink = storage_[i];
}
if (!storage_.empty()) {
sink = storage_.back();
}
(void)sink;
}

void Reset() noexcept {
write_idx_.store(0ULL, std::memory_order_relaxed);
read_idx_.store(0ULL, std::memory_order_relaxed);
}

[[nodiscard]] std::size_t AvailableToRead() const noexcept {
const std::uint64_t w = write_idx_.load(std::memory_order_acquire);
const std::uint64_t r = read_idx_.load(std::memory_order_relaxed);
return static_cast<std::size_t>(w - r);
}

[[nodiscard]] std::size_t AvailableToWrite() const noexcept {
const std::uint64_t r = read_idx_.load(std::memory_order_acquire);
const std::uint64_t w = write_idx_.load(std::memory_order_relaxed);
const std::size_t used = static_cast<std::size_t>(w - r);
return (used < capacity_) ? (capacity_ - used) : 0U;
}

[[nodiscard]] std::size_t PushInterleaved(const SampleT* interleaved_lr,
                                          std::size_t frames) noexcept {
if (!interleaved_lr || frames == 0U) {
return 0U;
}
const std::size_t can_write = std::min(frames, AvailableToWrite());
const std::uint64_t w = write_idx_.load(std::memory_order_relaxed);
for (std::size_t i = 0U; i < can_write; ++i) {
const std::size_t slot = static_cast<std::size_t>((w + i) & mask_) * 2U;
storage_[slot + 0U] = interleaved_lr[i * 2U + 0U];
storage_[slot + 1U] = interleaved_lr[i * 2U + 1U];
}
write_idx_.store(w + can_write, std::memory_order_release);
return can_write;
}

[[nodiscard]] std::size_t PopInterleaved(SampleT* out_lr,
                                         std::size_t max_frames) noexcept {
if (!out_lr || max_frames == 0U) {
return 0U;
}
const std::size_t can_read = std::min(max_frames, AvailableToRead());
const std::uint64_t r = read_idx_.load(std::memory_order_relaxed);
for (std::size_t i = 0U; i < can_read; ++i) {
const std::size_t slot = static_cast<std::size_t>((r + i) & mask_) * 2U;
out_lr[i * 2U + 0U] = storage_[slot + 0U];
out_lr[i * 2U + 1U] = storage_[slot + 1U];
}
read_idx_.store(r + can_read, std::memory_order_release);
return can_read;
}

private:
static constexpr std::size_t NextPowerOfTwo(std::size_t v) noexcept {
std::size_t p = 1U;
while (p < v) {
p <<= 1U;
}
return p;
}

const std::size_t capacity_;
const std::size_t mask_;
alignas(64) std::atomic<std::uint64_t> write_idx_{0ULL};
alignas(64) std::atomic<std::uint64_t> read_idx_{0ULL};
std::vector<SampleT> storage_;
};

using StereoFrameRing32 = StereoSampleRing<std::int32_t>;
using StereoFloatRing = StereoSampleRing<float>;

[[nodiscard]] inline std::int32_t FloatSampleToS32(float v) noexcept {
const float clamped = std::clamp(v, -1.0f, 1.0f);
const double scaled = static_cast<double>(clamped) * 2147483647.0;
return static_cast<std::int32_t>(std::llround(scaled));
}

inline void ConvertStereoFloatToS32(const float* in_lr,
                                    std::int32_t* out_lr,
                                    std::size_t frames) noexcept {
if (!in_lr || !out_lr || frames == 0U) {
return;
}
for (std::size_t i = 0U; i < frames * 2U; ++i) {
out_lr[i] = FloatSampleToS32(in_lr[i]);
}
}
#endif

// -----------------------------------------------------------------------------
// 2. Race-Free RAII DAC Arbitration (/run/lock/ax-dac.lock / Global\AX_DAC_LOCK)
// -----------------------------------------------------------------------------
#if PCSX2_AX_HAS_EXTERNAL_FLAC_LOCK
using ::flac_ax::audio::ScopedDacLock;
#else
class ScopedDacLock {
public:
static constexpr const char* kDefaultLinuxLockPath = "/run/lock/ax-dac.lock";

ScopedDacLock() noexcept = default;
ScopedDacLock(const ScopedDacLock&) = delete;
ScopedDacLock& operator=(const ScopedDacLock&) = delete;

ScopedDacLock(ScopedDacLock&& other) noexcept
#if !defined(_WIN32)
: fd_(other.fd_), last_errno_(other.last_errno_), locked_(other.locked_) {
other.fd_ = -1;
other.last_errno_ = 0;
other.locked_ = false;
}
#else
: mutex_(other.mutex_), last_errno_(other.last_errno_), locked_(other.locked_) {
other.mutex_ = nullptr;
other.last_errno_ = 0;
other.locked_ = false;
}
#endif

ScopedDacLock& operator=(ScopedDacLock&& other) noexcept {
if (this != &other) {
Release();
#if !defined(_WIN32)
fd_ = other.fd_;
last_errno_ = other.last_errno_;
locked_ = other.locked_;
other.fd_ = -1;
other.last_errno_ = 0;
other.locked_ = false;
#else
mutex_ = other.mutex_;
last_errno_ = other.last_errno_;
locked_ = other.locked_;
other.mutex_ = nullptr;
other.last_errno_ = 0;
other.locked_ = false;
#endif
}
return *this;
}

~ScopedDacLock() noexcept { Release(); }

bool Acquire(const char* lock_path = kDefaultLinuxLockPath,
             bool non_blocking = false) noexcept {
Release();
last_errno_ = 0;
#if !defined(_WIN32)
if (!lock_path || lock_path[0] == '\0') {
last_errno_ = EINVAL;
return false;
}
int fd = -1;
while (true) {
fd = ::open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0666);
if (fd >= 0) {
break;
}
if (errno != EINTR) {
last_errno_ = errno;
return false;
}
}
const int op = non_blocking ? (LOCK_EX | LOCK_NB) : LOCK_EX;
while (::flock(fd, op) != 0) {
if (errno == EINTR) {
continue;
}
last_errno_ = errno;
while (::close(fd) != 0 && errno == EINTR) {
}
return false;
}
fd_ = fd;
locked_ = true;
return true;
#else
(void)lock_path;
HANDLE h = ::CreateMutexW(nullptr, FALSE, L"Global\\AX_DAC_LOCK");
if (!h) {
last_errno_ = static_cast<int>(::GetLastError());
return false;
}
const DWORD timeout_ms = non_blocking ? 0UL : INFINITE;
const DWORD wait_res = ::WaitForSingleObject(h, timeout_ms);
if (wait_res == WAIT_OBJECT_0 || wait_res == WAIT_ABANDONED) {
mutex_ = h;
locked_ = true;
return true;
}
last_errno_ = (wait_res == WAIT_TIMEOUT)
? static_cast<int>(ERROR_TIMEOUT)
: static_cast<int>(::GetLastError());
::CloseHandle(h);
return false;
#endif
}

void Release() noexcept {
#if !defined(_WIN32)
if (fd_ >= 0) {
if (locked_) {
while (::flock(fd_, LOCK_UN) != 0 && errno == EINTR) {
}
locked_ = false;
}
while (::close(fd_) != 0 && errno == EINTR) {
}
fd_ = -1;
}
#else
if (mutex_ != nullptr) {
if (locked_) {
::ReleaseMutex(mutex_);
locked_ = false;
}
::CloseHandle(mutex_);
mutex_ = nullptr;
}
#endif
}

[[nodiscard]] bool is_locked() const noexcept { return locked_; }
[[nodiscard]] int last_error() const noexcept { return last_errno_; }

private:
#if !defined(_WIN32)
int fd_{-1};
#else
HANDLE mutex_{nullptr};
#endif
int last_errno_{0};
bool locked_{false};
};
#endif

// -----------------------------------------------------------------------------
// 3. Multi-DAC & ESS Sabre Profiles + DoP Packer + Hardware Mixer
// -----------------------------------------------------------------------------
enum class EssChipFamily : std::uint8_t {
Es9281AcPro = 0, // HiBy FC3 (VID 0x32BB / 0x2FC6, 2.0 Vrms or early 1.3 Vrms)
Es9281Pro,       // ESS Sabre ES9281PRO
FiioKa11,        // FiiO KA11 (VID 0x2972, --volume 0.72)
TrnBlackPearl,   // TRN Black Pearl (VID 0x3302 / 0x1A86, --volume 0.80, 4.0 Vrms balanced)
MoondropDawnPro2,// Moondrop Dawn Pro 2 (--volume 0.80)
AppleUsbCA2155   // Apple USB-C UK A2155 (VID 0x05AC, --volume 1.00)
};

enum class AudioLoadTarget : std::uint8_t {
Speakers = 0,     // 2.0 Vrms HiBy FC3 -> Nobsound Q10 / NS-01G Pro (0.80 default)
SpeakersFc3_1v3,  // 1.3 Vrms early HiBy FC3 revision -> Nobsound Q10 / NS-01G Pro (0.95 default)
GradoSr325x,      // 38 Ohm open-back reference headphones (0.55 default)
HeadphonesHighZ,  // ~120 Ohm studio open-back e.g. Sennheiser HD 560S (0.70 default)
HeadphonesClosed, // ~23-48 Ohm closed-back e.g. HD 569 / DJ 300 Pro X (0.50 default)
LowOhmIem         // 16-32 Ohm sensitive IEMs / HD400S (0.22 default)
};

enum class DsdRateKind : std::uint8_t {
Unsupported = 0,
Dsd64,  // 2,822,400 Hz -> 176,400 Hz DoP
Dsd128  // 5,644,800 Hz -> 352,800 Hz DoP
};

[[nodiscard]] inline constexpr const char* ToString(DsdRateKind kind) noexcept {
switch (kind) {
case DsdRateKind::Dsd64:
return "DSD64(176.4kHz DoP)";
case DsdRateKind::Dsd128:
return "DSD128(352.8kHz DoP)";
case DsdRateKind::Unsupported:
default:
return "PCM/None";
}
}

struct EssChipProfile {
EssChipFamily family{EssChipFamily::Es9281AcPro};
const char* chip_name{"ES9281AC Pro"};
const char* name{"ES9281AC Pro"};
const char* default_alsa_device{"hw:CARD=FC3,DEV=0"};
const char* default_mixer_card{"FC3"};
unsigned int min_rate_hz{44100U};
unsigned int max_rate_hz{384000U};
unsigned int default_period_frames{96U};
unsigned int fallback_period_frames{128U};
double output_vrms{2.0};
double default_device_volume{0.80};
double recommended_volume_speakers{0.80};
double recommended_volume_speakers_1v3{0.95};
double recommended_volume_grado_sr325x{0.55};
double recommended_volume_headphones_high_z{0.70};
double recommended_volume_headphones_closed{0.50};
double recommended_volume_low_ohm_iem{0.22};
bool supports_dsd64{true};
bool supports_dsd128{true};
bool supports_dsd256{false};
#if !defined(_WIN32)
snd_pcm_format_t preferred_format{SND_PCM_FORMAT_S32_LE};
#endif
};

inline constexpr EssChipProfile MakeEssProfile(EssChipFamily family,
                                               double output_vrms = 2.0) noexcept {
switch (family) {
case EssChipFamily::Es9281Pro:
return EssChipProfile{
EssChipFamily::Es9281Pro, "ES9281PRO", "ES9281PRO",
"hw:CARD=FC3,DEV=0", "FC3", 44100U, 384000U, 96U, 128U,
output_vrms, 0.80, 0.80, 0.95, 0.55, 0.70, 0.50, 0.22,
true, true, false
#if !defined(_WIN32)
, SND_PCM_FORMAT_S32_LE
#endif
};
case EssChipFamily::FiioKa11:
return EssChipProfile{
EssChipFamily::FiioKa11, "FiiO KA11 (0x2972)", "FiiO KA11",
"hw:CARD=KA11,DEV=0", "KA11", 44100U, 384000U, 96U, 128U,
2.45, 0.72, 0.72, 0.72, 0.50, 0.65, 0.45, 0.20,
true, true, false
#if !defined(_WIN32)
, SND_PCM_FORMAT_S32_LE
#endif
};
case EssChipFamily::TrnBlackPearl:
return EssChipProfile{
EssChipFamily::TrnBlackPearl, "TRN Black Pearl (0x3302/0x1A86)", "TRN Black Pearl",
"hw:CARD=BLACKPEARL,DEV=0", "BLACKPEARL", 44100U, 384000U, 96U, 128U,
4.0, 0.80, 0.80, 0.80, 0.55, 0.70, 0.50, 0.22,
true, true, false
#if !defined(_WIN32)
, SND_PCM_FORMAT_S32_LE
#endif
};
case EssChipFamily::MoondropDawnPro2:
return EssChipProfile{
EssChipFamily::MoondropDawnPro2, "Moondrop Dawn Pro 2", "Moondrop Dawn Pro 2",
"hw:CARD=DawnPro2,DEV=0", "DawnPro2", 44100U, 384000U, 96U, 128U,
4.0, 0.80, 0.80, 0.80, 0.55, 0.70, 0.50, 0.22,
true, true, false
#if !defined(_WIN32)
, SND_PCM_FORMAT_S32_LE
#endif
};
case EssChipFamily::AppleUsbCA2155:
return EssChipProfile{
EssChipFamily::AppleUsbCA2155, "Apple USB-C UK A2155 (0x05AC)", "Apple A2155",
"hw:CARD=USB,DEV=0", "USB", 44100U, 48000U, 96U, 128U,
0.5, 1.00, 1.00, 1.00, 0.75, 0.85, 0.65, 0.35,
false, false, false
#if !defined(_WIN32)
, SND_PCM_FORMAT_S32_LE
#endif
};
case EssChipFamily::Es9281AcPro:
default:
const double dev_vol = (output_vrms <= 1.35) ? 0.95 : 0.80;
return EssChipProfile{
EssChipFamily::Es9281AcPro,
(output_vrms <= 1.35) ? "ES9281AC Pro (FC3 1.3Vrms)" : "ES9281AC Pro (FC3 2.0Vrms)",
"ES9281AC Pro",
"hw:CARD=FC3,DEV=0", "FC3", 44100U, 384000U, 96U, 128U,
output_vrms, dev_vol, 0.80, 0.95, 0.55, 0.70, 0.50, 0.22,
true, true, false
#if !defined(_WIN32)
, SND_PCM_FORMAT_S32_LE
#endif
};
}
}

inline std::optional<EssChipProfile> ResolveEssChipHook(std::string_view spec) noexcept {
std::string lower;
lower.reserve(spec.size());
for (unsigned char ch : spec) {
lower.push_back(static_cast<char>(std::tolower(ch)));
}
if (lower.empty() || lower == "es9281ac_pro" || lower == "es9281ac-pro" ||
    lower == "es9281acpro" || lower == "fc3" || lower == "hiby_fc3" ||
    lower == "fc3_2v0" || lower == "hiby_fc3_2v0" || lower == "32bb:2fc6") {
return MakeEssProfile(EssChipFamily::Es9281AcPro, 2.0);
}
if (lower == "fc3_1v3" || lower == "hiby_fc3_1v3" || lower == "fc3-1.3v") {
return MakeEssProfile(EssChipFamily::Es9281AcPro, 1.3);
}
if (lower == "es9281pro" || lower == "es9281_pro" || lower == "es9281-pro") {
return MakeEssProfile(EssChipFamily::Es9281Pro, 2.0);
}
if (lower == "ka11" || lower == "fiio_ka11" || lower == "fiio-ka11" || lower == "2972") {
return MakeEssProfile(EssChipFamily::FiioKa11, 2.45);
}
if (lower == "blackpearl" || lower == "black_pearl" || lower == "trn_black_pearl" ||
    lower == "3302" || lower == "1a86") {
return MakeEssProfile(EssChipFamily::TrnBlackPearl, 4.0);
}
if (lower == "dawnpro2" || lower == "dawn_pro_2" || lower == "moondrop_dawn_pro_2") {
return MakeEssProfile(EssChipFamily::MoondropDawnPro2, 4.0);
}
if (lower == "a2155" || lower == "apple_a2155" || lower == "apple-a2155" ||
    lower == "apple_usb_c" || lower == "05ac") {
return MakeEssProfile(EssChipFamily::AppleUsbCA2155, 0.5);
}
return std::nullopt;
}

inline constexpr double RecommendedVolumeForTarget(const EssChipProfile& chip,
                                                   AudioLoadTarget target) noexcept {
switch (target) {
case AudioLoadTarget::SpeakersFc3_1v3:
return chip.recommended_volume_speakers_1v3;
case AudioLoadTarget::GradoSr325x:
return chip.recommended_volume_grado_sr325x;
case AudioLoadTarget::HeadphonesHighZ:
return chip.recommended_volume_headphones_high_z;
case AudioLoadTarget::HeadphonesClosed:
return chip.recommended_volume_headphones_closed;
case AudioLoadTarget::LowOhmIem:
return chip.recommended_volume_low_ohm_iem;
case AudioLoadTarget::Speakers:
default:
if (chip.family == EssChipFamily::Es9281AcPro || chip.family == EssChipFamily::Es9281Pro) {
return (chip.output_vrms <= 1.35)
? chip.recommended_volume_speakers_1v3
: chip.recommended_volume_speakers;
}
return chip.default_device_volume;
}
}

inline constexpr unsigned int NegotiateEssRate(const EssChipProfile& chip,
                                               unsigned int requested_rate_hz) noexcept {
if (requested_rate_hz == 0U) {
return 48000U;
}
return std::clamp(requested_rate_hz, chip.min_rate_hz, chip.max_rate_hz);
}

inline constexpr unsigned int RecommendedBufferPeriods(const EssChipProfile& chip,
                                                       unsigned int rate_hz,
                                                       unsigned int custom_periods = 0U) noexcept {
(void)chip;
if (custom_periods > 0U) {
return std::clamp(custom_periods, 2U, 32U);
}
return (rate_hz >= 176400U) ? 8U : 4U;
}

inline std::optional<double> ParseAxDacVolumeConfig(std::string_view config_path) noexcept {
std::ifstream ifs{std::string(config_path)};
if (!ifs.is_open()) {
return std::nullopt;
}
std::string line;
while (std::getline(ifs, line)) {
if (const auto hash_pos = line.find('#'); hash_pos != std::string::npos) {
line.erase(hash_pos);
}
const auto eq_pos = line.find('=');
if (eq_pos == std::string::npos) {
continue;
}
std::string_view key{line.data(), eq_pos};
while (!key.empty() && std::isspace(static_cast<unsigned char>(key.front()))) {
key.remove_prefix(1);
}
while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) {
key.remove_suffix(1);
}
if (key != "AX_DAC_VOL") {
continue;
}
std::string_view val{line.data() + eq_pos + 1};
while (!val.empty() && std::isspace(static_cast<unsigned char>(val.front()))) {
val.remove_prefix(1);
}
while (!val.empty() && std::isspace(static_cast<unsigned char>(val.back()))) {
val.remove_suffix(1);
}
if (val.empty()) {
return std::nullopt;
}
std::string val_str{val};
char* end_ptr = nullptr;
errno = 0;
const double parsed = std::strtod(val_str.c_str(), &end_ptr);
if (errno == 0 && end_ptr != val_str.c_str() && std::isfinite(parsed)) {
return std::clamp(parsed, 0.0, 1.0);
}
}
return std::nullopt;
}

class DopPacker {
public:
static constexpr std::uint8_t kMarkerA = 0x05U;
static constexpr std::uint8_t kMarkerB = 0xFAU;

void Reset() noexcept {
toggle_ = false;
has_pending_ = false;
pending_byte_ = 0U;
has_pending_stereo_ = false;
pending_l_ = 0U;
pending_r_ = 0U;
}

void PackStereoFrame(std::uint16_t dsd_l_msb_first,
                     std::uint16_t dsd_r_msb_first,
                     std::int32_t& out_l,
                     std::int32_t& out_r) noexcept {
const std::uint8_t marker = NextMarker();
const auto l0 = static_cast<std::uint8_t>((dsd_l_msb_first >> 8) & 0xFFU);
const auto l1 = static_cast<std::uint8_t>(dsd_l_msb_first & 0xFFU);
const auto r0 = static_cast<std::uint8_t>((dsd_r_msb_first >> 8) & 0xFFU);
const auto r1 = static_cast<std::uint8_t>(dsd_r_msb_first & 0xFFU);
out_l = static_cast<std::int32_t>(Pack24WithMarker(marker, l0, l1));
out_r = static_cast<std::int32_t>(Pack24WithMarker(marker, r0, r1));
}

std::size_t PackBytes(const std::uint8_t* src_bytes,
                      std::size_t src_count,
                      std::uint32_t* dst_words,
                      std::size_t dst_capacity) noexcept {
if (!src_bytes || !dst_words || dst_capacity == 0U) {
return 0U;
}
std::size_t written = 0U;
std::size_t i = 0U;
if (has_pending_ && i < src_count && written < dst_capacity) {
dst_words[written++] = MakeDopWord(pending_byte_, src_bytes[i++]);
has_pending_ = false;
}
while ((i + 1U) < src_count && written < dst_capacity) {
dst_words[written++] = MakeDopWord(src_bytes[i], src_bytes[i + 1U]);
i += 2U;
}
if (i < src_count && !has_pending_) {
pending_byte_ = src_bytes[i];
has_pending_ = true;
}
return written;
}

std::size_t PackStereoInterleaved(const std::uint8_t* src_lr_bytes,
                                  std::size_t stereo_byte_pairs,
                                  std::int32_t* dst_lr_words,
                                  std::size_t dst_stereo_frame_capacity) noexcept {
if (!src_lr_bytes || !dst_lr_words || dst_stereo_frame_capacity == 0U) {
return 0U;
}
std::size_t frames_out = 0U;
std::size_t pair_idx = 0U;
if (has_pending_stereo_ && pair_idx < stereo_byte_pairs && frames_out < dst_stereo_frame_capacity) {
const std::uint8_t l_b0 = pending_l_;
const std::uint8_t r_b0 = pending_r_;
const std::uint8_t l_b1 = src_lr_bytes[pair_idx * 2U];
const std::uint8_t r_b1 = src_lr_bytes[pair_idx * 2U + 1U];
const std::uint8_t marker = NextMarker();
dst_lr_words[frames_out * 2U] = static_cast<std::int32_t>(Pack24WithMarker(marker, l_b0, l_b1));
dst_lr_words[frames_out * 2U + 1U] = static_cast<std::int32_t>(Pack24WithMarker(marker, r_b0, r_b1));
++frames_out;
++pair_idx;
has_pending_stereo_ = false;
}
while ((pair_idx + 1U) < stereo_byte_pairs && frames_out < dst_stereo_frame_capacity) {
const std::uint8_t l_b0 = src_lr_bytes[pair_idx * 2U];
const std::uint8_t r_b0 = src_lr_bytes[pair_idx * 2U + 1U];
const std::uint8_t l_b1 = src_lr_bytes[(pair_idx + 1U) * 2U];
const std::uint8_t r_b1 = src_lr_bytes[(pair_idx + 1U) * 2U + 1U];
const std::uint8_t marker = NextMarker();
dst_lr_words[frames_out * 2U] = static_cast<std::int32_t>(Pack24WithMarker(marker, l_b0, l_b1));
dst_lr_words[frames_out * 2U + 1U] = static_cast<std::int32_t>(Pack24WithMarker(marker, r_b0, r_b1));
++frames_out;
pair_idx += 2U;
}
if (pair_idx < stereo_byte_pairs && !has_pending_stereo_) {
pending_l_ = src_lr_bytes[pair_idx * 2U];
pending_r_ = src_lr_bytes[pair_idx * 2U + 1U];
has_pending_stereo_ = true;
}
return frames_out;
}

private:
static std::uint32_t Pack24WithMarker(std::uint8_t marker,
                                      std::uint8_t first_dsd_byte,
                                      std::uint8_t second_dsd_byte) noexcept {
return (static_cast<std::uint32_t>(marker) << 24) |
       (static_cast<std::uint32_t>(first_dsd_byte) << 16) |
       (static_cast<std::uint32_t>(second_dsd_byte) << 8);
}

std::uint8_t NextMarker() noexcept {
const std::uint8_t m = toggle_ ? kMarkerB : kMarkerA;
toggle_ = !toggle_;
return m;
}

std::uint32_t MakeDopWord(std::uint8_t b0, std::uint8_t b1) noexcept {
return Pack24WithMarker(NextMarker(), b0, b1);
}

bool toggle_{false};
bool has_pending_{false};
std::uint8_t pending_byte_{0U};
bool has_pending_stereo_{false};
std::uint8_t pending_l_{0U};
std::uint8_t pending_r_{0U};
};

class EssHardwareMixer {
public:
EssHardwareMixer() = default;
~EssHardwareMixer() { Close(); }

EssHardwareMixer(const EssHardwareMixer&) = delete;
EssHardwareMixer& operator=(const EssHardwareMixer&) = delete;

bool Open(const char* card_name = "FC3") noexcept {
#if !defined(_WIN32)
Close();
if (!card_name || card_name[0] == '\0') {
return false;
}
std::string dev = "hw:";
dev += card_name;

snd_mixer_t* mixer = nullptr;
if (snd_mixer_open(&mixer, 0) < 0) {
return false;
}
if (snd_mixer_attach(mixer, dev.c_str()) < 0) {
snd_mixer_close(mixer);
return false;
}
if (snd_mixer_selem_register(mixer, nullptr, nullptr) < 0) {
snd_mixer_close(mixer);
return false;
}
if (snd_mixer_load(mixer) < 0) {
snd_mixer_close(mixer);
return false;
}

snd_mixer_selem_id_t* sid = nullptr;
snd_mixer_selem_id_alloca(&sid);

static constexpr std::array<const char*, 4> kCandidateControls = {
"PCM", "Playback", "Master", "DAC"
};

for (const char* name : kCandidateControls) {
snd_mixer_selem_id_set_index(sid, 0);
snd_mixer_selem_id_set_name(sid, name);
snd_mixer_elem_t* elem = snd_mixer_find_selem(mixer, sid);
if (elem && snd_mixer_selem_has_playback_volume(elem)) {
mixer_ = mixer;
elem_ = elem;
snd_mixer_selem_get_playback_volume_range(elem_, &min_vol_, &max_vol_);
return true;
}
}

for (snd_mixer_elem_t* elem = snd_mixer_first_elem(mixer);
     elem != nullptr;
     elem = snd_mixer_elem_next(elem)) {
if (snd_mixer_selem_has_playback_volume(elem)) {
mixer_ = mixer;
elem_ = elem;
snd_mixer_selem_get_playback_volume_range(elem_, &min_vol_, &max_vol_);
return true;
}
}

snd_mixer_close(mixer);
return false;
#else
(void)card_name;
return true;
#endif
}

bool SetNormalizedVolume(double normalized_0_to_1) noexcept {
#if !defined(_WIN32)
if (!mixer_ || !elem_ || max_vol_ <= min_vol_) {
return false;
}
const double clamped = std::clamp(normalized_0_to_1, 0.0, 1.0);
const double span = static_cast<double>(max_vol_ - min_vol_);
const long target = min_vol_ + static_cast<long>(std::llround(clamped * span));
return snd_mixer_selem_set_playback_volume_all(elem_, target) >= 0;
#else
(void)normalized_0_to_1;
return true;
#endif
}

void Close() noexcept {
#if !defined(_WIN32)
if (mixer_) {
snd_mixer_close(mixer_);
mixer_ = nullptr;
}
elem_ = nullptr;
min_vol_ = 0;
max_vol_ = 0;
#endif
}

private:
#if !defined(_WIN32)
snd_mixer_t* mixer_{nullptr};
snd_mixer_elem_t* elem_{nullptr};
long min_vol_{0};
long max_vol_{0};
#endif
};

// -----------------------------------------------------------------------------
// 4. Adaptive RAM Preload Policy (WholeAlbum / Window3Files / SingleFile)
// -----------------------------------------------------------------------------
enum class PreloadMode : std::uint8_t {
SingleFile = 0,
Window3Files,
WholeAlbum
};

[[nodiscard]] inline constexpr const char* ToString(PreloadMode mode) noexcept {
switch (mode) {
case PreloadMode::WholeAlbum:
return "WholeAlbum";
case PreloadMode::Window3Files:
return "Window3Files";
case PreloadMode::SingleFile:
default:
return "SingleFile";
}
}

struct PreloadPolicy {
PreloadMode mode{PreloadMode::SingleFile};
std::size_t max_window_files{1};
std::size_t max_total_bytes{0};
std::uint64_t detected_mem_total_bytes{0};
std::uint64_t detected_mem_available_bytes{0};
};

inline void ReadLinuxMemInfo(std::uint64_t& mem_total_bytes,
                             std::uint64_t& mem_available_bytes) noexcept {
mem_total_bytes = 0;
mem_available_bytes = 0;
#if !defined(_WIN32)
std::ifstream meminfo("/proc/meminfo");
if (!meminfo.is_open()) {
return;
}
std::string key;
std::uint64_t kb = 0;
std::string unit;
while (meminfo >> key >> kb) {
if (key == "MemTotal:") {
mem_total_bytes = kb * 1024ULL;
} else if (key == "MemAvailable:") {
mem_available_bytes = kb * 1024ULL;
}
std::getline(meminfo, unit);
if (mem_total_bytes > 0 && mem_available_bytes > 0) {
break;
}
}
#endif
}

inline PreloadPolicy EvaluatePreloadPolicy(std::uint64_t mem_total_bytes,
                                           std::uint64_t mem_available_bytes = 0) noexcept {
constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;
constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kWholeAlbumThreshold = 16ULL * kGiB;
constexpr std::uint64_t kEightGbTierMin = (6ULL * kGiB) + (512ULL * kMiB);
constexpr std::uint64_t kEmbeddedHandheldMax = (1ULL * kGiB) + (512ULL * kMiB);

PreloadPolicy policy{};
policy.detected_mem_total_bytes = mem_total_bytes;
policy.detected_mem_available_bytes = mem_available_bytes;

if (mem_total_bytes >= kWholeAlbumThreshold) {
policy.mode = PreloadMode::WholeAlbum;
policy.max_window_files = (std::numeric_limits<std::size_t>::max)();
policy.max_total_bytes = static_cast<std::size_t>(8ULL * kGiB);
} else if (mem_total_bytes >= kEightGbTierMin) {
policy.mode = PreloadMode::Window3Files;
policy.max_window_files = 3;
if (mem_available_bytes > 0 && mem_available_bytes < (2ULL * kGiB)) {
policy.max_window_files = 1;
policy.mode = PreloadMode::SingleFile;
}
policy.max_total_bytes = static_cast<std::size_t>(2ULL * kGiB);
} else if (mem_total_bytes > 0 && mem_total_bytes <= kEmbeddedHandheldMax) {
policy.mode = PreloadMode::SingleFile;
policy.max_window_files = 1;
policy.max_total_bytes = static_cast<std::size_t>(64ULL * kMiB);
} else {
policy.mode = PreloadMode::SingleFile;
policy.max_window_files = 1;
policy.max_total_bytes = static_cast<std::size_t>(512ULL * kMiB);
}
return policy;
}

inline PreloadPolicy DetectHostPreloadPolicy() noexcept {
std::uint64_t total = 0;
std::uint64_t avail = 0;
#if defined(_WIN32)
MEMORYSTATUSEX statex{};
statex.dwLength = sizeof(statex);
if (::GlobalMemoryStatusEx(&statex)) {
total = static_cast<std::uint64_t>(statex.ullTotalPhys);
avail = static_cast<std::uint64_t>(statex.ullAvailPhys);
}
#else
ReadLinuxMemInfo(total, avail);
#endif
return EvaluatePreloadPolicy(total, avail);
}

// -----------------------------------------------------------------------------
// 5. Dual-Mode SPU2 -> FLAC_AX Audio Sink Bridge (Ps2Console & CohHArcade)
// -----------------------------------------------------------------------------
struct ModeAudioProfile {
pcsx2_ax::core::HardwareMode mode{pcsx2_ax::core::HardwareMode::Ps2Console};
std::uint32_t sample_rate_hz{48000U};
std::uint32_t period_frames{96U};          // 2.00 ms @ 48 kHz (FLAC_AX Core 0 cadence)
std::uint32_t buffer_periods{4U};          // 8.00 ms PS2 console / 6.00 ms COH-H arcade
std::uint64_t producer_core_mask{0x10ULL}; // Core 4: R3000A IOP + SIF0/1 + SPU2 (2 MB RAM)
std::uint64_t drain_core_mask{0x01ULL};    // Core 0: SCHED_FIFO 80 S32_LE DAC writer
bool bit_exact_zero_dbfs{true};            // 0 dBFS PCM stream; hardware DAC volume control
bool bypass_software_resampler{true};      // snd_pcm_hw_params_set_rate_resample(0)
const char* alsa_device{"hw:CARD=FC3,DEV=0"};
};

[[nodiscard]] inline constexpr ModeAudioProfile MakeModeAudioProfile(
pcsx2_ax::core::HardwareMode mode,
const EssChipProfile& chip = MakeEssProfile(EssChipFamily::Es9281AcPro, 2.0)) noexcept {
if (mode == pcsx2_ax::core::HardwareMode::CohHArcade) {
return ModeAudioProfile{
.mode                      = pcsx2_ax::core::HardwareMode::CohHArcade,
.sample_rate_hz            = NegotiateEssRate(chip, 48000U),
.period_frames             = chip.default_period_frames,
.buffer_periods            = 3U, // 6.00 ms arcade cabinet transient budget
.producer_core_mask        = 0x10ULL,
.drain_core_mask           = 0x01ULL,
.bit_exact_zero_dbfs       = true,
.bypass_software_resampler = true,
.alsa_device               = chip.default_alsa_device,
};
}
return ModeAudioProfile{
.mode                      = pcsx2_ax::core::HardwareMode::Ps2Console,
.sample_rate_hz            = NegotiateEssRate(chip, 48000U),
.period_frames             = chip.default_period_frames,
.buffer_periods            = RecommendedBufferPeriods(chip, 48000U, 4U), // 8.00 ms
.producer_core_mask        = 0x10ULL,
.drain_core_mask           = 0x01ULL,
.bit_exact_zero_dbfs       = true,
.bypass_software_resampler = true,
.alsa_device               = chip.default_alsa_device,
};
}

struct FlacAxAudioTelemetry {
std::uint64_t active_epoch{1ULL};
std::uint64_t frames_pushed{0ULL};
std::uint64_t frames_drained{0ULL};
std::uint64_t ring_overruns{0ULL};
std::uint64_t epoch_flushes{0ULL};
std::int64_t av_delta_us{0LL};
std::size_t buffered_frames{0U};
std::size_t capacity_frames{0U};
double effective_volume{0.80};
DsdRateKind dsd_rate{DsdRateKind::Unsupported};
PreloadMode preload_mode{PreloadMode::SingleFile};
bool dac_lock_held{false};
bool initialized{false};
};

class FlacAxAudioBridge {
public:
static constexpr std::size_t kSpu2RingCapacityFrames = 16384U; // ~341 ms @ 48 kHz, power of two

FlacAxAudioBridge()
: s32_ring_(kSpu2RingCapacityFrames),
  chip_profile_(MakeEssProfile(EssChipFamily::Es9281AcPro, 2.0)),
  mode_profile_(MakeModeAudioProfile(pcsx2_ax::core::HardwareMode::Ps2Console, chip_profile_)),
  preload_policy_(DetectHostPreloadPolicy()) {}

~FlacAxAudioBridge() noexcept { shutdown(); }

FlacAxAudioBridge(const FlacAxAudioBridge&) = delete;
FlacAxAudioBridge& operator=(const FlacAxAudioBridge&) = delete;

void configure(pcsx2_ax::core::HardwareMode mode,
               std::string_view chip_spec = "fc3",
               AudioLoadTarget load_target = AudioLoadTarget::Speakers,
               std::string_view dac_conf_path = "/userdata/system/ax-dac.conf") noexcept {
if (const auto resolved = ResolveEssChipHook(chip_spec); resolved.has_value()) {
chip_profile_ = *resolved;
} else {
chip_profile_ = MakeEssProfile(EssChipFamily::Es9281AcPro, 2.0);
}
load_target_ = load_target;
mode_profile_ = MakeModeAudioProfile(mode, chip_profile_);
preload_policy_ = DetectHostPreloadPolicy();

if (const auto conf_vol = ParseAxDacVolumeConfig(dac_conf_path); conf_vol.has_value()) {
effective_volume_ = *conf_vol;
} else {
effective_volume_ = RecommendedVolumeForTarget(chip_profile_, load_target_);
}
}

bool initialize(std::uint64_t initial_epoch = 1ULL,
                bool non_blocking_lock = true) noexcept {
s32_ring_.PrefaultStorage();
s32_ring_.Reset();
dop_packer_.Reset();
active_epoch_.store(initial_epoch == 0ULL ? 1ULL : initial_epoch, std::memory_order_release);
frames_pushed_.store(0ULL, std::memory_order_relaxed);
frames_drained_.store(0ULL, std::memory_order_relaxed);
ring_overruns_.store(0ULL, std::memory_order_relaxed);
epoch_flushes_.store(0ULL, std::memory_order_relaxed);
av_delta_us_.store(0LL, std::memory_order_relaxed);
dsd_rate_.store(static_cast<std::uint8_t>(DsdRateKind::Unsupported), std::memory_order_relaxed);

(void)dac_lock_.Acquire(ScopedDacLock::kDefaultLinuxLockPath, non_blocking_lock);
if (dac_lock_.is_locked() && hw_mixer_.Open(chip_profile_.default_mixer_card)) {
(void)hw_mixer_.SetNormalizedVolume(effective_volume_);
}

initialized_.store(true, std::memory_order_release);
return true;
}

void on_epoch_transition(std::uint64_t new_epoch) noexcept {
const std::uint64_t norm_epoch = (new_epoch == 0ULL) ? 1ULL : new_epoch;
s32_ring_.Reset();
dop_packer_.Reset();
active_epoch_.store(norm_epoch, std::memory_order_release);
epoch_flushes_.fetch_add(1ULL, std::memory_order_relaxed);
}

bool reset(std::uint64_t epoch) noexcept {
on_epoch_transition(epoch);
return true;
}

void shutdown() noexcept {
s32_ring_.Reset();
dop_packer_.Reset();
hw_mixer_.Close();
dac_lock_.Release();
initialized_.store(false, std::memory_order_release);
}

void set_pts_drift_us(std::int64_t delta_us) noexcept {
av_delta_us_.store(delta_us, std::memory_order_relaxed);
}

void set_dsd_rate(DsdRateKind rate) noexcept {
dsd_rate_.store(static_cast<std::uint8_t>(rate), std::memory_order_relaxed);
}

std::size_t push_spu2_s32_frames(const std::int32_t* interleaved_lr,
                                 std::size_t frames) noexcept {
if (!interleaved_lr || frames == 0U) {
return 0U;
}
const std::size_t pushed = s32_ring_.PushInterleaved(interleaved_lr, frames);
frames_pushed_.fetch_add(pushed, std::memory_order_relaxed);
if (pushed < frames) {
ring_overruns_.fetch_add(frames - pushed, std::memory_order_relaxed);
}
return pushed;
}

std::size_t push_spu2_float_frames(const float* interleaved_lr,
                                   std::size_t frames) noexcept {
if (!interleaved_lr || frames == 0U) {
return 0U;
}
std::array<std::int32_t, 512U> scratch_lr{};
std::size_t total_pushed = 0U;
std::size_t offset = 0U;
while (offset < frames) {
const std::size_t batch = std::min<std::size_t>(frames - offset, scratch_lr.size() / 2U);
ConvertStereoFloatToS32(interleaved_lr + offset * 2U, scratch_lr.data(), batch);
const std::size_t pushed = push_spu2_s32_frames(scratch_lr.data(), batch);
total_pushed += pushed;
if (pushed < batch) {
break;
}
offset += batch;
}
return total_pushed;
}

std::size_t push_dop_stereo_bytes(const std::uint8_t* src_lr_bytes,
                                  std::size_t stereo_byte_pairs,
                                  DsdRateKind rate_kind) noexcept {
if (!src_lr_bytes || stereo_byte_pairs == 0U) {
return 0U;
}
set_dsd_rate(rate_kind);
std::array<std::int32_t, 256U * 2U> dop_lr{};
std::size_t total_pushed = 0U;
std::size_t offset = 0U;
while (offset < stereo_byte_pairs) {
const std::size_t batch_pairs = std::min<std::size_t>(stereo_byte_pairs - offset, 256U);
const std::size_t packed_frames = dop_packer_.PackStereoInterleaved(
src_lr_bytes + offset * 2U, batch_pairs, dop_lr.data(), 256U);
if (packed_frames > 0U) {
total_pushed += push_spu2_s32_frames(dop_lr.data(), packed_frames);
}
offset += batch_pairs;
}
return total_pushed;
}

std::size_t drain_to_s32(std::int32_t* out_interleaved_lr,
                         std::size_t max_frames) noexcept {
const std::size_t popped = s32_ring_.PopInterleaved(out_interleaved_lr, max_frames);
frames_drained_.fetch_add(popped, std::memory_order_relaxed);
return popped;
}

[[nodiscard]] const EssChipProfile& chip_profile() const noexcept { return chip_profile_; }
[[nodiscard]] const ModeAudioProfile& mode_profile() const noexcept { return mode_profile_; }
[[nodiscard]] const PreloadPolicy& preload_policy() const noexcept { return preload_policy_; }
[[nodiscard]] double effective_volume() const noexcept { return effective_volume_; }
[[nodiscard]] bool is_dac_locked() const noexcept { return dac_lock_.is_locked(); }
[[nodiscard]] int dac_lock_error() const noexcept { return dac_lock_.last_error(); }

[[nodiscard]] FlacAxAudioTelemetry telemetry() const noexcept {
return FlacAxAudioTelemetry{
.active_epoch    = active_epoch_.load(std::memory_order_acquire),
.frames_pushed   = frames_pushed_.load(std::memory_order_relaxed),
.frames_drained  = frames_drained_.load(std::memory_order_relaxed),
.ring_overruns   = ring_overruns_.load(std::memory_order_relaxed),
.epoch_flushes   = epoch_flushes_.load(std::memory_order_relaxed),
.av_delta_us     = av_delta_us_.load(std::memory_order_relaxed),
.buffered_frames = s32_ring_.AvailableToRead(),
.capacity_frames = s32_ring_.capacity_frames(),
.effective_volume= effective_volume_,
.dsd_rate        = static_cast<DsdRateKind>(dsd_rate_.load(std::memory_order_relaxed)),
.preload_mode    = preload_policy_.mode,
.dac_lock_held   = dac_lock_.is_locked(),
.initialized     = initialized_.load(std::memory_order_acquire),
};
}

private:
StereoFrameRing32 s32_ring_;
ScopedDacLock dac_lock_{};
EssHardwareMixer hw_mixer_{};
DopPacker dop_packer_{};
EssChipProfile chip_profile_{};
ModeAudioProfile mode_profile_{};
PreloadPolicy preload_policy_{};
AudioLoadTarget load_target_{AudioLoadTarget::Speakers};
double effective_volume_{0.80};
std::atomic<std::uint64_t> active_epoch_{1ULL};
std::atomic<std::uint64_t> frames_pushed_{0ULL};
std::atomic<std::uint64_t> frames_drained_{0ULL};
std::atomic<std::uint64_t> ring_overruns_{0ULL};
std::atomic<std::uint64_t> epoch_flushes_{0ULL};
std::atomic<std::int64_t> av_delta_us_{0LL};
std::atomic<std::uint8_t> dsd_rate_{static_cast<std::uint8_t>(DsdRateKind::Unsupported)};
std::atomic<bool> initialized_{false};
};

} // namespace pcsx2_ax::audio