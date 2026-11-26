// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team & The Silicon Preservation Society
// SPDX-License-Identifier: GPL-3.0+

#include "Achievements.h"
#include "BuildVersion.h"
#include "CDVD/IsoReader.h"
#include "Counters.h"
#include "DEV9/DEV9.h"
#include "DebugTools/DebugInterface.h"
#include "DebugTools/SymbolImporter.h"
#include "Elfheader.h"
#include "FW.h"
#include "GS.h"
#include "GS/Renderers/HW/GSTextureReplacements.h"
#include "GSDumpReplayer.h"
#include "GS/GSCapture.h"
#include "GameDatabase.h"
#include "GameList.h"
#include "Host.h"
#include "IHardwareModeAdapter.hpp"
#include "INISettingsInterface.h"
#include "ImGui/FullscreenUI.h"
#include "ImGui/ImGuiOverlays.h"
#include "Input/InputManager.h"
#include "MTGS.h"
#include "MTVU.h"
#include "PINE.h"
#include "Patch.h"
#include "PerformanceMetrics.h"
#include "R3000A.h"
#include "R5900.h"
#include "Recording/InputRecording.h"
#include "Recording/InputRecordingControls.h"
#include "SIO/Sio.h"
#include "SIO/Sio0.h"
#include "SPU2/spu2.h"
#include "SupportURLs.h"
#include "USB/USB.h"
#include "Vif_Dynarec.h"
#include "VMManager.h"

#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/FPControl.h"
#include "common/ScopedGuard.h"
#include "common/SettingsWrapper.h"
#include "common/SmallString.h"
#include "common/StringUtil.h"
#include "common/Threading.h"
#include "common/Timer.h"
#include "common/emitter/x86emitter.h"

#include "IconsFontAwesome.h"
#include "IconsPromptFont.h"
#include "cpuinfo.h"
#include "discord_rpc.h"
#include "fmt/format.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <sstream>
#include <common/RedtapeWilCom.h>

#ifdef _WIN32
#include "common/RedtapeWindows.h"
#include <objbase.h>
#include <timeapi.h>
#include <powrprof.h>
#include <wil/com.h>
#include <dxgi.h>
#endif

#ifdef __APPLE__
#include "common/Darwin/DarwinMisc.h"
#endif

namespace pcsx2_ax::vm_bridge {

namespace {

inline pcsx2_ax::core::IHardwareModeAdapter*& active_adapter() noexcept
{
	static pcsx2_ax::core::IHardwareModeAdapter* adapter = nullptr;
	return adapter;
}

inline std::string& active_disc_file()
{
	static std::string path;
	return path;
}

inline bool& active_disc_ready() noexcept
{
	static bool ready = false;
	return ready;
}

inline bool& active_memcard_busy() noexcept
{
	static bool busy = false;
	return busy;
}

} // namespace

void bind_mode_adapter(pcsx2_ax::core::IHardwareModeAdapter* adapter) noexcept
{
	active_adapter() = adapter;
}

[[nodiscard]] pcsx2_ax::core::IHardwareModeAdapter* get_mode_adapter() noexcept
{
	return active_adapter();
}

void set_media_path(std::string path, bool ready)
{
	active_disc_file() = std::move(path);
	active_disc_ready() = ready;
}

[[nodiscard]] const std::string& get_media_path() noexcept
{
	return active_disc_file();
}

[[nodiscard]] bool is_media_ready() noexcept
{
	return active_disc_ready();
}

void set_memcard_busy(bool busy) noexcept
{
	active_memcard_busy() = busy;
}

[[nodiscard]] bool is_memcard_busy() noexcept
{
	return active_memcard_busy();
}

[[nodiscard]] bool init_mode_subsystems()
{
	if (auto* adapter = active_adapter(); adapter != nullptr)
		return adapter->initialize();
	return true;
}

[[nodiscard]] bool reset_mode_subsystems()
{
	if (auto* adapter = active_adapter(); adapter != nullptr)
		return adapter->reset();
	return true;
}

void shutdown_mode_subsystems() noexcept
{
	if (auto* adapter = active_adapter(); adapter != nullptr)
		adapter->shutdown();
}

void notify_mode_vsync() noexcept
{
	if (auto* adapter = active_adapter(); adapter != nullptr)
		adapter->on_vsync();
}

[[nodiscard]] pcsx2_ax::core::BiosDescriptor current_bios_info() noexcept
{
	if (auto* adapter = active_adapter(); adapter != nullptr)
		return adapter->bios_descriptor();
	return pcsx2_ax::core::BiosDescriptor{};
}

} // namespace pcsx2_ax::vm_bridge

namespace VMManager
{
	static void SetDefaultLoggingSettings(SettingsInterface& si);
	static void UpdateLoggingSettings(SettingsInterface& si);

	static void LogCPUCapabilities();
	static void InitializeCPUProviders();
	static void ShutdownCPUProviders();
	static void UpdateCPUImplementations();

	static void ApplyGameFixes();
	static bool UpdateGameSettingsLayer();
	static void CheckForConfigChanges(const Pcsx2Config& old_config);
	static void CheckForCPUConfigChanges(const Pcsx2Config& old_config);
	static void CheckForGSConfigChanges(const Pcsx2Config& old_config);
	static void CheckForEmulationSpeedConfigChanges(const Pcsx2Config& old_config);
	static void CheckForPatchConfigChanges(const Pcsx2Config& old_config);
	static void CheckForDEV9ConfigChanges(const Pcsx2Config& old_config);
	static void CheckForMemoryCardConfigChanges(const Pcsx2Config& old_config);
	static void CheckForMiscConfigChanges(const Pcsx2Config& old_config);
	static void EnforceAchievementsChallengeModeSettings();
	static void LogUnsafeSettingsToConsole(const std::string& messages);
	static void WarnAboutUnsafeSettings();

	static bool AutoDetectSource(const std::string& filename, Error* error = nullptr);
	static void UpdateDiscDetails(bool booting);
	static void ClearDiscDetails();
	static void HandleELFChange(bool verbose_patches_if_changed);
	static void UpdateELFInfo(std::string elf_path);
	static void ClearELFInfo();
	static void ReportGameChangeToHost();
	static bool HasBootedELF();
	static bool HasValidOrInitializingVM();
	static void PrecacheCDVDFile();

	static std::string GetCurrentSaveStateFileName(s32 slot, bool backup = false);
	static bool DoLoadState(const char* filename, Error* error = nullptr);
	static void DoSaveState(const char* filename, s32 slot_for_message, bool zip_on_thread, bool backup_old_state, std::function<void(const std::string&)> error_callback);
	static void ZipSaveState(std::unique_ptr<ArchiveEntryList> elist,
		std::unique_ptr<SaveStateScreenshotData> screenshot, const char* filename,
		s32 slot_for_message, std::function<void(const std::string&)> error_callback);
	static void ZipSaveStateOnThread(std::unique_ptr<ArchiveEntryList> elist,
		std::unique_ptr<SaveStateScreenshotData> screenshot, std::string filename,
		s32 slot_for_message, std::function<void(const std::string&)> error_callback);

	static void LoadSettings();
	static void LoadCoreSettings(SettingsInterface& si);
	static void ApplyCoreSettings();
	static void LoadInputBindings(SettingsInterface& si, std::unique_lock<std::mutex>& lock);
	static bool HasAnyBindingsForPad(const SettingsInterface& si, u32 port);
	static void WarnAboutUnconfiguredController();
	static void UpdateInhibitScreensaver(bool allow);
	static void AccumulateSessionPlaytime();
	static void ResetResumeTimestamp();
	static void SaveSessionTime(const std::string& prev_serial);
	static void ReloadPINE();

	static float GetTargetSpeedForLimiterMode(LimiterModeType mode);
	static void ResetFrameLimiter();

	static void SetTimerResolutionIncreased(bool enabled);
	static void SetHardwareDependentDefaultSettings(SettingsInterface& si);
	static void EnsureCPUInfoInitialized();
	static void SetEmuThreadAffinities();

	static void InitializeDiscordPresence();
	static void ShutdownDiscordPresence();
	static void PollDiscordPresence();
} // namespace VMManager

static constexpr u32 SETTINGS_VERSION = 1;

static std::unique_ptr<INISettingsInterface> s_game_settings_interface;
static std::unique_ptr<INISettingsInterface> s_input_settings_interface;

static bool s_log_block_system_console = false;
static bool s_log_force_file_log = false;

static std::atomic<VMState> s_state{VMState::Shutdown};
static bool s_cpu_implementation_changed = false;
static Threading::ThreadHandle s_vm_thread_handle;

static std::deque<std::thread> s_save_state_threads;
static std::mutex s_save_state_threads_mutex;

static std::recursive_mutex s_info_mutex;
static std::string s_disc_serial;
static std::string s_disc_elf;
static std::string s_disc_version;
static std::string s_title;
static std::string s_title_en_search;
static std::string s_title_en_replace;
static std::string s_cur_region;
static u32 s_disc_crc = 0;
static u32 s_current_crc = 0;
static u32 s_elf_entry_point = 0xFFFFFFFFu;
static std::string s_elf_path;
static std::pair<u32, u32> s_elf_text_range;
static bool s_elf_executed = false;
static std::string s_elf_override;
static std::string s_game_settings_override;
static std::string s_input_profile_name;
static u32 s_frame_advance_count = 0;
static bool s_fast_boot_requested = false;
static bool s_gs_open_on_initialize = false;
static bool s_thread_affinities_set = false;

static LimiterModeType s_limiter_mode = LimiterModeType::Nominal;
static s64 s_limiter_ticks_per_frame = 0;
static u64 s_limiter_frame_start = 0;
static float s_target_speed = 0.0f;
static bool s_target_speed_can_sync_to_host = false;
static bool s_target_speed_synced_to_host = false;
static bool s_use_vsync_for_timing = false;

static u64 s_session_resume_timestamp = 0;
static u64 s_session_accumulated_playtime = 0;

static bool s_screensaver_inhibited = false;

static bool s_discord_presence_active = false;
static time_t s_discord_presence_time_epoch = 0;
static const char* s_discord_presence_app_id = "1458595419499139094";
static const char* s_discord_presence_large_image_key = "4k-pcsx2";
static const char* s_discord_presence_large_image_text = "PCSX2_AX";

extern R5900cpu GSDumpReplayerCpu;

bool VMManager::PerformEarlyHardwareChecks(const char** error)
{
#define COMMON_DOWNLOAD_MESSAGE "PCSX2_AX prototype build foundation."

#if defined(ARCH_X86)
	cpuinfo_initialize();

	if (!cpuinfo_has_x86_sse4_1())
	{
		*error =
			"PCSX2 requires the Streaming SIMD 4.1 Extensions instruction set, which your CPU does not support.\n\n"
			COMMON_DOWNLOAD_MESSAGE;
		return false;
	}

#if _M_SSE >= 0x0501
	if (!cpuinfo_has_x86_avx2())
	{
		*error = "This build of PCSX2 requires the Advanced Vector Extensions 2 instruction set, which your CPU does "
				 "not support.\n\n"
				 COMMON_DOWNLOAD_MESSAGE;
		return false;
	}
#endif
#elif defined(ARCH_ARM64)
	const size_t runtime_host_page_size = HostSys::GetRuntimePageSize();
	if (__pagesize != runtime_host_page_size)
	{
		*error = "Page size mismatch. This build cannot run on your system.\n\n" COMMON_DOWNLOAD_MESSAGE;
		return false;
	}
#endif

#undef COMMON_DOWNLOAD_MESSAGE
	return true;
}

VMState VMManager::GetState()
{
	return s_state.load(std::memory_order_acquire);
}

void VMManager::AccumulateSessionPlaytime()
{
	s_session_accumulated_playtime += static_cast<u64>(Common::Timer::GetCurrentValue()) - s_session_resume_timestamp;
}

void VMManager::ResetResumeTimestamp()
{
	s_session_resume_timestamp = static_cast<u64>(Common::Timer::GetCurrentValue());
}

void VMManager::SetState(VMState state)
{
	const VMState old_state = s_state.load(std::memory_order_acquire);
	pxAssert(state != VMState::Initializing && state != VMState::Shutdown);
	SetTimerResolutionIncreased(state == VMState::Running);
	s_state.store(state, std::memory_order_release);

	if (state != VMState::Stopping && (state == VMState::Paused || old_state == VMState::Paused))
	{
		const bool paused = (state == VMState::Paused);
		if (paused)
		{
			if (THREAD_VU1)
				vu1Thread.WaitVU();
			MTGS::WaitGS(false);
			InputManager::PauseVibration();
		}
		else
		{
			PerformanceMetrics::Reset();
			ResetFrameLimiter();
		}

		SPU2::SetOutputPaused(paused);
		Achievements::OnVMPaused(paused);

		UpdateInhibitScreensaver(!paused && EmuConfig.InhibitScreensaver);

		if (paused)
		{
			Host::OnVMPaused();
			AccumulateSessionPlaytime();
		}
		else
		{
			FullscreenUI::OnVMResumed();
			Host::OnVMResumed();
			ResetResumeTimestamp();
		}
	}
	else if (state == VMState::Stopping && old_state == VMState::Running)
	{
		Cpu->ExitExecution();
	}
}

bool VMManager::HasValidVM()
{
	const VMState state = s_state.load(std::memory_order_acquire);
	return (state >= VMState::Running && state <= VMState::Resetting);
}

bool VMManager::HasValidOrInitializingVM()
{
	const VMState state = s_state.load(std::memory_order_acquire);
	return (state >= VMState::Initializing && state <= VMState::Resetting);
}

std::string VMManager::GetDiscPath()
{
	std::unique_lock lock(s_info_mutex);
	return pcsx2_ax::vm_bridge::get_media_path();
}

std::string VMManager::GetDiscSerial()
{
	std::unique_lock lock(s_info_mutex);
	return s_disc_serial;
}

std::string VMManager::GetDiscELF()
{
	std::unique_lock lock(s_info_mutex);
	return s_disc_elf;
}

std::string VMManager::GetTitle(bool prefer_en)
{
	std::unique_lock lock(s_info_mutex);
	std::string out = s_title;
	if (prefer_en && !s_title_en_search.empty())
	{
		const size_t pos = out.find(s_title_en_search);
		if (pos != std::string::npos)
			out.replace(pos, s_title_en_search.size(), s_title_en_replace);
	}
	return out;
}

u32 VMManager::GetDiscCRC()
{
	std::unique_lock lock(s_info_mutex);
	return s_disc_crc;
}

std::string VMManager::GetDiscVersion()
{
	std::unique_lock lock(s_info_mutex);
	return s_disc_version;
}

u32 VMManager::GetCurrentCRC()
{
	return s_current_crc;
}

const std::string& VMManager::GetCurrentELF()
{
	return s_elf_path;
}

bool VMManager::Internal::CPUThreadInitialize()
{
	Threading::SetNameOfCurrentThread("CPU Thread");
	PerformanceMetrics::SetCPUThread(Threading::ThreadHandle::GetForCallingThread());

#ifdef _WIN32
	const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		Host::ReportErrorAsync("Error", fmt::format("CoInitializeEx() failed: {:08X}", hr));
		return false;
	}
#endif

	FPControlRegister::SetCurrent(FPControlRegister::GetDefault());

	if (!cpuinfo_initialize())
		Console.Error("cpuinfo_initialize() failed.");

#ifdef _M_X86
	x86Emitter::use_avx = g_cpu.vectorISA >= ProcessorFeatures::VectorISA::AVX;
#endif

	LogCPUCapabilities();

	if (!SysMemory::Allocate())
	{
		Host::ReportErrorAsync("Error", "Failed to allocate VM memory.");
		return false;
	}

	InitializeCPUProviders();
	USBinit();
	LoadSettings();

	if (EmuConfig.Achievements.Enabled)
		Achievements::Initialize();

	ReloadPINE();

	if (EmuConfig.EnableDiscordPresence)
		InitializeDiscordPresence();

	if (Host::GetBaseBoolSettingValue("UI", "ShowAdvancedSettings", false))
		Console.Warning("Settings: Advanced Settings are enabled.");

	return true;
}

void VMManager::Internal::CPUThreadShutdown()
{
	ShutdownDiscordPresence();
	PINEServer::Deinitialize();
	Achievements::Shutdown(false);
	InputManager::CloseSources();
	WaitForSaveStateFlush();

	PerformanceMetrics::SetCPUThread(Threading::ThreadHandle());

	USBshutdown();
	MTGS::ShutdownThread();
	GSJoinSnapshotThreads();
	ShutdownCPUProviders();
	SysMemory::Release();

#ifdef _WIN32
	CoUninitialize();
#endif

	Log::SetFileOutputLevel(LOGLEVEL_NONE, std::string());
	R5900SymbolImporter.ShutdownWorkerThread();
}

void VMManager::Internal::SetFileLogPath(std::string path)
{
	s_log_force_file_log = Log::SetFileOutputLevel(LOGLEVEL_DEBUG, std::move(path));
}

void VMManager::Internal::SetBlockSystemConsole(bool block)
{
	s_log_block_system_console = block;
}

void VMManager::UpdateLoggingSettings(SettingsInterface& si)
{
#ifdef _DEBUG
	constexpr LOGLEVEL level = LOGLEVEL_DEBUG;
#else
	const LOGLEVEL level = (IsDevBuild || si.GetBoolValue("Logging", "EnableVerbose", false)) ? LOGLEVEL_DEV : LOGLEVEL_INFO;
#endif

	const bool system_console_enabled = !s_log_block_system_console && si.GetBoolValue("Logging", "EnableSystemConsole", false);
	const bool log_window_enabled = !s_log_block_system_console && si.GetBoolValue("Logging", "EnableLogWindow", false);
	const bool file_logging_enabled = s_log_force_file_log || si.GetBoolValue("Logging", "EnableFileLogging", true);

	if (system_console_enabled != Log::IsConsoleOutputEnabled())
		Log::SetConsoleOutputLevel(system_console_enabled ? level : LOGLEVEL_NONE);

#ifdef _WIN32
	const bool debug_console_enabled = IsDebuggerPresent() && si.GetBoolValue("Logging", "EnableDebugConsole", false);
	Log::SetDebugOutputLevel(debug_console_enabled ? level : LOGLEVEL_NONE);
#else
	constexpr bool debug_console_enabled = false;
#endif

	const bool timestamps_enabled = si.GetBoolValue("Logging", "EnableTimestamps", true);
	Log::SetTimestampsEnabled(timestamps_enabled);

	const bool any_logging_sinks = system_console_enabled || log_window_enabled || file_logging_enabled || debug_console_enabled;

	ConsoleLogging.eeConsole.Enabled = any_logging_sinks && si.GetBoolValue("Logging", "EnableEEConsole", false);
	ConsoleLogging.iopConsole.Enabled = any_logging_sinks && si.GetBoolValue("Logging", "EnableIOPConsole", false);
	TraceLogging.IOP.R3000A.Enabled = true;
	TraceLogging.IOP.COP2.Enabled = true;
	TraceLogging.IOP.Memory.Enabled = true;
	TraceLogging.SIF.Enabled = true;

	ConsoleLogging.recordingConsole.Enabled = any_logging_sinks && si.GetBoolValue("Logging", "EnableInputRecordingLogs", true);
	ConsoleLogging.controlInfo.Enabled = any_logging_sinks && si.GetBoolValue("Logging", "EnableControllerLogs", false);

	EmuConfig.Trace.SyncToConfig();
	if (file_logging_enabled != Log::IsFileOutputEnabled() || (EmuConfig.Trace.Enabled && Log::GetMaxLevel() < LOGLEVEL_TRACE))
	{
		std::string path = Path::Combine(EmuFolders::Logs, "emulog.txt");
		Log::SetFileOutputLevel(file_logging_enabled ? (EmuConfig.Trace.Enabled ? LOGLEVEL_TRACE : level) : LOGLEVEL_NONE, std::move(path));
	}
}

void VMManager::SetDefaultLoggingSettings(SettingsInterface& si)
{
	si.SetBoolValue("Logging", "EnableSystemConsole", false);
	si.SetBoolValue("Logging", "EnableFileLogging", true);
	si.SetBoolValue("Logging", "EnableTimestamps", true);
	si.SetBoolValue("Logging", "EnableEESIOInput", false);
	si.SetBoolValue("Logging", "EnableVerbose", false);
	si.SetBoolValue("Logging", "EnableEEConsole", false);
	si.SetBoolValue("Logging", "EnableIOPConsole", false);
	si.SetBoolValue("Logging", "EnableInputRecordingLogs", true);
	si.SetBoolValue("Logging", "EnableControllerLogs", false);

	EmuConfig.Trace.Enabled = false;
	EmuConfig.Trace.EE.bitset = 0;
	EmuConfig.Trace.IOP.bitset = 0;
	EmuConfig.Trace.MISC.bitset = 0;
}

bool VMManager::Internal::CheckSettingsVersion()
{
	SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();
	uint settings_version = 0;
	return (bsi->GetUIntValue("UI", "SettingsVersion", &settings_version) && settings_version == SETTINGS_VERSION);
}

void VMManager::Internal::LoadStartupSettings()
{
	SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();
	EmuFolders::LoadConfig(*bsi);
	EmuFolders::EnsureFoldersExist();
	UpdateLoggingSettings(*bsi);

#ifdef ENABLE_RAINTEGRATION
	if (Host::GetBaseBoolSettingValue("Achievements", "UseRAIntegration", false))
		Achievements::SwitchToRAIntegration();
#endif
}

void VMManager::SetDefaultSettings(
	SettingsInterface& si, bool folders, bool core, bool controllers, bool hotkeys, bool ui)
{
	FPControlRegisterBackup fpcr_backup(FPControlRegister::GetDefault());

	if (si.GetUIntValue("UI", "SettingsVersion", 0u) != SETTINGS_VERSION)
		si.SetUIntValue("UI", "SettingsVersion", SETTINGS_VERSION);

	if (folders)
		EmuFolders::SetDefaults(si);
	if (core)
	{
		Pcsx2Config temp_config;
		SettingsSaveWrapper ssw(si);
		temp_config.LoadSave(ssw);
		si.SetBoolValue("EmuCore", "EnableFastBoot", true);
		SetHardwareDependentDefaultSettings(si);
		SetDefaultLoggingSettings(si);
	}
	if (controllers)
	{
		USB::SetDefaultConfiguration(&si);
	}
	if (hotkeys)
	{
	}
	if (ui)
		Host::SetDefaultUISettings(si);
}

void VMManager::LoadSettings()
{
	FPControlRegisterBackup fpcr_backup(FPControlRegister::GetDefault());

	std::unique_lock<std::mutex> lock = Host::GetSettingsLock();
	SettingsInterface* si = Host::GetSettingsInterface();
	LoadCoreSettings(*si);
	Host::LoadSettings(*si, lock);
	InputManager::ReloadSources(*si, lock);
	LoadInputBindings(*si, lock);
	UpdateLoggingSettings(*si);

	if (HasValidOrInitializingVM())
	{
		WarnAboutUnsafeSettings();
		ApplyGameFixes();
	}
}

void VMManager::ReloadInputSources()
{
	FPControlRegisterBackup fpcr_backup(FPControlRegister::GetDefault());
	std::unique_lock<std::mutex> lock = Host::GetSettingsLock();
	SettingsInterface* si = Host::GetSettingsInterface();
	InputManager::ReloadSources(*si, lock);

	if (HasValidVM())
		LoadInputBindings(*si, lock);
}

void VMManager::ReloadInputBindings(bool force)
{
	if (!force && !HasValidVM())
		return;

	FPControlRegisterBackup fpcr_backup(FPControlRegister::GetDefault());
	std::unique_lock<std::mutex> lock = Host::GetSettingsLock();
	SettingsInterface* si = Host::GetSettingsInterface();
	LoadInputBindings(*si, lock);
}

void VMManager::LoadCoreSettings(SettingsInterface& si)
{
	SettingsLoadWrapper slw(si);
	EmuConfig.LoadSave(slw);
	Patch::ApplyPatchSettingOverrides();
	EnforceAchievementsChallengeModeSettings();
	EmuConfig.GS.MaskUserHacks();
	EmuConfig.GS.MaskUpscalingHacks();

	if (GSDumpReplayer::IsReplayingDump())
	{
		EmuConfig.Speedhacks.vuThread = false;
		GSDumpReplayer::SetFrameRange(EmuConfig.GS.DumpReplayUseFrameRange,
			EmuConfig.GS.DumpReplayFrameStart, EmuConfig.GS.DumpReplayFrameEnd);
		GSDumpReplayer::SetLoopCount(EmuConfig.GS.DumpReplayLoopCount);
	}
}

void VMManager::LoadInputBindings(SettingsInterface& si, std::unique_lock<std::mutex>& lock)
{
	if (SettingsInterface* isi = Host::Internal::GetInputSettingsLayer())
	{
		const bool use_profile_hotkeys = isi->GetBoolValue("Pad", "UseProfileHotkeyBindings", false);
		if (use_profile_hotkeys)
		{
			InputManager::ReloadBindings(si, *isi, *isi, true, true);
		}
		else
		{
			Host::Internal::SetInputSettingsLayer(nullptr, lock);
			InputManager::ReloadBindings(si, *isi, si, true, false);
			Host::Internal::SetInputSettingsLayer(s_input_settings_interface.get(), lock);
		}
	}
	else
	{
		InputManager::ReloadBindings(si, si, si, false, false);
	}
}

bool VMManager::HasAnyBindingsForPad(const SettingsInterface& /*si*/, u32 /*port*/)
{
	return true;
}

void VMManager::WarnAboutUnconfiguredController()
{
}

void VMManager::ApplyGameFixes()
{
	if (!HasBootedELF() && !GSDumpReplayer::IsReplayingDump())
	{
		EmuConfig.Gamefixes.InstantDMAHack = true;
		EmuConfig.GS.ManualUserHacks = false;
		return;
	}

	const GameDatabaseSchema::GameEntry* game = GameDatabase::findGame(s_disc_serial);
	if (!game)
		return;

	game->applyGameFixes(EmuConfig, EmuConfig.EnableGameFixes);
	game->applyGSHardwareFixes(EmuConfig.GS);
	EmuConfig.GS.MaskUpscalingHacks();
}

void VMManager::ApplySettings()
{
	Console.WriteLn("Applying settings...");

	if (GetState() == VMState::Running)
	{
		if (THREAD_VU1)
			vu1Thread.WaitVU();
		MTGS::WaitGS(false);
	}

	Pcsx2Config old_config(std::move(EmuConfig));
	EmuConfig = Pcsx2Config();
	EmuConfig.CopyRuntimeConfig(old_config);
	LoadSettings();
	CheckForConfigChanges(old_config);
}

void VMManager::ApplyCoreSettings()
{
	pxAssertRel(HasValidOrInitializingVM(), "Reloading core settings requires a valid VM.");
	Console.WriteLn("Applying core settings...");

	if (GetState() == VMState::Running)
	{
		if (THREAD_VU1)
			vu1Thread.WaitVU();
		MTGS::WaitGS(false);
	}

	Pcsx2Config old_config(std::move(EmuConfig));
	EmuConfig = Pcsx2Config();
	EmuConfig.CopyRuntimeConfig(old_config);

	{
		FPControlRegisterBackup fpcr_backup(FPControlRegister::GetDefault());
		std::unique_lock<std::mutex> lock = Host::GetSettingsLock();
		LoadCoreSettings(*Host::GetSettingsInterface());
		WarnAboutUnsafeSettings();
		ApplyGameFixes();
	}

	CheckForConfigChanges(old_config);
}

bool VMManager::ReloadGameSettings()
{
	if (!UpdateGameSettingsLayer())
		return false;

	Patch::UpdateActivePatches(true, false, true, HasValidVM());
	ApplySettings();
	return true;
}

std::string VMManager::GetGameSettingsPath(const std::string_view game_serial, u32 game_crc)
{
	if (!s_game_settings_override.empty())
		return s_game_settings_override;

	const std::string sanitized_serial(Path::SanitizeFileName(game_serial));
	return game_serial.empty()
		? Path::Combine(EmuFolders::GameSettings, fmt::format("{:08X}.ini", game_crc))
		: Path::Combine(EmuFolders::GameSettings, fmt::format("{}_{:08X}.ini", sanitized_serial, game_crc));
}

std::string VMManager::GetDiscOverrideFromGameSettings(const std::string& elf_path)
{
	std::string iso_path;
	ElfObject elfo;
	if (!elfo.OpenFile(elf_path, false, nullptr))
		return iso_path;

	const u32 crc = elfo.GetCRC();
	if (crc != 0)
	{
		INISettingsInterface si(GetGameSettingsPath(std::string_view(), crc));
		if (si.Load())
		{
			iso_path = si.GetStringValue("EmuCore", "DiscPath");
			if (!iso_path.empty())
				Console.WriteLn(fmt::format("Disc override for ELF at '{}' is '{}'", elf_path, iso_path));
		}
	}

	return iso_path;
}

bool VMManager::AreGameSettingsPresent(const std::string_view game_serial, u32 game_crc)
{
	std::string filename(VMManager::GetGameSettingsPath(game_serial, game_crc));
	if (!FileSystem::FileExists(filename.c_str()))
		filename = VMManager::GetGameSettingsPath({}, game_crc);
	return FileSystem::FileExists(filename.c_str());
}

std::string VMManager::GetInputProfilePath(const std::string_view name)
{
	return Path::Combine(EmuFolders::InputProfiles, fmt::format("{}.ini", name));
}

std::string VMManager::GetDebuggerSettingsFilePath(const std::string_view game_serial, u32 game_crc)
{
	if (!game_serial.empty() && game_crc != 0)
	{
		auto lock = Host::GetSettingsLock();
		return Path::Combine(EmuFolders::DebuggerSettings, fmt::format("{}_{:08X}.json", game_serial, game_crc));
	}
	return {};
}

std::string VMManager::GetDebuggerSettingsFilePathForCurrentGame()
{
	return GetDebuggerSettingsFilePath(s_disc_serial, s_current_crc);
}

void VMManager::Internal::UpdateEmuFolders()
{
	const std::string old_cheats_directory(EmuFolders::Cheats);
	const std::string old_patches_directory(EmuFolders::Patches);
	const std::string old_textures_directory(EmuFolders::Textures);
	const std::string old_videos_directory(EmuFolders::Videos);

	auto lock = Host::GetSettingsLock();
	EmuFolders::LoadConfig(*Host::Internal::GetBaseSettingsLayer());
	EmuFolders::EnsureFoldersExist();

	if (VMManager::HasValidVM())
	{
		if (EmuFolders::Cheats != old_cheats_directory || EmuFolders::Patches != old_patches_directory)
			Patch::ReloadPatches(s_disc_serial, s_current_crc, true, false, true, true);

		if (EmuFolders::Textures != old_textures_directory)
		{
			MTGS::RunOnGSThread([]() {
				if (VMManager::HasValidVM())
					GSTextureReplacements::ReloadReplacementMap();
			});
		}

		if (EmuFolders::Videos != old_videos_directory)
		{
			if (VMManager::HasValidVM())
				MTGS::RunOnGSThread(&GSEndCapture);
		}
	}
}

void VMManager::RequestDisplaySize(float scale /*= 0.0f*/)
{
	int iwidth = 0, iheight = 0;
	GSgetInternalResolution(&iwidth, &iheight);
	if (iwidth <= 0 || iheight <= 0)
		return;

	float x_scale = 1.0f;
	switch (GSConfig.AspectRatio)
	{
		case AspectRatioType::RAuto4_3_3_2:
			if (EmuConfig.CurrentCustomAspectRatio > 0.f)
				x_scale = EmuConfig.CurrentCustomAspectRatio / (static_cast<float>(iwidth) / static_cast<float>(iheight));
			else if (GSgetDisplayMode() == GSVideoMode::SDTV_480P)
				x_scale = (3.0f / 2.0f) / (static_cast<float>(iwidth) / static_cast<float>(iheight));
			else
				x_scale = (4.0f / 3.0f) / (static_cast<float>(iwidth) / static_cast<float>(iheight));
			break;
		case AspectRatioType::R4_3:
			x_scale = (4.0f / 3.0f) / (static_cast<float>(iwidth) / static_cast<float>(iheight));
			break;
		case AspectRatioType::R16_9:
			x_scale = (16.0f / 9.0f) / (static_cast<float>(iwidth) / static_cast<float>(iheight));
			break;
		case AspectRatioType::R10_7:
			x_scale = (10.0f / 7.0f) / (static_cast<float>(iwidth) / static_cast<float>(iheight));
			break;
		case AspectRatioType::Stretch:
		default:
			x_scale = 1.0f;
			break;
	}

	float width = static_cast<float>(iwidth) * x_scale;
	float height = static_cast<float>(iheight);

	if (scale != 0.0f)
	{
		scale = (1.0f / GSConfig.UpscaleMultiplier) * scale;
		width *= scale;
		height *= scale;
	}

	iwidth = std::max(static_cast<int>(std::lroundf(width)), 1);
	iheight = std::max(static_cast<int>(std::lroundf(height)), 1);

	Host::RequestResizeHostDisplay(iwidth, iheight);
}

std::string VMManager::GetSerialForGameSettings()
{
	std::unique_lock lock(s_info_mutex);
	return s_elf_override.empty() ? std::string(s_disc_serial) : std::string();
}

bool VMManager::UpdateGameSettingsLayer()
{
	std::unique_ptr<INISettingsInterface> new_interface;
	if (s_disc_crc != 0)
	{
		std::string filename(GetGameSettingsPath(GetSerialForGameSettings(), s_disc_crc));
		if (!FileSystem::FileExists(filename.c_str()))
			filename = GetGameSettingsPath({}, s_disc_crc);

		if (FileSystem::FileExists(filename.c_str()))
		{
			new_interface = std::make_unique<INISettingsInterface>(std::move(filename));
			if (!new_interface->Load())
			{
				Console.Error("Failed to parse game settings ini '%s'", new_interface->GetFileName().c_str());
				new_interface.reset();
			}
		}
	}

	std::string input_profile_name;
	if (new_interface)
		new_interface->GetStringValue("EmuCore", "InputProfileName", &input_profile_name);

	if (!s_game_settings_interface && !new_interface && s_input_profile_name == input_profile_name)
		return false;

	auto lock = Host::GetSettingsLock();
	Host::Internal::SetGameSettingsLayer(new_interface.get(), lock);
	s_game_settings_interface = std::move(new_interface);

	std::unique_ptr<INISettingsInterface> input_interface;
	if (!input_profile_name.empty())
	{
		const std::string filename(GetInputProfilePath(input_profile_name));
		if (FileSystem::FileExists(filename.c_str()))
		{
			input_interface = std::make_unique<INISettingsInterface>(std::move(filename));
			if (!input_interface->Load())
			{
				Console.Error("Failed to parse input profile ini '%s'", input_interface->GetFileName().c_str());
				input_interface.reset();
				input_profile_name = {};
			}
		}
		else
		{
			input_profile_name = {};
		}
	}

	Host::Internal::SetInputSettingsLayer(input_interface.get(), lock);
	s_input_settings_interface = std::move(input_interface);
	s_input_profile_name = std::move(input_profile_name);
	return true;
}

void VMManager::UpdateDiscDetails(bool booting)
{
	const auto bios = pcsx2_ax::vm_bridge::current_bios_info();
	{
		std::unique_lock lock(s_info_mutex);
		const std::string old_serial = std::move(s_disc_serial);
		const u32 old_crc = s_disc_crc;
		bool serial_is_valid = false;
		std::string title;

		if (GSDumpReplayer::IsReplayingDump())
		{
			s_disc_serial = GSDumpReplayer::GetDumpSerial();
			s_disc_crc = GSDumpReplayer::GetDumpCRC();
			s_disc_elf = {};
			s_disc_version = {};
			s_cur_region = "NTSC";
			serial_is_valid = !s_disc_serial.empty();
		}
		else if (pcsx2_ax::vm_bridge::is_media_ready())
		{
			s_disc_serial = Path::GetFileTitle(pcsx2_ax::vm_bridge::get_media_path());
			s_disc_version = fmt::format("{:04X}", bios.rom_version);
			s_cur_region = bios.region_code.empty() ? "NTSC" : std::string(bios.region_code);
			serial_is_valid = !s_disc_serial.empty();
		}
		else if (!s_elf_override.empty())
		{
			s_disc_serial = Path::GetFileTitle(s_elf_override);
			s_disc_version = {};
			s_disc_crc = 0;
			s_cur_region = "NTSC";
		}
		else
		{
			s_disc_serial = bios.region_code.empty() ? "BIOS" : std::string(bios.region_code);
			s_disc_version = fmt::format("{:04X}", bios.rom_version);
			s_disc_crc = bios.bios_crc;
			s_cur_region = bios.region_code.empty() ? "NTSC" : std::string(bios.region_code);
			title = fmt::format("Runtime ({})", s_disc_serial);
		}

		if (!booting && s_disc_serial == old_serial && s_disc_crc == old_crc)
			return;

		SaveSessionTime(old_serial);

		s_title_en_search.clear();
		s_title_en_replace.clear();
		std::string custom_title = GameList::GetCustomTitleForPath(pcsx2_ax::vm_bridge::get_media_path());
		if (serial_is_valid)
		{
			if (const GameDatabaseSchema::GameEntry* game = GameDatabase::findGame(s_disc_serial))
			{
				if (!game->name_en.empty())
				{
					s_title_en_search = game->name;
					s_title_en_replace = game->name_en;
				}

				std::string game_title = custom_title.empty() ? game->name : std::move(custom_title);
				title = !s_elf_override.empty()
					? fmt::format("{} [{}]", game_title, Path::GetFileTitle(s_elf_override))
					: std::move(game_title);
			}
		}

		if (title.empty())
			title = std::move(custom_title);

		if (title.empty())
		{
			if (!s_disc_serial.empty())
				title = fmt::format("{} [?]", s_disc_serial);
			else if (!s_disc_elf.empty())
				title = fmt::format("{} [?]", Path::GetFileName(IsoReader::RemoveVersionIdentifierFromPath(s_disc_elf)));
			else
				title = TRANSLATE_STR("VMManager", "Unknown Game");
		}

		s_title = std::move(title);
	}

	UpdateGameSettingsLayer();
	ApplySettings();
	Patch::ReloadPatches(s_disc_serial, HasBootedELF() ? s_current_crc : 0, true, true, false, false);

	ReportGameChangeToHost();
	if (MTGS::IsOpen())
		MTGS::GameChanged();

	if (!GSDumpReplayer::IsReplayingDump())
	{
		Achievements::GameChanged(s_disc_crc, s_current_crc);
		ReloadPINE();
		UpdateDiscordPresence(s_state.load(std::memory_order_relaxed) == VMState::Initializing);
	}
}

void VMManager::ClearDiscDetails()
{
	std::unique_lock lock(s_info_mutex);
	s_disc_crc = 0;
	s_title = {};
	s_disc_version = {};
	s_disc_elf = {};
	s_disc_serial = {};
}

void VMManager::HandleELFChange(bool verbose_patches_if_changed)
{
	const u32 crc_to_report = HasBootedELF() ? s_current_crc : 0;

	ReportGameChangeToHost();
	Achievements::GameChanged(s_disc_crc, crc_to_report);

	Console.WriteLn(Color_StrongOrange, fmt::format("ELF changed, active CRC {:08X} ({})", crc_to_report, s_elf_path));
	Patch::ReloadPatches(s_disc_serial, crc_to_report, false, false, false, verbose_patches_if_changed);
	ApplyCoreSettings();
}

void VMManager::UpdateELFInfo(std::string elf_path)
{
	if (elf_path.empty())
	{
		s_elf_path = {};
		s_elf_text_range = {};
		s_elf_entry_point = 0xFFFFFFFFu;
		s_current_crc = 0;
		return;
	}

	ElfObject elfo;
	if (!elfo.OpenFile(elf_path, false, nullptr))
	{
		Console.Error(fmt::format("Failed to read ELF being loaded: {}", elf_path));
		s_elf_path = {};
		s_elf_text_range = {};
		s_elf_entry_point = 0xFFFFFFFFu;
		s_current_crc = 0;
		return;
	}

	elfo.LoadHeaders();
	s_current_crc = elfo.GetCRC();
	s_elf_entry_point = elfo.GetEntryPoint();
	s_elf_text_range = elfo.GetTextRange();
	s_elf_path = std::move(elf_path);

	R5900SymbolImporter.OnElfChanged(elfo.ReleaseData(), s_elf_path);
}

void VMManager::ClearELFInfo()
{
	s_current_crc = 0;
	s_elf_executed = false;
	s_elf_text_range = {};
	s_elf_path = {};
	s_elf_entry_point = 0xFFFFFFFFu;
}

void VMManager::ReportGameChangeToHost()
{
	const std::string& disc_path = pcsx2_ax::vm_bridge::get_media_path();
	const u32 crc_to_report = HasBootedELF() ? s_current_crc : 0;
	const bool prefer_english = Host::GetBaseBoolSettingValue("UI", "PreferEnglishGameList", false);
	const std::string game_title = GetTitle(prefer_english);
	FullscreenUI::GameChanged(disc_path, s_disc_serial, game_title, s_disc_crc, crc_to_report);
	Host::OnGameChanged(game_title, s_elf_override, disc_path, s_disc_serial, s_disc_crc, crc_to_report);
}

bool VMManager::HasBootedELF()
{
	return s_current_crc != 0 && s_elf_executed;
}

bool VMManager::AutoDetectSource(const std::string& filename, Error* error)
{
	if (filename.empty())
	{
		pcsx2_ax::vm_bridge::set_media_path({}, false);
		return true;
	}

	if (!FileSystem::FileExists(filename.c_str()))
	{
		Error::SetStringFmt(error, TRANSLATE_FS("VMManager", "Requested filename '{}' does not exist."), filename);
		return false;
	}

	if (IsGSDumpFileName(filename))
	{
		pcsx2_ax::vm_bridge::set_media_path({}, false);
		return GSDumpReplayer::Initialize(filename.c_str(), error);
	}

	if (IsElfFileName(filename))
	{
		std::string disc_path = GetDiscOverrideFromGameSettings(filename);
		if (!disc_path.empty())
			pcsx2_ax::vm_bridge::set_media_path(std::move(disc_path), true);
		else
			pcsx2_ax::vm_bridge::set_media_path({}, false);

		s_elf_override = filename;
		return true;
	}

	pcsx2_ax::vm_bridge::set_media_path(filename, true);
	return true;
}

void VMManager::PrecacheCDVDFile()
{
}

void VMManager::InitializeAsync(
	const VMBootParameters& boot_params,
	VMBootHardcoreDisableCallback hardcore_disable_callback,
	VMBootDoneCallback done_callback)
{
	Error error;
	const VMBootResult result = VMManager::Initialize(boot_params, &error);

	if (result == VMBootResult::PromptDisableHardcoreMode)
	{
		std::string reason = DebugInterface::getPauseOnEntry()
			? TRANSLATE_STR("VMManager", "Boot and Debug")
			: TRANSLATE_STR("VMManager", "Resuming state");

		hardcore_disable_callback(reason,
			[boot_params, done_callback = std::move(done_callback)]() {
				VMBootParameters new_boot_params = boot_params;
				new_boot_params.disable_achievements_hardcore_mode = true;

				Error retry_error;
				const VMBootResult retry_result = VMManager::Initialize(new_boot_params, &retry_error);
				done_callback(retry_result, retry_error);
			});

		return;
	}

	done_callback(result, error);
}

VMBootResult VMManager::Initialize(const VMBootParameters& boot_params, Error* error)
{
	const Common::Timer init_timer;
	if (s_state.load(std::memory_order_acquire) != VMState::Shutdown)
	{
		Error::SetString(error, TRANSLATE_STR("VMManager", "The virtual machine is already running."));
		return VMBootResult::StartupFailure;
	}

	Host::CancelGameListRefresh();

	s_state.store(VMState::Initializing, std::memory_order_release);
	s_vm_thread_handle = Threading::ThreadHandle::GetForCallingThread();
	Host::OnVMStarting();
	VMManager::Internal::ResetVMHotkeyState();

	ScopedGuard close_state = [] {
		if (GSDumpReplayer::IsReplayingDump())
			GSDumpReplayer::Shutdown();

		pcsx2_ax::vm_bridge::shutdown_mode_subsystems();
		s_elf_override = {};
		s_game_settings_override = {};
		ClearELFInfo();
		ClearDiscDetails();

		Achievements::GameChanged(0, 0);
		FullscreenUI::GameChanged(s_title, std::string(), s_disc_serial, 0, 0);
		UpdateDiscordPresence(true);
		Host::OnGameChanged(s_title, std::string(), std::string(), s_disc_serial, 0, 0);

		UpdateGameSettingsLayer();
		s_state.store(VMState::Shutdown, std::memory_order_release);
		Host::OnVMDestroyed();
		ApplySettings();
	};

	std::string state_to_load;
	s_elf_override = boot_params.elf_override;
	if (!boot_params.save_state.empty())
		state_to_load = boot_params.save_state;

	if (boot_params.state_index.has_value())
	{
		if (boot_params.filename.empty())
		{
			Error::SetString(error,
				TRANSLATE_STR("VMManager", "Cannot load an indexed save state without a boot filename."));
			return VMBootResult::StartupFailure;
		}

		state_to_load = GetSaveStateFileName(boot_params.filename.c_str(), boot_params.state_index.value());
		if (state_to_load.empty())
		{
			Error::SetString(error,
				TRANSLATE_STR("VMManager", "Could not resolve path for indexed save state load."));
			return VMBootResult::StartupFailure;
		}
	}

	if (!AutoDetectSource(boot_params.filename, error))
		return VMBootResult::StartupFailure;

	if (!pcsx2_ax::vm_bridge::init_mode_subsystems())
	{
		Error::SetString(error, "Failed to initialize mode adapter subsystems.");
		return VMBootResult::StartupFailure;
	}

	if (!boot_params.game_config.empty())
	{
		if (!StringUtil::compareNoCase(Path::GetExtension(boot_params.game_config), "ini"))
		{
			Error::SetStringFmt(error,
				TRANSLATE_FS("VMManager", "Requested game config '{}' is not an INI file."), boot_params.game_config);
			return VMBootResult::StartupFailure;
		}
		if (!FileSystem::FileExists(boot_params.game_config.c_str()))
		{
			Error::SetStringFmt(error,
				TRANSLATE_FS("VMManager", "Requested game config '{}' does not exist."), boot_params.game_config);
			return VMBootResult::StartupFailure;
		}
		s_game_settings_override = boot_params.game_config;
	}

	UpdateDiscDetails(true);

	s_fast_boot_requested =
		(boot_params.fast_boot.value_or(static_cast<bool>(EmuConfig.EnableFastBoot)) || !s_elf_override.empty()) &&
		(pcsx2_ax::vm_bridge::is_media_ready() || !s_elf_override.empty()) &&
		!GSDumpReplayer::IsReplayingDump();

	if (boot_params.disable_achievements_hardcore_mode || GSDumpReplayer::IsReplayingDump())
		Achievements::DisableHardcoreMode();
	else
		Achievements::ResetHardcoreMode(true);

	if (Achievements::IsHardcoreModeActive() && (!state_to_load.empty() || DebugInterface::getPauseOnEntry()))
		return VMBootResult::PromptDisableHardcoreMode;

	if (boot_params.start_unlimited.value_or(false))
		s_limiter_mode = LimiterModeType::Unlimited;
	else if (boot_params.start_turbo.value_or(false))
		s_limiter_mode = LimiterModeType::Turbo;
	else
		s_limiter_mode = LimiterModeType::Nominal;

	s_target_speed = GetTargetSpeedForLimiterMode(s_limiter_mode);
	s_use_vsync_for_timing = false;

	s_cpu_implementation_changed = false;
	UpdateCPUImplementations();
	mmap_ResetBlockTracking();
	memSetExtraMemMode(EmuConfig.Cpu.ExtraMemory);
	Internal::ClearCPUExecutionCaches();
	FPControlRegister::SetCurrent(EmuConfig.Cpu.FPUFPCR);
	memBindConditionalHandlers();
	SysMemory::Reset();
	cpuReset();

	Console.WriteLn("Opening GS...");
	s_gs_open_on_initialize = MTGS::IsOpen();
	if (!s_gs_open_on_initialize && !MTGS::WaitForOpen())
	{
		Error::SetString(error, TRANSLATE_STR("VMManager", "Failed to initialize GS."));
		return VMBootResult::StartupFailure;
	}

	ScopedGuard close_gs = []() {
		if (!s_gs_open_on_initialize)
			MTGS::WaitForClose();
	};

	Console.WriteLn("Opening SPU2...");
	if (!SPU2::Open())
	{
		Error::SetString(error, TRANSLATE_STR("VMManager", "Failed to initialize SPU2."));
		return VMBootResult::StartupFailure;
	}
	ScopedGuard close_spu2(&SPU2::Close);

	Console.WriteLn("Opening DEV9...");
	if (DEV9init() != 0 || DEV9open() != 0)
	{
		Error::SetString(error, TRANSLATE_STR("VMManager", "Failed to initialize DEV9."));
		return VMBootResult::StartupFailure;
	}
	ScopedGuard close_dev9 = []() {
		DEV9close();
		DEV9shutdown();
	};

	Console.WriteLn("Opening USB...");
	if (!USBopen())
	{
		Error::SetString(error, TRANSLATE_STR("VMManager", "Failed to initialize USB."));
		return VMBootResult::StartupFailure;
	}
	ScopedGuard close_usb = []() { USBclose(); };

	Console.WriteLn("Opening FW...");
	if (FWopen() != 0)
	{
		Error::SetString(error, TRANSLATE_STR("VMManager", "Failed to initialize FW."));
		return VMBootResult::StartupFailure;
	}
	ScopedGuard close_fw = []() { FWclose(); };

	close_fw.Cancel();
	close_usb.Cancel();
	close_dev9.Cancel();
	close_spu2.Cancel();
	close_gs.Cancel();
	close_state.Cancel();

	hwReset();

	Console.WriteLn("VM subsystems initialized in %.2f ms", init_timer.GetTimeMilliseconds());
	s_state.store(VMState::Paused, std::memory_order_release);
	Host::OnVMStarted();
	FullscreenUI::OnVMStarted();
	UpdateInhibitScreensaver(EmuConfig.InhibitScreensaver);

	SetEmuThreadAffinities();

	if (!GSDumpReplayer::IsReplayingDump() && !state_to_load.empty())
	{
		if (!DoLoadState(state_to_load.c_str(), error))
		{
			Shutdown(false);
			return VMBootResult::StartupFailure;
		}
	}

	PerformanceMetrics::Clear();
	MTGS::ResetStats();
	return VMBootResult::StartupSuccess;
}

void VMManager::Shutdown(bool save_resume_state)
{
	s_state.store(VMState::Stopping, std::memory_order_release);
	SetTimerResolutionIncreased(false);

	if (THREAD_VU1)
		vu1Thread.WaitVU();
	MTGS::WaitGS();

	if (!GSDumpReplayer::IsReplayingDump() && save_resume_state)
	{
		const std::string resume_file_name(GetCurrentSaveStateFileName(-1));
		if (!resume_file_name.empty())
		{
			DoSaveState(resume_file_name.c_str(), -1, true, false, [](const std::string& error) {
				Host::AddIconOSDMessage("SaveResumeState", ICON_FA_TRIANGLE_EXCLAMATION,
					fmt::format(TRANSLATE_FS("VMManager", "Failed to save resume state: {}"), error), Host::OSD_QUICK_DURATION);
			});
		}
	}

	if (g_InputRecording.isActive())
		g_InputRecording.stop();

	SaveSessionTime(s_disc_serial);
	s_elf_override = {};
	s_game_settings_override = {};
	ClearELFInfo();
	pcsx2_ax::vm_bridge::set_media_path({}, false);

	{
		std::unique_lock lock(s_info_mutex);
		ClearDiscDetails();
	}

	Achievements::GameChanged(0, 0);
	FullscreenUI::GameChanged(s_title, std::string(), s_disc_serial, 0, 0);
	UpdateDiscordPresence(true);
	Host::OnGameChanged(s_title, std::string(), std::string(), s_disc_serial, 0, 0);

	s_fast_boot_requested = false;
	UpdateGameSettingsLayer();
	FPControlRegister::SetCurrent(FPControlRegister::GetDefault());

	Patch::UnloadPatches();
	R3000A::ioman::reset();
	vtlb_Shutdown();
	USBclose();
	SPU2::Close();
	pcsx2_ax::vm_bridge::set_memcard_busy(false);
	DEV9close();
	FWclose();
	pcsx2_ax::vm_bridge::shutdown_mode_subsystems();

	if (s_gs_open_on_initialize)
	{
		MTGS::WaitGS(false, false, false);
		MTGS::ResetGS(true);
		MTGS::GameChanged();
	}
	else
	{
		MTGS::WaitForClose();
	}

	DEV9shutdown();

	if (GSDumpReplayer::IsReplayingDump())
		GSDumpReplayer::Shutdown();

	s_state.store(VMState::Shutdown, std::memory_order_release);
	FullscreenUI::OnVMDestroyed();
	SaveStateSelectorUI::Clear();
	UpdateInhibitScreensaver(false);
	SetEmuThreadAffinities();
	Host::OnVMDestroyed();

	LoadSettings();
}

bool VMManager::RequestReset()
{
	if (pcsx2_ax::vm_bridge::is_memcard_busy())
	{
		Host::AddIconOSDMessage("RequestReset", ICON_FA_TRIANGLE_EXCLAMATION,
			TRANSLATE_STR("VMManager",
				"The memory card is busy, so the reset operation has been cancelled to prevent data loss."),
			Host::OSD_WARNING_DURATION);
		return false;
	}

	VMManager::Reset();
	return true;
}

void VMManager::Reset()
{
	pxAssert(HasValidVM());

	if (s_state.load(std::memory_order_acquire) == VMState::Running)
	{
		s_state.store(VMState::Resetting, std::memory_order_release);
		return;
	}

	if (!Achievements::ConfirmSystemReset())
		return;

	if (!GSDumpReplayer::IsReplayingDump() && Achievements::ResetHardcoreMode(false))
		ApplySettings();

	vu1Thread.WaitVU();
	vu1Thread.Reset();
	MTGS::WaitGS();

	const bool elf_was_changed = (s_current_crc != 0);
	ClearELFInfo();
	if (elf_was_changed)
		HandleELFChange(false);

	Achievements::ResetClient();

	(void)pcsx2_ax::vm_bridge::reset_mode_subsystems();
	mmap_ResetBlockTracking();
	memSetExtraMemMode(EmuConfig.Cpu.ExtraMemory);
	Internal::ClearCPUExecutionCaches();
	memBindConditionalHandlers();
	SysMemory::Reset();
	cpuReset();
	hwReset();

	if (g_InputRecording.isActive())
	{
		g_InputRecording.handleReset();
		MTGS::PresentCurrentFrame();
	}

	ResetFrameLimiter();

	if (s_state.load(std::memory_order_acquire) == VMState::Resetting)
		s_state.store(VMState::Running, std::memory_order_release);
}

bool SaveStateBase::vmFreeze()
{
	const u32 prev_crc = s_current_crc;
	const std::string prev_elf = s_elf_path;
	const bool prev_elf_executed = s_elf_executed;
	Freeze(s_current_crc);
	FreezeString(s_elf_path);
	Freeze(s_elf_executed);

	if (IsLoading())
	{
		if (s_elf_path != prev_elf)
		{
			if (s_elf_path.empty())
			{
				if (s_elf_executed)
					Console.Error("Somehow executed a non-existant ELF");
				VMManager::ClearELFInfo();
			}
			else
			{
				VMManager::UpdateELFInfo(std::move(s_elf_path));
			}
		}

		if (s_current_crc != prev_crc || s_elf_path != prev_elf || s_elf_executed != prev_elf_executed)
			VMManager::HandleELFChange(true);
	}

	return IsOkay();
}

std::string VMManager::GetSaveStateFileName(const char* game_serial, u32 game_crc, s32 slot, bool backup)
{
	std::string filename;
	if (game_serial != nullptr && std::strlen(game_serial) > 0)
	{
		if (slot < 0)
			filename = fmt::format("{} ({:08X}).resume.p2s", game_serial, game_crc);
		else if (backup)
			filename = fmt::format("{} ({:08X}).{:02d}.p2s.backup", game_serial, game_crc, slot);
		else
			filename = fmt::format("{} ({:08X}).{:02d}.p2s", game_serial, game_crc, slot);

		filename = Path::Combine(EmuFolders::Savestates, filename);
	}
	return filename;
}

std::string VMManager::GetSaveStateFileName(const char* filename, s32 slot, bool backup)
{
	pxAssertRel(!HasValidVM(), "Should not have a VM when calling the non-gamelist GetSaveStateFileName()");

	std::string ret;
	std::string serial;
	u32 crc = 0;
	if (GameList::GetSerialAndCRCForFilename(filename, &serial, &crc))
		ret = GetSaveStateFileName(serial.c_str(), crc, slot, backup);

	return ret;
}

bool VMManager::HasSaveStateInSlot(const char* game_serial, u32 game_crc, s32 slot)
{
	const std::string filename(GetSaveStateFileName(game_serial, game_crc, slot));
	return (!filename.empty() && FileSystem::FileExists(filename.c_str()));
}

std::string VMManager::GetCurrentSaveStateFileName(s32 slot, bool backup)
{
	std::unique_lock lock(s_info_mutex);
	return GetSaveStateFileName(s_disc_serial.c_str(), s_disc_crc, slot, backup);
}

bool VMManager::DoLoadState(const char* filename, Error* error)
{
	if (GSDumpReplayer::IsReplayingDump())
	{
		Error::SetString(error, TRANSLATE_STR("VMManager", "Cannot load state while replaying a GS dump."));
		return false;
	}
	GSCapture::FlushAudioOnly();
	Host::OnSaveStateLoading(filename);

	if (!SaveState_UnzipFromDisk(filename, error))
		return false;

	Host::OnSaveStateLoaded(filename, true);
	if (g_InputRecording.isActive())
	{
		g_InputRecording.handleLoadingSavestate();
		MTGS::PresentCurrentFrame();
	}

	return true;
}

void VMManager::DoSaveState(const char* filename, s32 slot_for_message, bool zip_on_thread, bool backup_old_state, std::function<void(const std::string&)> error_callback)
{
	if (GSDumpReplayer::IsReplayingDump())
	{
		error_callback(TRANSLATE_STR("VMManager", "Cannot save state while replaying a GS dump."));
		return;
	}

	Error error;
	std::unique_ptr<ArchiveEntryList> elist = SaveState_DownloadState(&error);
	if (!elist)
	{
		error_callback(error.GetDescription());
		return;
	}

	std::unique_ptr<SaveStateScreenshotData> screenshot = SaveState_SaveScreenshot();

	if (FileSystem::FileExists(filename) && backup_old_state)
	{
		const std::string backup_filename(fmt::format("{}.backup", filename));
		if (!FileSystem::RenamePath(filename, backup_filename.c_str()))
		{
			error_callback(fmt::format(
				TRANSLATE_FS("VMManager", "Cannot back up old save state '{}'."),
				Path::GetFileName(filename)));
			return;
		}
	}

	if (zip_on_thread)
	{
		std::unique_lock lock(s_save_state_threads_mutex);
		s_save_state_threads.emplace_back(&VMManager::ZipSaveStateOnThread, std::move(elist), std::move(screenshot),
			std::string(filename), slot_for_message, std::move(error_callback));
	}
	else
	{
		ZipSaveState(std::move(elist), std::move(screenshot), filename, slot_for_message, std::move(error_callback));
	}

	Host::OnSaveStateSaved(filename);
}

void VMManager::ZipSaveState(std::unique_ptr<ArchiveEntryList> elist,
	std::unique_ptr<SaveStateScreenshotData> screenshot, const char* filename,
	s32 slot_for_message, std::function<void(const std::string&)> error_callback)
{
	const Common::Timer timer;

	Error error;
	if (!SaveState_ZipToDisk(std::move(elist), std::move(screenshot), filename, &error))
	{
		error_callback(error.GetDescription());
		return;
	}

	if (slot_for_message >= 0 && VMManager::HasValidVM())
	{
		Host::AddIconOSDMessage(fmt::format("SaveStateSlot{}", slot_for_message), ICON_FA_FLOPPY_DISK,
			fmt::format(TRANSLATE_FS("VMManager", "Saved state to slot {}."), slot_for_message),
			Host::OSD_QUICK_DURATION);
	}

	DevCon.WriteLn("Zipping save state to '%s' took %.2f ms", filename, timer.GetTimeMilliseconds());
}

void VMManager::ZipSaveStateOnThread(std::unique_ptr<ArchiveEntryList> elist,
	std::unique_ptr<SaveStateScreenshotData> screenshot, std::string filename,
	s32 slot_for_message, std::function<void(const std::string&)> error_callback)
{
	ZipSaveState(std::move(elist), std::move(screenshot), filename.c_str(), slot_for_message, std::move(error_callback));

	const auto this_id = std::this_thread::get_id();
	std::unique_lock lock(s_save_state_threads_mutex);
	for (auto it = s_save_state_threads.begin(); it != s_save_state_threads.end(); ++it)
	{
		if (it->get_id() == this_id)
		{
			it->detach();
			s_save_state_threads.erase(it);
			break;
		}
	}
}

void VMManager::WaitForSaveStateFlush()
{
	std::unique_lock lock(s_save_state_threads_mutex);
	while (!s_save_state_threads.empty())
	{
		std::thread save_thread(std::move(s_save_state_threads.front()));
		s_save_state_threads.pop_front();
		lock.unlock();
		save_thread.join();
		lock.lock();
	}
}

u32 VMManager::DeleteSaveStates(const char* game_serial, u32 game_crc, bool also_backups /* = true */)
{
	WaitForSaveStateFlush();

	u32 deleted = 0;
	for (s32 i = -1; i <= NUM_SAVE_STATE_SLOTS; ++i)
	{
		std::string filename(GetSaveStateFileName(game_serial, game_crc, i));
		if (!filename.empty() && FileSystem::FileExists(filename.c_str()) && FileSystem::DeleteFilePath(filename.c_str()))
			++deleted;

		if (also_backups)
		{
			filename += ".backup";
			if (FileSystem::FileExists(filename.c_str()) && FileSystem::DeleteFilePath(filename.c_str()))
				++deleted;
		}
	}
	return deleted;
}

bool VMManager::LoadState(const char* filename, Error* error)
{
	if (Achievements::IsHardcoreModeActive())
	{
		Error::SetString(error, TRANSLATE_STR("VMManager", "Cannot load state while RetroAchievements Hardcore Mode is active."));
		return false;
	}

	if (pcsx2_ax::vm_bridge::is_memcard_busy())
	{
		Error::SetString(error,
			TRANSLATE_STR("VMManager", "The memory card is busy, so the state load operation has been cancelled to prevent data loss."));
		return false;
	}

	if (!DoLoadState(filename, error))
	{
		Reset();
		return false;
	}

	return true;
}

bool VMManager::LoadStateFromSlot(s32 slot, bool backup, Error* error)
{
	const std::string filename = GetCurrentSaveStateFileName(slot, backup);
	if (filename.empty() || !FileSystem::FileExists(filename.c_str()))
	{
		Error::SetString(error, TRANSLATE_STR("VMManager", "The save slot is empty."));
		return false;
	}

	if (Achievements::IsHardcoreModeActive())
	{
		Error::SetString(error, TRANSLATE_STR("VMManager", "Cannot load state while RetroAchievements Hardcore Mode is active."));
		return false;
	}

	if (pcsx2_ax::vm_bridge::is_memcard_busy())
	{
		Error::SetString(error,
			TRANSLATE_STR("VMManager", "The memory card is busy, so the state load operation has been cancelled to prevent data loss."));
		return false;
	}

	if (!DoLoadState(filename.c_str(), error))
		return false;

	Host::AddIconOSDMessage("LoadStateFromSlot", ICON_FA_FOLDER_OPEN,
		backup ? fmt::format(TRANSLATE_FS("VMManager", "Loaded state from backup slot {}."), slot)
		       : fmt::format(TRANSLATE_FS("VMManager", "Loaded state from slot {}."), slot),
		Host::OSD_QUICK_DURATION);

	return true;
}

void VMManager::SaveState(
	const char* filename, bool zip_on_thread, bool backup_old_state, std::function<void(const std::string&)> error_callback)
{
	if (pcsx2_ax::vm_bridge::is_memcard_busy())
	{
		error_callback(TRANSLATE_STR("VMManager",
			"The memory card is busy, so the state save operation has been cancelled to prevent data loss."));
		return;
	}

	DoSaveState(filename, -1, zip_on_thread, backup_old_state, std::move(error_callback));
}

void VMManager::SaveStateToSlot(s32 slot, bool zip_on_thread, std::function<void(const std::string&)> error_callback)
{
	const std::string filename(GetCurrentSaveStateFileName(slot));
	if (filename.empty())
	{
		error_callback(TRANSLATE_STR("VMManager", "Cannot generate filename for save state."));
		return;
	}

	if (pcsx2_ax::vm_bridge::is_memcard_busy())
	{
		error_callback(TRANSLATE_STR("VMManager",
			"The memory card is busy, so the state save operation has been cancelled to prevent data loss."));
		return;
	}

	Host::AddIconOSDMessage(fmt::format("SaveStateSlot{}", slot), ICON_FA_FLOPPY_DISK,
		fmt::format(TRANSLATE_FS("VMManager", "Saving state to slot {}..."), slot), 60.0f);

	auto callback = [error_callback = std::move(error_callback), slot](const std::string& err) {
		Host::RemoveKeyedOSDMessage(fmt::format("SaveStateSlot{}", slot));
		error_callback(err);
	};

	DoSaveState(filename.c_str(), slot, zip_on_thread, EmuConfig.BackupSavestate, std::move(callback));
}

LimiterModeType VMManager::GetLimiterMode() { return s_limiter_mode; }

void VMManager::SetLimiterMode(LimiterModeType type)
{
	if (s_limiter_mode == type)
		return;
	s_limiter_mode = type;
	UpdateTargetSpeed();
}

float VMManager::GetTargetSpeed() { return s_target_speed; }

float VMManager::GetTargetSpeedForLimiterMode(LimiterModeType mode)
{
	if (EmuConfig.EnableFastBootFastForward && VMManager::Internal::IsFastBootInProgress())
		return 0.0f;

	switch (mode)
	{
		case LimiterModeType::Nominal:   return EmuConfig.EmulationSpeed.NominalScalar;
		case LimiterModeType::Slomo:     return EmuConfig.EmulationSpeed.SlomoScalar;
		case LimiterModeType::Turbo:     return EmuConfig.EmulationSpeed.TurboScalar;
		case LimiterModeType::Unlimited: return 0.0f;
		default:                         ASSUME(false);
	}
}

void VMManager::UpdateTargetSpeed()
{
	const float frame_rate = GetFrameRate();
	float target_speed = GetTargetSpeedForLimiterMode(s_limiter_mode);

	s_target_speed_can_sync_to_host = false;
	s_target_speed_synced_to_host = false;
	s_use_vsync_for_timing = false;

	if (EmuConfig.EmulationSpeed.SyncToHostRefreshRate)
	{
		if (const std::optional<float> host_refresh_rate = GSGetHostRefreshRate(); host_refresh_rate.has_value())
		{
			const float host_to_guest_ratio = host_refresh_rate.value() / frame_rate;
			s_target_speed_can_sync_to_host = (host_to_guest_ratio >= 0.95f && host_to_guest_ratio <= 1.05f);
			s_target_speed_synced_to_host = (s_target_speed_can_sync_to_host && target_speed == 1.0f);
			target_speed = s_target_speed_synced_to_host ? host_to_guest_ratio : target_speed;
			s_use_vsync_for_timing = (s_target_speed_synced_to_host && !EmuConfig.GS.SkipDuplicateFrames && EmuConfig.GS.VsyncEnable &&
									  EmuConfig.EmulationSpeed.UseVSyncForTiming);
		}
	}

	s_limiter_ticks_per_frame =
		static_cast<s64>(static_cast<double>(GetTickFrequency()) / static_cast<double>(std::max(frame_rate * target_speed, 1.0f)));

	if (s_target_speed != target_speed)
	{
		s_target_speed = target_speed;
		MTGS::UpdateVSyncMode();
		SPU2::OnTargetSpeedChanged();
		ResetFrameLimiter();
	}
}

bool VMManager::IsTargetSpeedAdjustedToHost() { return s_target_speed_synced_to_host; }
float VMManager::GetFrameRate() { return GetVerticalFrequency(); }
void VMManager::ResetFrameLimiter() { s_limiter_frame_start = GetCPUTicks(); }

void VMManager::Internal::Throttle(bool vsync_start)
{
	if (s_target_speed == 0.0f || s_use_vsync_for_timing)
		return;

	const u64 uExpectedEnd =
		s_limiter_frame_start +
		static_cast<u64>(vsync_start ? (static_cast<double>(s_limiter_ticks_per_frame) * 0.9)
		                             : static_cast<double>(s_limiter_ticks_per_frame));
	const u64 iEnd = GetCPUTicks();
	const s64 sDeltaTime = static_cast<s64>(iEnd - uExpectedEnd);

	if (sDeltaTime >= s_limiter_ticks_per_frame)
	{
		s_limiter_frame_start += static_cast<u64>((sDeltaTime / s_limiter_ticks_per_frame) * s_limiter_ticks_per_frame);
		return;
	}

	const s32 msec = static_cast<s32>((sDeltaTime * -1000) / static_cast<s64>(GetTickFrequency()));
	if (msec > 1)
		Threading::Sleep(msec - 1);

	while (GetCPUTicks() < uExpectedEnd)
	{
	}

	if (!vsync_start)
		s_limiter_frame_start = uExpectedEnd;
}

void VMManager::Internal::FrameRateChanged() { UpdateTargetSpeed(); }

void VMManager::FrameAdvance(u32 num_frames /*= 1*/)
{
	if (!HasValidVM())
		return;

	if (Achievements::IsHardcoreModeActive())
	{
		Host::AddIconOSDMessage("FrameAdvanceHardcoreBlocked", ICON_FA_TRIANGLE_EXCLAMATION,
			TRANSLATE_SV("VMManager", "Cannot frame advance while RetroAchievements Hardcore Mode is active."),
			Host::OSD_WARNING_DURATION);
		return;
	}

	s_frame_advance_count = num_frames;
	SetState(VMState::Running);
}

bool VMManager::ChangeDisc(CDVD_SourceType source, std::string path)
{
	const std::string old_path(pcsx2_ax::vm_bridge::get_media_path());
	const bool ready = (source != CDVD_SourceType::NoDisc) && (!path.empty() || !old_path.empty());
	pcsx2_ax::vm_bridge::set_media_path(!path.empty() ? std::move(path) : old_path, ready);
	UpdateDiscDetails(false);
	return true;
}

bool VMManager::SetELFOverride(std::string path)
{
	if (!HasValidVM() || (!path.empty() && !FileSystem::FileExists(path.c_str())))
		return false;

	s_elf_override = std::move(path);
	UpdateDiscDetails(false);
	s_fast_boot_requested = !s_elf_override.empty() || EmuConfig.EnableFastBoot;
	Reset();
	return true;
}

bool VMManager::ChangeGSDump(const std::string& path)
{
	if (!HasValidVM() || !GSDumpReplayer::IsReplayingDump() || !IsGSDumpFileName(path))
		return false;

	if (!GSDumpReplayer::ChangeDump(path.c_str()))
		return false;

	UpdateDiscDetails(false);
	return true;
}

bool VMManager::IsElfFileName(const std::string_view path) { return StringUtil::EndsWithNoCase(path, ".elf"); }
bool VMManager::IsBlockDumpFileName(const std::string_view path) { return StringUtil::EndsWithNoCase(path, ".dump"); }

bool VMManager::IsGSDumpFileName(const std::string_view path)
{
	return StringUtil::EndsWithNoCase(path, ".gs") || StringUtil::EndsWithNoCase(path, ".gs.xz") ||
	       StringUtil::EndsWithNoCase(path, ".gs.zst");
}

bool VMManager::IsSaveStateFileName(const std::string_view path) { return StringUtil::EndsWithNoCase(path, ".p2s"); }

bool VMManager::IsDiscFileName(const std::string_view path)
{
	static constexpr std::string_view extensions[] = {".iso", ".bin", ".img", ".mdf", ".gz", ".cso", ".zso", ".chd"};
	for (const std::string_view ext : extensions)
	{
		if (StringUtil::EndsWithNoCase(path, ext))
			return true;
	}
	return false;
}

bool VMManager::IsLoadableFileName(const std::string_view path)
{
	return IsDiscFileName(path) || IsElfFileName(path) || IsGSDumpFileName(path) || IsBlockDumpFileName(path);
}

#ifdef _WIN32
inline void LogUserPowerPlan()
{
	wil::unique_any<GUID*, decltype(&::LocalFree), ::LocalFree> pPwrGUID;
	if (PowerGetActiveScheme(nullptr, pPwrGUID.put()) != ERROR_SUCCESS)
		return;

	UCHAR aBuffer[2048];
	DWORD aBufferSize = sizeof(aBuffer);
	if (PowerReadFriendlyName(nullptr, pPwrGUID.get(), &NO_SUBGROUP_GUID, nullptr, aBuffer, &aBufferSize) != ERROR_SUCCESS)
		return;

	const std::string friendlyName(StringUtil::WideStringToUTF8String(reinterpret_cast<wchar_t*>(aBuffer)));
	DWORD acMax = 0, acMin = 0, dcMax = 0, dcMin = 0;
	if (PowerReadACValueIndex(nullptr, pPwrGUID.get(), &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_THROTTLE_MAXIMUM, &acMax) ||
		PowerReadACValueIndex(nullptr, pPwrGUID.get(), &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_THROTTLE_MINIMUM, &acMin) ||
		PowerReadDCValueIndex(nullptr, pPwrGUID.get(), &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_THROTTLE_MAXIMUM, &dcMax) ||
		PowerReadDCValueIndex(nullptr, pPwrGUID.get(), &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_THROTTLE_MINIMUM, &dcMin))
	{
		return;
	}

	Console.WriteLnFmt(
		"  Power Profile    = '{}'\n"
		"  Power States (min/max)\n"
		"    AC             = {}% / {}%\n"
		"    Battery        = {}% / {}%\n",
		friendlyName, acMin, acMax, dcMin, dcMax);
}

void LogGPUCapabilities()
{
	Console.WriteLn(Color_StrongBlack, "Graphics Adapters Detected:");
	wil::com_ptr_nothrow<IDXGIFactory1> pFactory;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(pFactory.put()))))
		return;

	UINT i = 0;
	wil::com_ptr_nothrow<IDXGIAdapter> pAdapter;
	while (pFactory->EnumAdapters(i, pAdapter.put()) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC desc{};
		LARGE_INTEGER umdver{};
		if (SUCCEEDED(pAdapter->GetDesc(&desc)) && SUCCEEDED(pAdapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umdver)))
		{
			Console.WriteLnFmt(
				"  GPU              = {}\n"
				"  Driver Version   = {}.{}.{}.{}\n",
				StringUtil::WideStringToUTF8String(desc.Description),
				umdver.QuadPart >> 48,
				(umdver.QuadPart >> 32) & 0xFFFF,
				(umdver.QuadPart >> 16) & 0xFFFF,
				umdver.QuadPart & 0xFFFF);
			++i;
		}
		else
		{
			break;
		}
	}
}
#endif

void VMManager::LogCPUCapabilities()
{
	Console.WriteLn(Color_StrongGreen, "PCSX2_AX %s", BuildVersion::GitRev);
	Console.WriteLnFmt("Savestate version: 0x{:x}\n", g_SaveVersion);

	const CPUInfo& cpuinfo = GetCPUInfo();
	Console.WriteLnFmt("  Processor        = {}", cpuinfo.name);
	Console.WriteLnFmt("  Core Count       = {} cores", cpuinfo.num_big_cores + cpuinfo.num_small_cores);
	Console.WriteLnFmt("  Thread Count     = {} threads", cpuinfo.num_threads);
	Console.WriteLnFmt("  Cluster Count    = {} clusters", cpuinfo.num_clusters);

#ifdef _WIN32
	LogUserPowerPlan();
	LogGPUCapabilities();
#endif
}

void VMManager::InitializeCPUProviders()
{
#ifdef _M_X86
	recCpu.Reserve();
	psxRec.Reserve();
	CpuMicroVU0.Reserve();
	CpuMicroVU1.Reserve();
#else
	vu1Thread.Open();
#endif
	VifUnpackSSE_Init();
}

void VMManager::ShutdownCPUProviders()
{
	if (newVifDynaRec)
	{
		dVifRelease(1);
		dVifRelease(0);
	}

#ifdef _M_X86
	CpuMicroVU1.Shutdown();
	CpuMicroVU0.Shutdown();
	psxRec.Shutdown();
	recCpu.Shutdown();
#else
	if (vu1Thread.IsOpen())
		vu1Thread.WaitVU();
#endif
}

void VMManager::UpdateCPUImplementations()
{
	if (GSDumpReplayer::IsReplayingDump())
	{
		Cpu = &GSDumpReplayerCpu;
		psxCpu = &psxInt;
		CpuVU0 = &CpuIntVU0;
		CpuVU1 = &CpuIntVU1;
		return;
	}

#ifdef _M_X86
	Cpu = CHECK_EEREC ? &recCpu : &intCpu;
	psxCpu = CHECK_IOPREC ? &psxRec : &psxInt;
	CpuVU0 = EmuConfig.Cpu.Recompiler.EnableVU0 ? static_cast<BaseVUmicroCPU*>(&CpuMicroVU0) : static_cast<BaseVUmicroCPU*>(&CpuIntVU0);
	CpuVU1 = EmuConfig.Cpu.Recompiler.EnableVU1 ? static_cast<BaseVUmicroCPU*>(&CpuMicroVU1) : static_cast<BaseVUmicroCPU*>(&CpuIntVU1);
#else
	Cpu = &intCpu;
	psxCpu = &psxInt;
	CpuVU0 = &CpuIntVU0;
	CpuVU1 = &CpuIntVU1;
#endif
}

void VMManager::Internal::ClearCPUExecutionCaches()
{
	Cpu->Reset();
	psxCpu->Reset();

#ifdef _M_X86
	if (CHECK_EEREC && !EmuConfig.Cpu.Recompiler.EnableVU0)
		CpuMicroVU0.Reset();
#endif

	CpuVU0->Reset();
	CpuVU1->Reset();

	if constexpr (newVifDynaRec)
	{
		dVifReset(0);
		dVifReset(1);
	}
}

void VMManager::Execute()
{
	if (std::exchange(s_cpu_implementation_changed, false))
	{
		UpdateCPUImplementations();
		Internal::ClearCPUExecutionCaches();
		vtlb_ResetFastmem();
	}

	Cpu->Execute();
}

void VMManager::IdlePollUpdate()
{
	Achievements::IdleUpdate();
	PollDiscordPresence();
	InputManager::PollSources();
}

void VMManager::SetPaused(bool paused)
{
	if (!HasValidVM())
		return;

	SetState(paused ? VMState::Paused : VMState::Running);
}

GSVSyncMode VMManager::GetEffectiveVSyncMode()
{
	if (!EmuConfig.GS.VsyncEnable)
		return GSVSyncMode::Disabled;

	const VMState state = GetState();
	const bool valid_vm = (state != VMState::Shutdown && state != VMState::Stopping);
	if (s_target_speed_can_sync_to_host || (!valid_vm && EmuConfig.EmulationSpeed.SyncToHostRefreshRate) ||
		EmuConfig.GS.DisableMailboxPresentation)
	{
		return GSVSyncMode::FIFO;
	}

	return GSVSyncMode::Mailbox;
}

bool VMManager::ShouldAllowPresentThrottle()
{
	const VMState state = GetState();
	const bool valid_vm = (state != VMState::Shutdown && state != VMState::Stopping);
	return (!valid_vm || (!s_target_speed_synced_to_host && s_target_speed != 1.0f));
}

bool VMManager::Internal::WasFastBooted() { return s_fast_boot_requested; }
bool VMManager::Internal::IsFastBootInProgress() { return s_fast_boot_requested && !HasBootedELF(); }
std::string VMManager::Internal::GetCurrentRegion() { return s_cur_region; }

void VMManager::Internal::DisableFastBoot()
{
	if (!s_fast_boot_requested)
		return;
	s_fast_boot_requested = false;
	if (EmuConfig.EnableFastBootFastForward && !s_elf_executed)
		UpdateTargetSpeed();
}

bool VMManager::Internal::HasBootedELF() { return VMManager::HasBootedELF(); }
u32 VMManager::Internal::GetCurrentELFEntryPoint() { return s_elf_entry_point; }
const std::string& VMManager::Internal::GetELFOverride() { return s_elf_override; }

bool VMManager::Internal::IsExecutionInterrupted()
{
	return s_state.load(std::memory_order_relaxed) != VMState::Running || s_cpu_implementation_changed;
}

void VMManager::Internal::ELFLoadingOnCPUThread(std::string elf_path)
{
	const bool was_running_bios = (s_current_crc == 0);
	UpdateELFInfo(std::move(elf_path));
	s_elf_executed = false;

	if (!was_running_bios)
	{
		Patch::ReloadPatches(s_disc_serial, 0, false, false, false, true);
		ApplyCoreSettings();
	}
}

void VMManager::Internal::EntryPointCompilingOnCPUThread()
{
	if (s_elf_executed)
		return;

	const bool reset_speed_limiter = (EmuConfig.EnableFastBootFastForward && IsFastBootInProgress());
	s_elf_executed = true;

	if (reset_speed_limiter)
	{
		UpdateTargetSpeed();
		PerformanceMetrics::Reset();
	}

	HandleELFChange(true);
	Patch::ApplyBootPatches();
	mmap_ResetBlockTracking();
	ClearCPUExecutionCaches();
	R5900SymbolImporter.OnElfLoadedInMemory();
}

void VMManager::Internal::VSyncOnCPUThread()
{
	pcsx2_ax::vm_bridge::notify_mode_vsync();
	Patch::ApplyVsyncPatches();

	if (s_frame_advance_count > 0)
	{
		--s_frame_advance_count;
		if (s_frame_advance_count == 0)
			SetState(VMState::Paused);
	}

	Achievements::FrameUpdate();
	PollDiscordPresence();
}

void VMManager::Internal::PollInputOnCPUThread()
{
	Host::PumpMessagesOnCPUThread();
	InputManager::PollSources();

	if (EmuConfig.EnableRecordingTools && g_InputRecording.isActive())
	{
		g_InputRecording.processRecordQueue();
		g_InputRecording.getControls().processControlQueue();
		g_InputRecording.incFrameCounter();
		g_InputRecording.handleExceededFrameCounter();
		g_InputRecording.handleControllerDataUpdate();
	}
}

void VMManager::CheckForCPUConfigChanges(const Pcsx2Config& old_config)
{
	if (EmuConfig.Cpu == old_config.Cpu && EmuConfig.Gamefixes == old_config.Gamefixes &&
		EmuConfig.Speedhacks == old_config.Speedhacks && EmuConfig.Profiler == old_config.Profiler)
	{
		return;
	}

	FPControlRegister::SetCurrent(EmuConfig.Cpu.FPUFPCR);
	Internal::ClearCPUExecutionCaches();
	memBindConditionalHandlers();

	if (EmuConfig.Cpu.Recompiler.EnableFastmem != old_config.Cpu.Recompiler.EnableFastmem)
		vtlb_ResetFastmem();

	if (EmuConfig.Cpu.CpusChanged(old_config.Cpu))
		s_cpu_implementation_changed = true;
}

void VMManager::CheckForGSConfigChanges(const Pcsx2Config& old_config)
{
	if (EmuConfig.GS == old_config.GS)
		return;

	if (EmuConfig.GS.FramerateNTSC != old_config.GS.FramerateNTSC ||
		EmuConfig.GS.FrameratePAL != old_config.GS.FrameratePAL)
	{
		UpdateVSyncRate(false);
		UpdateTargetSpeed();
	}
	else if (EmuConfig.GS.VsyncEnable != old_config.GS.VsyncEnable ||
			 EmuConfig.GS.DisableMailboxPresentation != old_config.GS.DisableMailboxPresentation)
	{
		UpdateTargetSpeed();
		MTGS::UpdateVSyncMode();
	}

	MTGS::ApplySettings();
}

void VMManager::CheckForEmulationSpeedConfigChanges(const Pcsx2Config& old_config)
{
	if (EmuConfig.EmulationSpeed != old_config.EmulationSpeed)
		UpdateTargetSpeed();
}

void VMManager::CheckForPatchConfigChanges(const Pcsx2Config& old_config)
{
	if (EmuConfig.EnableCheats == old_config.EnableCheats &&
		EmuConfig.EnableWideScreenPatches == old_config.EnableWideScreenPatches &&
		EmuConfig.EnableNoInterlacingPatches == old_config.EnableNoInterlacingPatches &&
		EmuConfig.EnablePatches == old_config.EnablePatches)
	{
		return;
	}

	Patch::UpdateActivePatches(true, false, true, HasValidVM());
	if (Patch::ReloadPatchAffectingOptions())
		MTGS::ApplySettings();
}

void VMManager::CheckForDEV9ConfigChanges(const Pcsx2Config& old_config)
{
	if (EmuConfig.DEV9 != old_config.DEV9)
		DEV9CheckChanges(old_config);
}

void VMManager::CheckForMemoryCardConfigChanges(const Pcsx2Config& /*old_config*/)
{
}

void VMManager::CheckForMiscConfigChanges(const Pcsx2Config& old_config)
{
	if (EmuConfig.EnableFastBootFastForward && !old_config.EnableFastBootFastForward &&
		VMManager::Internal::IsFastBootInProgress())
	{
		UpdateTargetSpeed();
	}

	if (EmuConfig.InhibitScreensaver != old_config.InhibitScreensaver)
		UpdateInhibitScreensaver(EmuConfig.InhibitScreensaver && VMManager::GetState() == VMState::Running);

	if (EmuConfig.EnableDiscordPresence != old_config.EnableDiscordPresence)
	{
		if (EmuConfig.EnableDiscordPresence)
			InitializeDiscordPresence();
		else
			ShutdownDiscordPresence();
	}

	if (HasValidVM() && (EmuConfig.EnableThreadPinning != old_config.EnableThreadPinning ||
							(s_thread_affinities_set && EmuConfig.Speedhacks.vuThread != old_config.Speedhacks.vuThread)))
	{
		SetEmuThreadAffinities();
	}
}

void VMManager::CheckForConfigChanges(const Pcsx2Config& old_config)
{
	if (HasValidVM())
	{
		CheckForCPUConfigChanges(old_config);
		CheckForEmulationSpeedConfigChanges(old_config);
		CheckForPatchConfigChanges(old_config);
		SPU2::CheckForConfigChanges(old_config);
		CheckForDEV9ConfigChanges(old_config);
		CheckForMemoryCardConfigChanges(old_config);
		USB::CheckForConfigChanges(old_config);
	}

	if (HasValidVM() || MTGS::IsOpen())
		CheckForGSConfigChanges(old_config);

	if (EmuConfig.Achievements != old_config.Achievements)
		Achievements::UpdateSettings(old_config.Achievements);

	FullscreenUI::CheckForConfigChanges(old_config);
	CheckForMiscConfigChanges(old_config);
	Host::CheckForSettingsChanges(old_config);
}

void VMManager::ReloadPatches(bool reload_files, bool reload_enabled_list, bool verbose, bool verbose_if_changed)
{
	if (!HasValidVM())
		return;

	Patch::ReloadPatches(s_disc_serial, HasBootedELF() ? s_current_crc : 0, reload_files, reload_enabled_list, verbose, verbose_if_changed);
	if (Patch::ReloadPatchAffectingOptions())
		ApplyCoreSettings();
}

void VMManager::EnforceAchievementsChallengeModeSettings()
{
	if (!Achievements::IsHardcoreModeActive())
		return;

	static constexpr auto ClampSpeed = [](float& rate) {
		if (rate > 0.0f && rate < 1.0f)
			rate = 1.0f;
	};

	ClampSpeed(EmuConfig.EmulationSpeed.NominalScalar);
	ClampSpeed(EmuConfig.EmulationSpeed.TurboScalar);
	ClampSpeed(EmuConfig.EmulationSpeed.SlomoScalar);

	if (EmuConfig.EnableCheats)
	{
		Host::AddIconOSDMessage("ChallengeDisableCheats", ICON_FA_TRIANGLE_EXCLAMATION,
			TRANSLATE_SV("VMManager", "Cheats have been disabled due to RetroAchievements Hardcore Mode."),
			Host::OSD_WARNING_DURATION);
		EmuConfig.EnableCheats = false;
	}

	EmuConfig.EnableRecordingTools = false;
	EmuConfig.EnablePINE = false;
	EmuConfig.GS.FramerateNTSC = Pcsx2Config::GSOptions::DEFAULT_FRAME_RATE_NTSC;
	EmuConfig.GS.FrameratePAL = Pcsx2Config::GSOptions::DEFAULT_FRAME_RATE_PAL;
	EmuConfig.Speedhacks.EECycleRate = std::max<decltype(EmuConfig.Speedhacks.EECycleRate)>(EmuConfig.Speedhacks.EECycleRate, 0);
	EmuConfig.Speedhacks.EECycleSkip = 0;
}

void VMManager::LogUnsafeSettingsToConsole(const std::string& messages)
{
	std::string console_messages(messages);
	for (;;)
	{
		const std::string::size_type pos = console_messages.find("\xef");
		if (pos == std::string::npos)
			break;
		console_messages.erase(pos, pos + 3);
		console_messages.insert(pos, "[Unsafe Settings]");
	}
	Console.Warning(console_messages);
}

void VMManager::WarnAboutUnsafeSettings()
{
	if (!EmuConfig.WarnAboutUnsafeSettings)
		return;

	std::string messages;
	auto append = [&messages](const char* icon, const std::string_view msg) {
		messages += icon;
		messages += ' ';
		messages += msg;
		messages += '\n';
	};

	if (EmuConfig.Speedhacks.fastCDVD)
		append(ICON_FA_COMPACT_DISC, TRANSLATE_SV("VMManager", "Fast CDVD is enabled, this may break games."));
	if (EmuConfig.Speedhacks.EECycleRate != 0 || EmuConfig.Speedhacks.EECycleSkip != 0)
		append(ICON_FA_GAUGE_SIMPLE_HIGH, TRANSLATE_SV("VMManager", "Cycle rate/skip is not at default."));

	if (!messages.empty())
	{
		if (messages.back() == '\n')
			messages.pop_back();
		LogUnsafeSettingsToConsole(messages);
		Host::AddKeyedOSDMessage("unsafe_settings_warning", std::move(messages), Host::OSD_WARNING_DURATION);
	}
	else
	{
		Host::RemoveKeyedOSDMessage("unsafe_settings_warning");
	}
}

void VMManager::UpdateInhibitScreensaver(bool inhibit)
{
	if (s_screensaver_inhibited == inhibit)
		return;

	if (Common::InhibitScreensaver(inhibit))
		s_screensaver_inhibited = inhibit;
	else if (inhibit)
		Console.Warning("Failed to inhibit screen saver.");
}

void VMManager::SaveSessionTime(const std::string& prev_serial)
{
	if (GSDumpReplayer::IsReplayingDump() || prev_serial.empty())
		return;

	const std::time_t etime =
		static_cast<std::time_t>(std::round(Common::Timer::ConvertValueToSeconds(std::exchange(s_session_accumulated_playtime, 0))));
	const std::time_t wtime = std::time(nullptr);
	GameList::AddPlayedTimeForSerial(prev_serial, wtime, etime);
}

u64 VMManager::GetSessionPlayedTime()
{
	return static_cast<u64>(std::round(Common::Timer::ConvertValueToSeconds(s_session_accumulated_playtime)));
}

#ifdef _WIN32
static bool s_timer_resolution_increased = false;

void VMManager::SetTimerResolutionIncreased(bool enabled)
{
	if (s_timer_resolution_increased == enabled)
		return;

	if (enabled)
		s_timer_resolution_increased = (timeBeginPeriod(1) == TIMERR_NOERROR);
	else if (s_timer_resolution_increased)
	{
		timeEndPeriod(1);
		s_timer_resolution_increased = false;
	}
}
#else
void VMManager::SetTimerResolutionIncreased(bool) {}
#endif

static std::vector<u32> s_processor_list;
static std::vector<u32> s_software_renderer_processor_list;
static std::once_flag s_processor_list_initialized;

#if defined(__linux__) || defined(_WIN32)
static u32 GetProcessorIdForProcessor(const cpuinfo_processor* proc)
{
#if defined(__linux__)
	return static_cast<u32>(proc->linux_id);
#elif defined(_WIN32)
	return static_cast<u32>(proc->windows_processor_id);
#else
	return 0;
#endif
}

static void InitializeProcessorList()
{
	if (!cpuinfo_initialize())
	{
		ERROR_LOG("cpuinfo_initialize() failed");
		return;
	}

	const u32 processor_count = cpuinfo_get_processors_count();
	std::vector<const cpuinfo_processor*> processors;
	for (u32 i = 0; i < processor_count; ++i)
	{
		const cpuinfo_processor* proc = cpuinfo_get_processor(i);
		if (!proc || proc->smt_id != 0)
			continue;
		processors.push_back(proc);
	}

	std::sort(processors.begin(), processors.end(),
		[](const cpuinfo_processor* lhs, const cpuinfo_processor* rhs) {
			return lhs->core->frequency > rhs->core->frequency;
		});

	s_processor_list.reserve(processors.size());
	for (const cpuinfo_processor* proc : processors)
		s_processor_list.push_back(GetProcessorIdForProcessor(proc));
}

void VMManager::SetHardwareDependentDefaultSettings(SettingsInterface& si)
{
	EnsureCPUInfoInitialized();
	const u32 core_count = cpuinfo_get_cores_count();
	if (core_count == 0)
		return;

	si.SetBoolValue("EmuCore/Speedhacks", "vuThread", core_count >= 3);
	si.SetIntValue("EmuCore/GS", "extrathreads", (core_count > 3) ? 3 : 2);
}
#elif defined(__APPLE__)
static void InitializeProcessorList() {}
void VMManager::SetHardwareDependentDefaultSettings(SettingsInterface& si) { si.SetBoolValue("EmuCore/Speedhacks", "vuThread", true); }
#else
static void InitializeProcessorList() {}
void VMManager::SetHardwareDependentDefaultSettings(SettingsInterface& si) { si.SetBoolValue("EmuCore/Speedhacks", "vuThread", true); }
#endif

void VMManager::EnsureCPUInfoInitialized()
{
	std::call_once(s_processor_list_initialized, InitializeProcessorList);
}

void VMManager::SetEmuThreadAffinities()
{
	const bool new_pin_enable = (GetState() != VMState::Shutdown && EmuConfig.EnableThreadPinning);
	if (s_thread_affinities_set == new_pin_enable)
		return;

	s_thread_affinities_set = EmuConfig.EnableThreadPinning;
	EnsureCPUInfoInitialized();

	if (s_processor_list.empty())
		return;

	const bool mtvu = EmuConfig.Speedhacks.vuThread;
	if (!new_pin_enable || s_processor_list.size() < (mtvu ? 3u : 2u))
	{
		MTGS::GetThreadHandle().SetAffinity(0);
		vu1Thread.GetThreadHandle().SetAffinity(0);
		s_vm_thread_handle.SetAffinity(0);
		s_software_renderer_processor_list.clear();
		return;
	}

	const u32 ee_index = s_processor_list[0];
	const u32 vu_index = s_processor_list[1];
	const u32 gs_index = s_processor_list[mtvu ? 2 : 1];

	s_vm_thread_handle.SetAffinity(static_cast<u64>(1) << ee_index);
	vu1Thread.GetThreadHandle().SetAffinity(mtvu ? (static_cast<u64>(1) << vu_index) : 0u);
	MTGS::GetThreadHandle().SetAffinity(static_cast<u64>(1) << gs_index);
}

const std::vector<u32>& VMManager::Internal::GetSoftwareRendererProcessorList()
{
	EnsureCPUInfoInitialized();
	return s_software_renderer_processor_list;
}

void VMManager::ReloadPINE()
{
	const bool needs_reinit = (EmuConfig.EnablePINE != PINEServer::IsInitialized() ||
							   PINEServer::GetSlot() != EmuConfig.PINESlot);
	if (!needs_reinit)
		return;

	PINEServer::Deinitialize();
	if (EmuConfig.EnablePINE)
		PINEServer::Initialize(EmuConfig.PINESlot);
}

void VMManager::InitializeDiscordPresence()
{
	if (s_discord_presence_active)
		return;

	DiscordEventHandlers handlers{};
	Discord_Initialize(s_discord_presence_app_id, &handlers, 0, nullptr);
	s_discord_presence_active = true;
	UpdateDiscordPresence(true);
}

void VMManager::ShutdownDiscordPresence()
{
	if (!s_discord_presence_active)
		return;

	Discord_ClearPresence();
	Discord_RunCallbacks();
	Discord_Shutdown();
	s_discord_presence_active = false;
}

void VMManager::UpdateDiscordPresence(bool update_session_time)
{
	if (!s_discord_presence_active)
		return;

	if (update_session_time)
		s_discord_presence_time_epoch = std::time(nullptr);

	std::string rp_title;
	const bool prefer_english = Host::GetBaseBoolSettingValue("UI", "PreferEnglishGameList", false);
	if (!s_title.empty())
		rp_title = GetTitle(prefer_english);

	DiscordRichPresence rp{};
	rp.largeImageKey = s_discord_presence_large_image_key;
	rp.largeImageText = s_discord_presence_large_image_text;
	rp.startTimestamp = s_discord_presence_time_epoch;

	if (rp_title.empty())
		rp.details = TRANSLATE("VMManager", "No Game Running");
	else
	{
		rp.details = rp_title.c_str();
		rp.status_display_type = DiscordStatusDisplayType_Details;
	}

	std::string state_string;
	auto lock = Achievements::GetLock();
	if (Achievements::HasActiveGame() && Achievements::HasAchievementsOrLeaderboards())
	{
		if (const std::string& icon_url = Achievements::GetGameIconURL(); !icon_url.empty())
		{
			rp.largeImageKey = icon_url.c_str();
			rp.largeImageText = s_title.c_str();
		}

		if (Achievements::HasRichPresence())
			rp.state = (state_string = StringUtil::Ellipsise(Achievements::GetRichPresenceString(), 128)).c_str();
	}

	Discord_UpdatePresence(&rp);
	Discord_RunCallbacks();
}

void VMManager::PollDiscordPresence()
{
	if (s_discord_presence_active)
		Discord_RunCallbacks();
}

bool VMManager::WriteBytesToEESIORXFIFO(const std::span<const u8> data)
{
	if (ee_sio_rx_fifo.size() + data.size() > 1024)
	{
		Console.Warning("EE RX FIFO is full, not appending more bytes.");
		return false;
	}

	ee_sio_rx_fifo.insert(ee_sio_rx_fifo.end(), data.begin(), data.end());
	return true;
}
