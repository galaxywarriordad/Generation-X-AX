cmake_minimum_required(VERSION 3.22)

project(pcsx2_ax
  VERSION 0.1.0
  DESCRIPTION "PCSX2_AX shared core and mode adapters (Prototype Phase)"
  LANGUAGES C CXX
)

option(PCSX2_AX_PLANNING_ONLY "Keep PCSX2_AX in Prototype Phase guardrails" ON)
option(PCSX2_AX_ENABLE_VULKAN_1_3 "Enable Vulkan 1.3 GS offload hooks" ON)
option(PCSX2_AX_BUILD_HARNESS "Build CLI prototype harness" ON)
option(PCSX2_AX_ENABLE_LTO "Enable ThinLTO / LTO when supported" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# 0. Shared Compiler Options & Prototype Include Fallbacks
# ---------------------------------------------------------------------------
add_library(pcsx2_ax_compile_options INTERFACE)

target_compile_features(pcsx2_ax_compile_options INTERFACE cxx_std_20)

if(UNIX AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(pcsx2_ax_compile_options INTERFACE
    -O2
    -Wall
    -Wextra
    -Wpedantic
    $<$<STREQUAL:${CMAKE_SYSTEM_PROCESSOR},x86_64>:-march=skylake>
  )
elseif(MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
  target_compile_definitions(pcsx2_ax_compile_options INTERFACE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(pcsx2_ax_compile_options INTERFACE /O2 /W4 /permissive-)
endif()

if(PCSX2_AX_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
  if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
  else()
    message(STATUS "IPO/ThinLTO requested but not supported by active toolchain: ${ipo_error}")
  endif()
endif()

# Locate ax-sdk v1_36_2 (env override -> sibling checkouts -> FetchContent fallback)
if(TARGET ax_sdk)
  set(PCSX2_AX_SDK_TARGET ax_sdk)
elseif(TARGET ax::core)
  set(PCSX2_AX_SDK_TARGET ax::core)
elseif(DEFINED AX_SDK_DIR AND EXISTS "${AX_SDK_DIR}/CMakeLists.txt")
  add_subdirectory("${AX_SDK_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/ax_sdk_build" EXCLUDE_FROM_ALL)
  set(PCSX2_AX_SDK_TARGET ax_sdk)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../ax-sdk/CMakeLists.txt")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../ax-sdk" "${CMAKE_CURRENT_BINARY_DIR}/ax_sdk_build" EXCLUDE_FROM_ALL)
  set(PCSX2_AX_SDK_TARGET ax_sdk)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../ax-sdk/CMakeLists.txt")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../ax-sdk" "${CMAKE_CURRENT_BINARY_DIR}/ax_sdk_build" EXCLUDE_FROM_ALL)
  set(PCSX2_AX_SDK_TARGET ax_sdk)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../ax-sdk/CMakeLists.txt")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../ax-sdk" "${CMAKE_CURRENT_BINARY_DIR}/ax_sdk_build" EXCLUDE_FROM_ALL)
  set(PCSX2_AX_SDK_TARGET ax_sdk)
else()
  include(FetchContent)
  FetchContent_Declare(
    ax_sdk
    GIT_REPOSITORY https://github.com/galaxywarriordad/ax-sdk.git
    GIT_TAG v1_36_2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(ax_sdk)
  if(TARGET ax_sdk)
    set(PCSX2_AX_SDK_TARGET ax_sdk)
  elseif(TARGET ax::core)
    set(PCSX2_AX_SDK_TARGET ax::core)
  else()
    message(WARNING "ax-sdk (v1_36_2) target not found; building standalone prototype stubs only.")
  endif()
endif()

set(PCSX2_AX_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/core")
set(PCSX2_AX_PROTO_INC_DIR "${CMAKE_CURRENT_BINARY_DIR}/proto_inc")

function(pcsx2_ax_write_stub rel_path content)
  set(full_path "${PCSX2_AX_PROTO_INC_DIR}/${rel_path}")
  get_filename_component(parent_dir "${full_path}" DIRECTORY)
  file(MAKE_DIRECTORY "${parent_dir}")
  if(NOT EXISTS "${PCSX2_AX_CORE_DIR}/${rel_path}")
    file(WRITE "${full_path}" "${content}")
  endif()
endfunction()

if(PCSX2_AX_PLANNING_ONLY)
  file(MAKE_DIRECTORY "${PCSX2_AX_PROTO_INC_DIR}")

  pcsx2_ax_write_stub("Common.h"
"#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;
using uint = unsigned int;
using sptr = intptr_t;
using uptr = uintptr_t;

namespace Ps2MemSize {
inline constexpr size_t GSregs = 0x2000;
}

#ifndef __fi
#if defined(_MSC_VER)
#define __fi __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define __fi inline __attribute__((always_inline))
#else
#define __fi inline
#endif
#endif

#ifndef __noinline
#if defined(_MSC_VER)
#define __noinline __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define __noinline __attribute__((noinline))
#else
#define __noinline
#endif
#endif

inline u8 itob(u8 x) noexcept { return static_cast<u8>(((x / 10u) << 4u) | (x % 10u)); }
inline u8 btoi(u8 x) noexcept { return static_cast<u8>(((x >> 4u) * 10u) + (x & 0x0Fu)); }
inline u16 GetBufferU16(const u8* buf, size_t off) noexcept {
  u16 v = 0;
  std::memcpy(&v, buf + off, sizeof(v));
  return v;
}
inline u32 GetBufferU32(const u8* buf, size_t off) noexcept {
  u32 v = 0;
  std::memcpy(&v, buf + off, sizeof(v));
  return v;
}
")

  pcsx2_ax_write_stub("MemoryTypes.h"
"#pragma once
#include <cstddef>
#include <cstdint>
#include \"Common.h\"

inline uint8_t* eeMem = nullptr;
inline uint8_t* iopMem = nullptr;
inline uint8_t* vu0Mem = nullptr;
inline uint8_t* vu1Mem = nullptr;
")

  pcsx2_ax_write_stub("Config.h"
"#pragma once
#include <cstdint>
#include <string>

enum class LimiterModeType { Nominal, Slomo, Turbo, Unlimited };
enum class GSVSyncMode { Disabled, FIFO, Mailbox };
enum class AspectRatioType { RAuto4_3_3_2, R4_3, R16_9, R10_7, Stretch };
enum class SavestateCompressionMethod { Uncompressed, Deflate, Zstandard };
enum class SavestateCompressionLevel { Low, Medium, High, VeryHigh };

struct Pcsx2Config {
  struct CpuOptions {
    struct RecompilerOptions {
      bool EnableFastmem = true;
      bool EnableVU0 = true;
      bool EnableVU1 = true;
      bool operator==(const RecompilerOptions&) const = default;
    } Recompiler;
    int ExtraMemory = 0;
    uint32_t FPUFPCR = 0;
    bool operator==(const CpuOptions&) const = default;
    bool CpusChanged(const CpuOptions&) const noexcept { return false; }
  } Cpu;

  struct GSOptions {
    static constexpr double DEFAULT_FRAME_RATE_NTSC = 59.94;
    static constexpr double DEFAULT_FRAME_RATE_PAL = 50.0;
    double FramerateNTSC = DEFAULT_FRAME_RATE_NTSC;
    double FrameratePAL = DEFAULT_FRAME_RATE_PAL;
    float UpscaleMultiplier = 1.0f;
    AspectRatioType AspectRatio = AspectRatioType::R4_3;
    bool VsyncEnable = true;
    bool DisableMailboxPresentation = false;
    bool SkipDuplicateFrames = false;
    bool ManualUserHacks = false;
    bool DumpReplayUseFrameRange = false;
    uint32_t DumpReplayFrameStart = 0;
    uint32_t DumpReplayFrameEnd = 0;
    uint32_t DumpReplayLoopCount = 1;
    bool operator==(const GSOptions&) const = default;
    void MaskUserHacks() noexcept {}
    void MaskUpscalingHacks() noexcept {}
  } GS;

  struct SpeedhackOptions {
    bool fastCDVD = false;
    bool vuThread = true;
    int EECycleRate = 0;
    int EECycleSkip = 0;
    bool operator==(const SpeedhackOptions&) const = default;
  } Speedhacks;

  struct GamefixOptions {
    bool InstantDMAHack = false;
    bool GoemonTlbHack = false;
    bool operator==(const GamefixOptions&) const = default;
  } Gamefixes;

  struct ProfilerOptions {
    bool operator==(const ProfilerOptions&) const = default;
  } Profiler;

  struct EmulationSpeedOptions {
    float NominalScalar = 1.0f;
    float SlomoScalar = 0.5f;
    float TurboScalar = 2.0f;
    bool SyncToHostRefreshRate = false;
    bool UseVSyncForTiming = false;
    bool operator==(const EmulationSpeedOptions&) const = default;
  } EmulationSpeed;

  struct AchievementsOptions {
    bool Enabled = false;
    bool operator==(const AchievementsOptions&) const = default;
  } Achievements;

  struct TraceOptions {
    bool Enabled = false;
    struct { uint32_t bitset = 0; } EE, IOP, MISC;
    void SyncToConfig() noexcept {}
  } Trace;

  struct SavestateOptions {
    SavestateCompressionMethod CompressionType = SavestateCompressionMethod::Zstandard;
    SavestateCompressionLevel CompressionRatio = SavestateCompressionLevel::Medium;
  } Savestate;

  struct DEV9Options {
    bool operator==(const DEV9Options&) const = default;
  } DEV9;

  bool EnableFastBoot = true;
  bool EnableFastBootFastForward = false;
  bool EnableGameFixes = true;
  bool EnableCheats = false;
  bool EnableWideScreenPatches = false;
  bool EnableNoInterlacingPatches = false;
  bool EnablePatches = true;
  bool EnableRecordingTools = false;
  bool EnablePINE = false;
  int PINESlot = 28011;
  bool EnableDiscordPresence = false;
  bool EnableThreadPinning = false;
  bool InhibitScreensaver = false;
  bool WarnAboutUnsafeSettings = true;
  bool ManuallySetRealTimeClock = false;
  bool BackupSavestate = true;
  float CurrentCustomAspectRatio = 0.0f;
  int RtcSecond = 0, RtcMinute = 0, RtcHour = 0, RtcDay = 1, RtcMonth = 1, RtcYear = 0;

  void CopyRuntimeConfig(const Pcsx2Config&) noexcept {}
  template <typename T> void LoadSave(T&) {}
};

inline Pcsx2Config EmuConfig{};
inline Pcsx2Config::GSOptions& GSConfig = EmuConfig.GS;
inline bool NoOSD = false;
inline bool CHECK_EEREC = false;
inline bool CHECK_IOPREC = false;
inline bool CHECK_EXTRAMEM = false;
inline bool THREAD_VU1 = false;
inline bool IsDevBuild = true;
")

  pcsx2_ax_write_stub("Counters.h"
"#pragma once
#include <cstdint>
inline uint64_t EEsCycle = 0;
inline uint64_t EEoCycle = 0;
inline uint64_t nextDeltaCounter = 0;
inline uint64_t nextStartCounter = 0;
inline uint64_t psxNextStartCounter = 0;
inline uint64_t psxNextDeltaCounter = 0;
inline bool rcntFreeze() { return true; }
inline bool psxRcntFreeze() { return true; }
inline void intUpdateCPUCycles() {}
inline void intEventTest() {}
inline void UpdateVSyncRate(bool) {}
inline double GetVerticalFrequency() { return 59.94; }
")

  pcsx2_ax_write_stub("Gif_Unit.h"
"#pragma once
#include <array>
#include <cstdint>

struct GsSignalState {
  bool queued = false;
  std::array<uint32_t, 2> data{0, 0};
};

struct GsFinishState {
  bool gsFINISHFired = false;
  bool gsFINISHPending = false;
};

struct gifUnit {
  GsSignalState gsSIGNAL{};
  GsFinishState gsFINISH{};
  void Reset() noexcept {
    gsSIGNAL.queued = false;
    gsSIGNAL.data.fill(0);
    gsFINISH.gsFINISHFired = false;
    gsFINISH.gsFINISHPending = false;
  }
  bool Freeze() noexcept { return true; }
};

inline gifUnit g_gifUnit{};
")

  pcsx2_ax_write_stub("GS.h"
"#pragma once
#include <cstdint>
#include \"Common.h\"

struct tGS_CSR {
  uint64_t raw = 0;
  bool SIGNAL = false;
  bool FINISH = false;
  bool RESET() const noexcept { return (raw & 0x2ULL) != 0; }
};

struct tGS_IMR {
  uint64_t raw = 0;
  void reset() noexcept { raw = 0; }
};

struct tCSRreg {
  bool SIGNAL = false;
  bool FINISH = false;
  void Reset() noexcept {
    SIGNAL = false;
    FINISH = false;
  }
};

struct tGsSigLblId {
  uint32_t SIGID = 0;
  uint32_t LBLID = 0;
};

inline tGS_IMR GSIMR{};
inline tCSRreg CSRreg{};
inline tGsSigLblId GSSIGLBLID{};
")

  pcsx2_ax_write_stub("Host.h"
"#pragma once
#include <mutex>
#include <string>
#include <string_view>

class SettingsInterface {
public:
  virtual ~SettingsInterface() = default;
};

namespace Host {

inline constexpr float OSD_QUICK_DURATION = 2.0f;
inline constexpr float OSD_WARNING_DURATION = 5.0f;

inline void ReportErrorAsync(std::string_view, std::string_view) {}
inline void OnGameChanged(std::string_view, std::string_view, std::string_view, std::string_view, uint32_t, uint32_t) {}
inline void OnVMStarting() {}
inline void OnVMStarted() {}
inline void OnVMDestroyed() {}
inline void OnVMPaused() {}
inline void OnVMResumed() {}
inline void AddIconOSDMessage(std::string_view, std::string_view, std::string_view, float) {}
inline void AddKeyedOSDMessage(std::string_view, std::string, float) {}
inline void RemoveKeyedOSDMessage(std::string_view) {}
inline void RequestResizeHostDisplay(int, int) {}
inline void RequestVMShutdown(bool) {}
inline void PumpMessagesOnCPUThread() {}
inline std::unique_lock<std::mutex> GetSettingsLock() {
  static std::mutex m;
  return std::unique_lock<std::mutex>(m);
}
inline SettingsInterface* GetSettingsInterface() noexcept { return nullptr; }
inline bool GetBaseBoolSettingValue(std::string_view, std::string_view, bool def = false) { return def; }
inline void SetDefaultUISettings(SettingsInterface&) {}
inline void LoadSettings(SettingsInterface&, std::unique_lock<std::mutex>&) {}
inline void CheckForSettingsChanges(const void*) {}

namespace Internal {
inline SettingsInterface* GetBaseSettingsLayer() noexcept { return nullptr; }
inline SettingsInterface* GetGameSettingsLayer() noexcept { return nullptr; }
inline void SetGameSettingsLayer(SettingsInterface*, std::unique_lock<std::mutex>&) {}
inline SettingsInterface* GetInputSettingsLayer() noexcept { return nullptr; }
inline void SetInputSettingsLayer(SettingsInterface*, std::unique_lock<std::mutex>&) {}
} // namespace Internal

} // namespace Host
")

  pcsx2_ax_write_stub("MTGS.h"
"#pragma once
#include <cstdint>
#include <functional>

enum class GSVideoMode { Uninitialized = 0, NTSC, PAL };
using GS_VideoMode = GSVideoMode;

inline GSVideoMode gsVideoMode = GSVideoMode::Uninitialized;
inline bool gsIsInterlaced = false;
inline void GSgetInternalResolution(int* width, int* height) noexcept {
  if (width) *width = 640;
  if (height) *height = 480;
}
inline int GSgetDisplayMode() noexcept { return 0; }
inline float GSGetHostRefreshRate() noexcept { return 60.0f; }

namespace MTGS {
inline void WaitGS(bool = false) noexcept {}
inline void ReleaseGS() noexcept {}
inline void ResetGS(bool = false) noexcept {}
inline void ApplySettings() noexcept {}
inline void UpdateVSyncMode() noexcept {}
inline void GameChanged() noexcept {}
inline void PresentCurrentFrame() noexcept {}
inline bool IsOpen() noexcept { return false; }
inline void RunOnGSThread(std::function<void()> fn) { if (fn) fn(); }
struct DummyThreadHandle { void SetAffinity(uint64_t) noexcept {} };
inline DummyThreadHandle& GetThreadHandle() noexcept {
  static DummyThreadHandle h{};
  return h;
}
} // namespace MTGS
")

  pcsx2_ax_write_stub("IconsFontAwesome.h"
"#pragma once
#define ICON_FA_FLOPPY_DISK \"\"
#define ICON_FA_FOLDER_OPEN \"\"
#define ICON_FA_TRIANGLE_EXCLAMATION \"\"
")

  pcsx2_ax_write_stub("common/Pcsx2Defs.h"
"#pragma once
#include <cstddef>
#include <cstdint>
#include \"Common.h\"

#if defined(_MSC_VER)
#define PCSX2_EXPORT_API __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define PCSX2_EXPORT_API __attribute__((visibility(\"default\")))
#else
#define PCSX2_EXPORT_API
#endif

static constexpr size_t HOST_PAGE_SIZE = 4096;
")

  pcsx2_ax_write_stub("common/Assertions.h"
"#pragma once
#include \"common/Console.h\"
")

  pcsx2_ax_write_stub("common/EnumOps.h"
"#pragma once
#include <type_traits>

template <typename E>
concept ScopedEnum = std::is_enum_v<E>;
")

  pcsx2_ax_write_stub("common/FileSystem.h"
"#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

class Error;

namespace FileSystem {

inline std::FILE* OpenCFile(const char* path, const char* mode, Error* = nullptr) noexcept {
  return std::fopen(path, mode);
}

inline int64_t FSeek64(std::FILE* fp, int64_t offset, int origin) noexcept {
  return std::fseek(fp, static_cast<long>(offset), origin);
}

inline int64_t FTell64(std::FILE* fp) noexcept {
  return std::ftell(fp);
}

inline bool DirectoryExists(const char*) noexcept { return false; }
inline bool FileExists(const char*) noexcept { return false; }

} // namespace FileSystem
")

  pcsx2_ax_write_stub("common/Path.h"
"#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace Path {

inline std::string Combine(std::string_view lhs, std::string_view rhs) {
  return (std::filesystem::path(lhs) / std::filesystem::path(rhs)).string();
}

inline std::string_view GetFileName(std::string_view path) noexcept {
  const auto pos = path.find_last_of(\"/\\\\\");
  return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

inline std::string ReplaceExtension(std::string_view path, std::string_view ext) {
  std::filesystem::path p(path);
  p.replace_extension(ext);
  return p.string();
}

} // namespace Path
")

  pcsx2_ax_write_stub("CDVD/CDVDcommon.h"
"#pragma once
#include <cstdint>
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;
using uint = unsigned int;

enum CdvdDiscType {
  CDVD_TYPE_NODISC = 0x00,
  CDVD_TYPE_DETCTDVDS = 0x10,
  CDVD_TYPE_DETCTDVDD = 0x11,
  CDVD_TYPE_DETCTCD = 0x12,
  CDVD_TYPE_DETCT = 0x13,
  CDVD_TYPE_PS2DVD = 0x14,
  CDVD_TYPE_DVDV = 0xFE,
};

enum CdvdTrayStatus {
  CDVD_TRAY_CLOSE = 0,
  CDVD_TRAY_OPEN = 1,
};

enum CdvdReadMode {
  CDVD_MODE_2048 = 0,
  CDVD_MODE_2328 = 1,
  CDVD_MODE_2340 = 2,
  CDVD_MODE_2352 = 3,
  CDVD_MODE_2368 = 4,
};

enum CdvdTrackType {
  CDVD_AUDIO_TRACK = 0,
  CDVD_MODE1_TRACK = 1,
  CDVD_MODE2_TRACK = 2,
};

struct cdvdTN { u8 strack = 1; u8 etrack = 1; };
struct cdvdTD { u32 lsn = 0; u8 type = 0; };
struct cdvdSubQ {
  u8 ctrl = 0, adr = 0, trackNum = 0, trackIndex = 0;
  u8 trackM = 0, trackS = 0, trackF = 0, pad = 0;
  u8 discM = 0, discS = 0, discF = 0;
};
struct cdvdTrack { u32 start_lba = 0; u8 type = 0; };
struct ProgressCallback {};

struct CDVD_API {
  void (*close)();
  bool (*open)(const char*, void*);
  bool (*precache)(ProgressCallback*, void*);
  int32_t (*readTrack)(u32, int);
  int32_t (*getBuffer)(u8*);
  int32_t (*readSubQ)(u32, cdvdSubQ*);
  int32_t (*getTN)(cdvdTN*);
  int32_t (*getTD)(u8, cdvdTD*);
  int32_t (*getTOC)(void*);
  int32_t (*getDiskType)();
  int32_t (*getTrayStatus)();
  int32_t (*ctrlTrayOpen)();
  int32_t (*ctrlTrayClose)();
  void (*newDiskCB)(void (*)());
  int32_t (*readSector)(u8*, u32, int);
  int32_t (*getDualInfo)(s32*, u32*);
};

extern const CDVD_API* CDVD;
extern u32 g_last_sector_block_lsn;
")

  pcsx2_ax_write_stub("CDVD/IsoFileFormats.h"
"#pragma once
#include <cstdint>
#include <string>

enum class CDVDDiscType { Other, PS1Disc, PS2Disc };

class IsoFile {
public:
  virtual ~IsoFile() = default;
  virtual bool Open(const std::string&) { return false; }
  virtual void Close() {}
};
")

  pcsx2_ax_write_stub("CDVD/IsoReader.h"
"#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class Error;

class IsoReader {
public:
  IsoReader() = default;
  ~IsoReader() = default;
  bool Open(Error*) { return false; }
  bool ReadFile(const char*, std::vector<uint8_t>*, Error*) { return false; }
  static std::string_view RemoveVersionIdentifierFromPath(std::string_view path) noexcept { return path; }
};
")

  pcsx2_ax_write_stub("common/Error.h"
"#pragma once
#include <string>
#include <string_view>
#include <utility>

class Error {
public:
  void SetString(std::string s) { m_desc = std::move(s); m_valid = true; }
  void SetStringView(std::string_view sv) { m_desc.assign(sv); m_valid = true; }
  template <typename... Args>
  void SetStringFmt(std::string_view fmt_str, Args&&...) { m_desc.assign(fmt_str); m_valid = true; }
  static void SetString(Error* err, std::string s) { if (err) err->SetString(std::move(s)); }
  static void SetStringView(Error* err, std::string_view sv) { if (err) err->SetStringView(sv); }
  template <typename... Args>
  static void SetStringFmt(Error* err, std::string_view fmt_str, Args&&...) { if (err) err->SetStringView(fmt_str); }
  static Error CreateErrno(int code) { Error e; e.SetString(\"errno=\" + std::to_string(code)); return e; }
  [[nodiscard]] bool IsValid() const noexcept { return m_valid; }
  [[nodiscard]] const std::string& GetDescription() const noexcept { return m_desc; }
private:
  std::string m_desc{};
  bool m_valid = false;
};
")

  pcsx2_ax_write_stub("common/Console.h"
"#pragma once
#include <cstdio>
#include <string>
#include <string_view>

enum ConsoleColors {
  Color_Default = 0,
  Color_Green,
  Color_Red,
  Color_Yellow,
  Color_Blue,
  Color_StrongBlue,
  Color_StrongGreen,
  Color_StrongRed,
  Color_StrongBlack,
  Color_Gray,
};

struct ConsoleStream {
  template <typename... Args> void WriteLn(const char* fmt, Args&&...) const noexcept {
    std::printf(\"%s\\n\", fmt);
  }
  template <typename... Args> void WriteLn(ConsoleColors, const char* fmt, Args&&...) const noexcept {
    std::printf(\"%s\\n\", fmt);
  }
  void WriteLn(const std::string& s) const { std::printf(\"%s\\n\", s.c_str()); }
  template <typename... Args> void WriteLnFmt(const char* fmt, Args&&...) const noexcept {
    std::printf(\"%s\\n\", fmt);
  }
  template <typename... Args> void Warning(const char* fmt, Args&&...) const noexcept {
    std::printf(\"%s\\n\", fmt);
  }
  void Warning(const std::string& s) const { std::printf(\"%s\\n\", s.c_str()); }
  template <typename... Args> void WarningFmt(const char* fmt, Args&&...) const noexcept {
    std::printf(\"%s\\n\", fmt);
  }
  template <typename... Args> void Error(const char* fmt, Args&&...) const noexcept {
    std::printf(\"%s\\n\", fmt);
  }
  void Error(const std::string& s) const { std::printf(\"%s\\n\", s.c_str()); }
};

inline ConsoleStream Console{};
inline ConsoleStream DevCon{};
inline ConsoleStream DbgCon{};

#define ERROR_LOG(...) do { Console.Error(__VA_ARGS__); } while (0)
#define WARNING_LOG(...) do { Console.Warning(__VA_ARGS__); } while (0)
#define INFO_LOG(...) do { Console.WriteLn(__VA_ARGS__); } while (0)
#define DEV_LOG(...) do { DevCon.WriteLn(__VA_ARGS__); } while (0)
#define GUNIT_WARN(...) do { Console.Warning(__VA_ARGS__); } while (0)
#define GUNIT_LOG(...) do { DevCon.WriteLn(__VA_ARGS__); } while (0)
#define pxAssert(expr) do { (void)sizeof(expr); } while (0)
#define pxAssertMsg(expr, msg) do { (void)sizeof(expr); (void)sizeof(msg); } while (0)
#define pxAssertRel(expr, msg) do { (void)sizeof(expr); (void)sizeof(msg); } while (0)
#define ASSUME(expr) do { (void)sizeof(expr); } while (0)
")

  pcsx2_ax_write_stub("common/Threading.h"
"#pragma once
#include <cstdint>
#include <string_view>

namespace Threading {

class ThreadHandle {
public:
  ThreadHandle() noexcept = default;
  static ThreadHandle GetForCallingThread() noexcept { return {}; }
  void SetAffinity(uint64_t) noexcept {}
};

inline void Sleep(int) noexcept {}
inline void SpinWait() noexcept {}
inline void SetNameOfCurrentThread(std::string_view) noexcept {}

} // namespace Threading
")

  pcsx2_ax_write_stub("common/ScopedGuard.h"
"#pragma once
#include <utility>

template <typename Fn>
class ScopedGuard {
public:
  explicit ScopedGuard(Fn fn) noexcept : m_fn(std::move(fn)) {}
  ~ScopedGuard() noexcept {
    if (m_active)
      m_fn();
  }
  ScopedGuard(const ScopedGuard&) = delete;
  ScopedGuard& operator=(const ScopedGuard&) = delete;
  void Cancel() noexcept { m_active = false; }
private:
  Fn m_fn;
  bool m_active = true;
};
")

  pcsx2_ax_write_stub("common/Timer.h"
"#pragma once
#include <cstdint>

namespace Common {

class Timer {
public:
  Timer() noexcept = default;
  [[nodiscard]] double GetTimeMilliseconds() const noexcept { return 0.0; }
  [[nodiscard]] static int64_t GetCurrentValue() noexcept { return 0; }
  [[nodiscard]] static double ConvertValueToSeconds(uint64_t) noexcept { return 0.0; }
};

} // namespace Common

inline uint64_t GetCPUTicks() noexcept { return 0; }
inline uint64_t GetTickFrequency() noexcept { return 1000000000ULL; }
")
endif()

# ---------------------------------------------------------------------------
# 1. Shared Deduplicated Core: libax_pcsx2_core (engines/pcsx2_ax/core)
# ---------------------------------------------------------------------------
set(PCSX2_AX_CORE_HEADERS
  "${PCSX2_AX_CORE_DIR}/CDVDRingSlab.hpp"
  "${PCSX2_AX_CORE_DIR}/IHardwareModeAdapter.hpp"
  "${PCSX2_AX_CORE_DIR}/VMManager_Internal.h"
)

set(PCSX2_AX_CORE_SOURCES
  "${PCSX2_AX_CORE_DIR}/CDVDcommon.cpp"
  "${PCSX2_AX_CORE_DIR}/CDVDdiscThread.cpp"
  "${PCSX2_AX_CORE_DIR}/CDVDisoReader.cpp"
  "${PCSX2_AX_CORE_DIR}/GS.cpp"
  "${PCSX2_AX_CORE_DIR}/R5900OpcodeImpl.cpp"
  "${PCSX2_AX_CORE_DIR}/SPR.cpp"
  "${PCSX2_AX_CORE_DIR}/SPU2.cpp"
  "${PCSX2_AX_CORE_DIR}/SaveState.cpp"
  "${PCSX2_AX_CORE_DIR}/Sif.cpp"
  "${PCSX2_AX_CORE_DIR}/Sif0.cpp"
  "${PCSX2_AX_CORE_DIR}/Sif1.cpp"
  "${PCSX2_AX_CORE_DIR}/StateWrapper.cpp"
  "${PCSX2_AX_CORE_DIR}/VMManager.cpp"
  "${PCSX2_AX_CORE_DIR}/VMManager_Config.cpp"
  "${PCSX2_AX_CORE_DIR}/VMManager_Media.cpp"
  "${PCSX2_AX_CORE_DIR}/VMManager_SaveState.cpp"
  "${PCSX2_AX_CORE_DIR}/VU0.cpp"
  "${PCSX2_AX_CORE_DIR}/VU0micro.cpp"
  "${PCSX2_AX_CORE_DIR}/VU0microInterp.cpp"
  "${PCSX2_AX_CORE_DIR}/VU1micro.cpp"
  "${PCSX2_AX_CORE_DIR}/VU1microInterp.cpp"
  "${PCSX2_AX_CORE_DIR}/VUflags.cpp"
  "${PCSX2_AX_CORE_DIR}/VUmicro.cpp"
  "${PCSX2_AX_CORE_DIR}/VUmicroMem.cpp"
  "${PCSX2_AX_CORE_DIR}/VUops.cpp"
  "${PCSX2_AX_CORE_DIR}/Vif.cpp"
  "${PCSX2_AX_CORE_DIR}/Vif0_Dma.cpp"
  "${PCSX2_AX_CORE_DIR}/Vif1_Dma.cpp"
  "${PCSX2_AX_CORE_DIR}/Vif1_MFIFO.cpp"
  "${PCSX2_AX_CORE_DIR}/Vif_Transfer.cpp"
  "${PCSX2_AX_CORE_DIR}/Vif_Unpack.cpp"
  "${PCSX2_AX_CORE_DIR}/sif2.cpp"
  "${PCSX2_AX_CORE_DIR}/vtlb.cpp"
)

add_library(ax_pcsx2_core STATIC ${PCSX2_AX_CORE_SOURCES} ${PCSX2_AX_CORE_HEADERS})
add_library(PCSX2_AX::core ALIAS ax_pcsx2_core)
set_target_properties(ax_pcsx2_core PROPERTIES
  OUTPUT_NAME "ax_pcsx2_core"
  CXX_VISIBILITY_PRESET hidden
)

target_include_directories(ax_pcsx2_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PCSX2_AX_CORE_DIR}"
    $<$<BOOL:${PCSX2_AX_PLANNING_ONLY}>:${PCSX2_AX_PROTO_INC_DIR}>
)

target_compile_definitions(ax_pcsx2_core
  PUBLIC
    PCSX2_AX_SDK_VERSION_TARGET="v1_36_2"
    PCSX2_AX_SHM_SYNC_DAEMON="K01piccolo-sync"
    PCSX2_AX_VRAM_PRUNE_GIB=5.00
    PCSX2_AX_VRAM_CEILING_GIB=5.50
    $<$<BOOL:${PCSX2_AX_PLANNING_ONLY}>:PCSX2_AX_PLANNING_ONLY=1>
    $<$<BOOL:${PCSX2_AX_ENABLE_VULKAN_1_3}>:PCSX2_AX_HAS_VULKAN_1_3=1>
)

target_link_libraries(ax_pcsx2_core
  PUBLIC
    pcsx2_ax_compile_options
)

if(DEFINED PCSX2_AX_SDK_TARGET AND TARGET ${PCSX2_AX_SDK_TARGET})
  target_link_libraries(ax_pcsx2_core PUBLIC ${PCSX2_AX_SDK_TARGET})
endif()

# ---------------------------------------------------------------------------
# 2. Retail PS2 Mode Adapter: Ps2ConsoleAdapter (engines/pcsx2_ax/adapters/ps2)
# ---------------------------------------------------------------------------
set(PCSX2_AX_PS2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/adapters/ps2")

set(PCSX2_AX_PS2_SOURCES
  "${PCSX2_AX_PS2_DIR}/BiosTools.cpp"
  "${PCSX2_AX_PS2_DIR}/CDVD.cpp"
  "${PCSX2_AX_PS2_DIR}/CDVDdiscReader.cpp"
  "${PCSX2_AX_PS2_DIR}/IopBios.cpp"
  "${PCSX2_AX_PS2_DIR}/MemoryCardFile.cpp"
  "${PCSX2_AX_PS2_DIR}/MemoryCardFolder.cpp"
  "${PCSX2_AX_PS2_DIR}/MemoryCardProtocol.cpp"
  "${PCSX2_AX_PS2_DIR}/Pad.cpp"
  "${PCSX2_AX_PS2_DIR}/PadBase.cpp"
  "${PCSX2_AX_PS2_DIR}/PadDualshock1.cpp"
  "${PCSX2_AX_PS2_DIR}/PadDualshock2.cpp"
  "${PCSX2_AX_PS2_DIR}/PsxBios.cpp"
  "${PCSX2_AX_PS2_DIR}/Sio2.cpp"
)

add_library(ax_pcsx2_adapter_ps2 STATIC ${PCSX2_AX_PS2_SOURCES})
add_library(PCSX2_AX::ps2 ALIAS ax_pcsx2_adapter_ps2)
set_target_properties(ax_pcsx2_adapter_ps2 PROPERTIES CXX_VISIBILITY_PRESET hidden)

target_include_directories(ax_pcsx2_adapter_ps2
  PUBLIC
    "${PCSX2_AX_PS2_DIR}"
)

target_compile_definitions(ax_pcsx2_adapter_ps2
  PUBLIC
    PCSX2_AX_SAVE_ROOT_PS2="/userdata/saves/ps2/"
)

target_link_libraries(ax_pcsx2_adapter_ps2
  PUBLIC
    PCSX2_AX::core
    pcsx2_ax_compile_options
)

# ---------------------------------------------------------------------------
# 3. COH-H / System 246 & 256 Adapter: CohHArcadeAdapter (engines/pcsx2_ax/adapters/coh_h)
# ---------------------------------------------------------------------------
set(PCSX2_AX_COHH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/adapters/coh_h")

set(PCSX2_AX_COHH_SOURCES
  "${PCSX2_AX_COHH_DIR}/ACATA.cpp"
  "${PCSX2_AX_COHH_DIR}/ACATAPI.cpp"
  "${PCSX2_AX_COHH_DIR}/ACATA_IO.cpp"
  "${PCSX2_AX_COHH_DIR}/ACATA_IO_CHD.cpp"
  "${PCSX2_AX_COHH_DIR}/ACCORE.cpp"
  "${PCSX2_AX_COHH_DIR}/ACJV.cpp"
  "${PCSX2_AX_COHH_DIR}/ACRAM.cpp"
  "${PCSX2_AX_COHH_DIR}/ACSRAM.cpp"
  "${PCSX2_AX_COHH_DIR}/ACUART.cpp"
  "${PCSX2_AX_COHH_DIR}/ACUART_GLUE.cpp"
)

add_library(ax_pcsx2_adapter_coh_h STATIC ${PCSX2_AX_COHH_SOURCES})
add_library(PCSX2_AX::coh_h ALIAS ax_pcsx2_adapter_coh_h)
set_target_properties(ax_pcsx2_adapter_coh_h PROPERTIES CXX_VISIBILITY_PRESET hidden)

target_include_directories(ax_pcsx2_adapter_coh_h
  PUBLIC
    "${PCSX2_AX_COHH_DIR}"
)

target_compile_definitions(ax_pcsx2_adapter_coh_h
  PUBLIC
    PCSX2_AX_SAVE_ROOT_COHH="/userdata/saves/pcsx2x6/"
)

target_link_libraries(ax_pcsx2_adapter_coh_h
  PUBLIC
    PCSX2_AX::core
    pcsx2_ax_compile_options
)

# ---------------------------------------------------------------------------
# 4. Minimal CLI Prototype Harness
# ---------------------------------------------------------------------------
if(PCSX2_AX_BUILD_HARNESS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pcsx2_ax_harness.cpp")
  add_executable(pcsx2_ax_harness
    "${CMAKE_CURRENT_SOURCE_DIR}/pcsx2_ax_harness.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/vm_bridge_stub.cpp"
  )
  set_target_properties(pcsx2_ax_harness PROPERTIES CXX_VISIBILITY_PRESET hidden)
  target_link_libraries(pcsx2_ax_harness
    PRIVATE
      PCSX2_AX::ps2
      PCSX2_AX::coh_h
      pcsx2_ax_compile_options
  )
endif()
