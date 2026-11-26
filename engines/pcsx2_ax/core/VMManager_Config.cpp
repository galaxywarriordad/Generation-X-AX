// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team & The Silicon Preservation Society
// SPDX-License-Identifier: GPL-3.0+

#include "VMManager_Internal.h"

#include "Achievements.h"
#include "BuildVersion.h"
#include "DEV9/DEV9.h"
#include "Elfheader.h"
#include "GS.h"
#include "GS/Renderers/HW/GSTextureReplacements.h"
#include "GS/GSCapture.h"
#include "GameDatabase.h"
#include "GameList.h"
#include "GSDumpReplayer.h"
#include "Host.h"
#include "INISettingsInterface.h"
#include "ImGui/FullscreenUI.h"
#include "Input/InputManager.h"
#include "MTGS.h"
#include "MTVU.h"
#include "PINE.h"
#include "Patch.h"
#include "SPU2/spu2.h"
#include "USB/USB.h"

#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/FPControl.h"
#include "common/Path.h"
#include "common/SettingsWrapper.h"
#include "common/SmallString.h"
#include "common/StringUtil.h"
#include "common/Timer.h"

#include "IconsFontAwesome.h"
#include "cpuinfo.h"
#include "discord_rpc.h"
#include "fmt/format.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include "common/RedtapeWindows.h"
#include <timeapi.h>
#include <powrprof.h>
#endif

namespace {

constexpr u32 kSettingsVersion = 1u;

std::unique_ptr<INISettingsInterface> s_game_settings_interface;
std::unique_ptr<INISettingsInterface> s_input_settings_interface;
std::string s_input_profile_name;
bool s_log_block_system_console = false;
bool s_log_force_file_log = false;
bool s_thread_affinities_set = false;

s64 s_limiter_ticks_per_frame = 0;
u64 s_limiter_frame_start = 0;
u64 s_session_resume_timestamp = 0;
u64 s_session_accumulated_playtime = 0;
bool s_screensaver_inhibited = false;

bool s_discord_presence_active = false;
std::time_t s_discord_presence_time_epoch = 0;
constexpr const char* kDiscordAppId = "1458595419499139094";
constexpr const char* kDiscordLargeImageKey = "4k-pcsx2";
constexpr const char* kDiscordLargeImageText = "PCSX2_AX";

std::vector<u32> s_processor_list;
std::vector<u32> s_software_renderer_processor_list;
std::once_flag s_processor_list_initialized;

#ifdef _WIN32
bool s_timer_resolution_increased = false;
#endif

} // namespace

void VMManager::AccumulateSessionPlaytime()
{
	s_session_accumulated_playtime += static_cast<u64>(Common::Timer::GetCurrentValue()) - s_session_resume_timestamp;
}

void VMManager::ResetResumeTimestamp()
{
	s_session_resume_timestamp = static_cast<u64>(Common::Timer::GetCurrentValue());
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

	Log::SetTimestampsEnabled(si.GetBoolValue("Logging", "EnableTimestamps", true));

	const bool any_logging_sinks = system_console_enabled || log_window_enabled || file_logging_enabled || debug_console_enabled;
	ConsoleLogging.eeConsole.Enabled = any_logging_sinks && si.GetBoolValue("Logging", "EnableEEConsole", false);
	ConsoleLogging.iopConsole.Enabled = any_logging_sinks && si.GetBoolValue("Logging", "EnableIOPConsole", false);
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
	return bsi && bsi->GetUIntValue("UI", "SettingsVersion", &settings_version) && settings_version == kSettingsVersion;
}

void VMManager::Internal::LoadStartupSettings()
{
	SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer();
	if (!bsi)
		return;
	EmuFolders::LoadConfig(*bsi);
	EmuFolders::EnsureFoldersExist();
	UpdateLoggingSettings(*bsi);
}

void VMManager::SetDefaultSettings(SettingsInterface& si, bool folders, bool core, bool controllers, bool hotkeys, bool ui)
{
	FPControlRegisterBackup fpcr_backup(FPControlRegister::GetDefault());

	if (si.GetUIntValue("UI", "SettingsVersion", 0u) != kSettingsVersion)
		si.SetUIntValue("UI", "SettingsVersion", kSettingsVersion);

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
		USB::SetDefaultConfiguration(&si);
	(void)hotkeys;
	if (ui)
		Host::SetDefaultUISettings(si);
}

void VMManager::LoadSettings()
{
	FPControlRegisterBackup fpcr_backup(FPControlRegister::GetDefault());

	std::unique_lock<std::mutex> lock = Host::GetSettingsLock();
	SettingsInterface* si = Host::GetSettingsInterface();
	if (!si)
		return;

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
	if (!si)
		return;

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
	if (!si)
		return;

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

void VMManager::ApplyGameFixes()
{
	if (!HasBootedELF() && !GSDumpReplayer::IsReplayingDump())
	{
		EmuConfig.Gamefixes.InstantDMAHack = true;
		EmuConfig.GS.ManualUserHacks = false;
		return;
	}

	const std::string serial = GetDiscSerial();
	const GameDatabaseSchema::GameEntry* game = GameDatabase::findGame(serial);
	if (!game)
		return;

	game->applyGameFixes(EmuConfig, EmuConfig.EnableGameFixes);
	game->applyGSHardwareFixes(EmuConfig.GS);
	EmuConfig.GS.MaskUpscalingHacks();
}

void VMManager::ApplySettings()
{
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
		if (SettingsInterface* si = Host::GetSettingsInterface())
			LoadCoreSettings(*si);
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
	if (!g_game_settings_override.empty())
		return g_game_settings_override;

	const std::string sanitized_serial(Path::SanitizeFileName(game_serial));
	return game_serial.empty()
		? Path::Combine(EmuFolders::GameSettings, fmt::format("{:08X}.ini", game_crc))
		: Path::Combine(EmuFolders::GameSettings, fmt::format("{}_{:08X}.ini", sanitized_serial, game_crc));
}

bool VMManager::AreGameSettingsPresent(const std::string_view game_serial, u32 game_crc)
{
	std::string filename = GetGameSettingsPath(game_serial, game_crc);
	if (!FileSystem::FileExists(filename.c_str()))
		filename = GetGameSettingsPath({}, game_crc);
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
	return GetDebuggerSettingsFilePath(GetDiscSerial(), GetCurrentCRC());
}

void VMManager::Internal::UpdateEmuFolders()
{
	const std::string old_cheats(EmuFolders::Cheats);
	const std::string old_patches(EmuFolders::Patches);
	const std::string old_textures(EmuFolders::Textures);
	const std::string old_videos(EmuFolders::Videos);

	auto lock = Host::GetSettingsLock();
	if (SettingsInterface* bsi = Host::Internal::GetBaseSettingsLayer())
		EmuFolders::LoadConfig(*bsi);
	EmuFolders::EnsureFoldersExist();

	if (VMManager::HasValidVM())
	{
		if (EmuFolders::Cheats != old_cheats || EmuFolders::Patches != old_patches)
			Patch::ReloadPatches(GetDiscSerial(), GetCurrentCRC(), true, false, true, true);

		if (EmuFolders::Textures != old_textures)
		{
			MTGS::RunOnGSThread([]() {
				if (VMManager::HasValidVM())
					GSTextureReplacements::ReloadReplacementMap();
			});
		}

		if (EmuFolders::Videos != old_videos)
		{
			MTGS::RunOnGSThread(&GSEndCapture);
		}
	}
}

void VMManager::RequestDisplaySize(float scale)
{
	int iwidth = 0, iheight = 0;
	GSgetInternalResolution(&iwidth, &iheight);
	if (iwidth <= 0 || iheight <= 0)
		return;

	float x_scale = 1.0f;
	const double aspect = static_cast<double>(iwidth) / static_cast<double>(iheight);
	switch (GSConfig.AspectRatio)
	{
		case AspectRatioType::RAuto4_3_3_2:
			if (EmuConfig.CurrentCustomAspectRatio > 0.f)
				x_scale = static_cast<float>(static_cast<double>(EmuConfig.CurrentCustomAspectRatio) / aspect);
			else if (GSgetDisplayMode() == GSVideoMode::SDTV_480P)
				x_scale = static_cast<float>((3.0 / 2.0) / aspect);
			else
				x_scale = static_cast<float>((4.0 / 3.0) / aspect);
			break;
		case AspectRatioType::R4_3:
			x_scale = static_cast<float>((4.0 / 3.0) / aspect);
			break;
		case AspectRatioType::R16_9:
			x_scale = static_cast<float>((16.0 / 9.0) / aspect);
			break;
		case AspectRatioType::R10_7:
			x_scale = static_cast<float>((10.0 / 7.0) / aspect);
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
		const float adj = (1.0f / GSConfig.UpscaleMultiplier) * scale;
		width *= adj;
		height *= adj;
	}

	Host::RequestResizeHostDisplay(std::max(static_cast<int>(std::lroundf(width)), 1),
	                               std::max(static_cast<int>(std::lroundf(height)), 1));
}

bool VMManager::UpdateGameSettingsLayer()
{
	std::unique_ptr<INISettingsInterface> new_interface;
	const u32 crc = GetDiscCRC();
	if (crc != 0)
	{
		std::string filename = GetGameSettingsPath(GetSerialForGameSettings(), crc);
		if (!FileSystem::FileExists(filename.c_str()))
			filename = GetGameSettingsPath({}, crc);

		if (FileSystem::FileExists(filename.c_str()))
		{
			new_interface = std::make_unique<INISettingsInterface>(std::move(filename));
			if (!new_interface->Load())
				new_interface.reset();
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
		const std::string filename = GetInputProfilePath(input_profile_name);
		if (FileSystem::FileExists(filename.c_str()))
		{
			input_interface = std::make_unique<INISettingsInterface>(std::move(filename));
			if (!input_interface->Load())
			{
				input_interface.reset();
				input_profile_name.clear();
			}
		}
		else
		{
			input_profile_name.clear();
		}
	}

	Host::Internal::SetInputSettingsLayer(input_interface.get(), lock);
	s_input_settings_interface = std::move(input_interface);
	s_input_profile_name = std::move(input_profile_name);
	return true;
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
		g_cpu_implementation_changed = true;
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

	Patch::ReloadPatches(GetDiscSerial(), HasBootedELF() ? GetCurrentCRC() : 0, reload_files, reload_enabled_list, verbose, verbose_if_changed);
	if (Patch::ReloadPatchAffectingOptions())
		ApplyCoreSettings();
}

void VMManager::EnforceAchievementsChallengeModeSettings()
{
	if (!Achievements::IsHardcoreModeActive())
		return;

	static constexpr auto clamp_speed = [](float& rate) {
		if (rate > 0.0f && rate < 1.0f)
			rate = 1.0f;
	};

	clamp_speed(EmuConfig.EmulationSpeed.NominalScalar);
	clamp_speed(EmuConfig.EmulationSpeed.TurboScalar);
	clamp_speed(EmuConfig.EmulationSpeed.SlomoScalar);

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
	if (EmuConfig.Speedhacks.fastCDVD)
		messages += TRANSLATE_SV("VMManager", "Fast CDVD is enabled, this may break games.\n");
	if (EmuConfig.Speedhacks.EECycleRate != 0 || EmuConfig.Speedhacks.EECycleSkip != 0)
		messages += TRANSLATE_SV("VMManager", "Cycle rate/skip is not at default.\n");

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

#ifdef _WIN32
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
		return;

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
		g_vm_thread_handle.SetAffinity(0);
		s_software_renderer_processor_list.clear();
		return;
	}

	const u32 ee_index = s_processor_list[0];
	const u32 vu_index = s_processor_list[1];
	const u32 gs_index = s_processor_list[mtvu ? 2 : 1];

	g_vm_thread_handle.SetAffinity(static_cast<u64>(1) << ee_index);
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
	Discord_Initialize(kDiscordAppId, &handlers, 0, nullptr);
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
	if (!GetDiscSerial().empty())
		rp_title = GetTitle(prefer_english);

	DiscordRichPresence rp{};
	rp.largeImageKey = kDiscordLargeImageKey;
	rp.largeImageText = kDiscordLargeImageText;
	rp.startTimestamp = s_discord_presence_time_epoch;

	std::string state_string;
	if (rp_title.empty())
		rp.details = TRANSLATE("VMManager", "No Game Running");
	else
	{
		rp.details = rp_title.c_str();
		rp.status_display_type = DiscordStatusDisplayType_Details;
	}

	auto lock = Achievements::GetLock();
	if (Achievements::HasActiveGame() && Achievements::HasAchievementsOrLeaderboards())
	{
		if (const std::string& icon_url = Achievements::GetGameIconURL(); !icon_url.empty())
		{
			rp.largeImageKey = icon_url.c_str();
			rp.largeImageText = rp_title.c_str();
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

void VMManager::LogCPUCapabilities()
{
	const auto snap = pcsx2_ax::vm_bridge::current_snapshot();
	Console.WriteLn(Color_StrongGreen, "PCSX2_AX %s (epoch={}, save_dir={})",
	                BuildVersion::GitRev, snap.epoch, snap.save_directory);
	Console.WriteLnFmt("Savestate version: 0x{:x}\n", g_SaveVersion);

	const CPUInfo& cpuinfo = GetCPUInfo();
	Console.WriteLnFmt("  Processor        = {}", cpuinfo.name);
	Console.WriteLnFmt("  Core Count       = {} cores", cpuinfo.num_big_cores + cpuinfo.num_small_cores);
	Console.WriteLnFmt("  Thread Count     = {} threads", cpuinfo.num_threads);
	Console.WriteLnFmt("  Cluster Count    = {} clusters", cpuinfo.num_clusters);
}

LimiterModeType VMManager::GetLimiterMode() { return g_limiter_mode; }

void VMManager::SetLimiterMode(LimiterModeType type)
{
	if (g_limiter_mode == type)
		return;
	g_limiter_mode = type;
	UpdateTargetSpeed();
}

float VMManager::GetTargetSpeed() { return g_target_speed; }

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
	float target_speed = GetTargetSpeedForLimiterMode(g_limiter_mode);

	g_target_speed_can_sync_to_host = false;
	g_target_speed_synced_to_host = false;
	g_use_vsync_for_timing = false;

	if (EmuConfig.EmulationSpeed.SyncToHostRefreshRate)
	{
		if (const std::optional<float> host_refresh_rate = GSGetHostRefreshRate(); host_refresh_rate.has_value())
		{
			const float host_to_guest_ratio = host_refresh_rate.value() / frame_rate;
			g_target_speed_can_sync_to_host = (host_to_guest_ratio >= 0.95f && host_to_guest_ratio <= 1.05f);
			g_target_speed_synced_to_host = (g_target_speed_can_sync_to_host && target_speed == 1.0f);
			target_speed = g_target_speed_synced_to_host ? host_to_guest_ratio : target_speed;
			g_use_vsync_for_timing = (g_target_speed_synced_to_host && !EmuConfig.GS.SkipDuplicateFrames && EmuConfig.GS.VsyncEnable &&
									  EmuConfig.EmulationSpeed.UseVSyncForTiming);
		}
	}

	s_limiter_ticks_per_frame =
		static_cast<s64>(static_cast<double>(GetTickFrequency()) / static_cast<double>(std::max(frame_rate * target_speed, 1.0f)));

	if (g_target_speed != target_speed)
	{
		g_target_speed = target_speed;
		MTGS::UpdateVSyncMode();
		SPU2::OnTargetSpeedChanged();
		ResetFrameLimiter();
	}
}

bool VMManager::IsTargetSpeedAdjustedToHost() { return g_target_speed_synced_to_host; }
float VMManager::GetFrameRate() { return GetVerticalFrequency(); }
void VMManager::ResetFrameLimiter() { s_limiter_frame_start = GetCPUTicks(); }

void VMManager::Internal::Throttle(bool vsync_start)
{
	if (g_target_speed == 0.0f || g_use_vsync_for_timing)
		return;

	const u64 expected_end =
		s_limiter_frame_start +
		static_cast<u64>(vsync_start ? (static_cast<double>(s_limiter_ticks_per_frame) * 0.9)
		                             : static_cast<double>(s_limiter_ticks_per_frame));
	const u64 end_tick = GetCPUTicks();
	const s64 delta_ticks = static_cast<s64>(end_tick - expected_end);

	if (delta_ticks >= s_limiter_ticks_per_frame)
	{
		s_limiter_frame_start += static_cast<u64>((delta_ticks / s_limiter_ticks_per_frame) * s_limiter_ticks_per_frame);
		return;
	}

	const s32 msec = static_cast<s32>((delta_ticks * -1000) / static_cast<s64>(GetTickFrequency()));
	if (msec > 1)
		Threading::Sleep(msec - 1);

	while (GetCPUTicks() < expected_end)
	{
	}

	if (!vsync_start)
		s_limiter_frame_start = expected_end;
}

void VMManager::Internal::FrameRateChanged() { UpdateTargetSpeed(); }

void VMManager::FrameAdvance(u32 num_frames)
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

	g_frame_advance_count = num_frames;
	SetState(VMState::Running);
}

GSVSyncMode VMManager::GetEffectiveVSyncMode()
{
	if (!EmuConfig.GS.VsyncEnable)
		return GSVSyncMode::Disabled;

	const VMState state = GetState();
	const bool valid_vm = (state != VMState::Shutdown && state != VMState::Stopping);
	if (g_target_speed_can_sync_to_host || (!valid_vm && EmuConfig.EmulationSpeed.SyncToHostRefreshRate) ||
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
	return (!valid_vm || (!g_target_speed_synced_to_host && g_target_speed != 1.0f));
}

bool VMManager::Internal::WasFastBooted() { return g_fast_boot_requested; }
bool VMManager::Internal::IsFastBootInProgress() { return g_fast_boot_requested && !HasBootedELF(); }

void VMManager::Internal::DisableFastBoot()
{
	if (!g_fast_boot_requested)
		return;
	g_fast_boot_requested = false;
	if (EmuConfig.EnableFastBootFastForward && !HasBootedELF())
		UpdateTargetSpeed();
}
