// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team & The Silicon Preservation Society
// SPDX-License-Identifier: GPL-3.0+

#include "VMManager_Internal.h"

#include "Achievements.h"
#include "BuildVersion.h"
#include "Counters.h"
#include "DEV9/DEV9.h"
#include "DebugTools/DebugInterface.h"
#include "DebugTools/SymbolImporter.h"
#include "FW.h"
#include "GS.h"
#include "GSDumpReplayer.h"
#include "Host.h"
#include "ImGui/FullscreenUI.h"
#include "ImGui/ImGuiOverlays.h"
#include "Input/InputManager.h"
#include "MTGS.h"
#include "MTVU.h"
#include "Patch.h"
#include "PerformanceMetrics.h"
#include "R3000A.h"
#include "R5900.h"
#include "Recording/InputRecording.h"
#include "Recording/InputRecordingControls.h"
#include "SIO/Sio.h"
#include "SPU2/spu2.h"
#include "USB/USB.h"
#include "Vif_Dynarec.h"

#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/FPControl.h"
#include "common/ScopedGuard.h"
#include "common/Timer.h"
#include "common/emitter/x86emitter.h"

#include "cpuinfo.h"
#include "fmt/format.h"

#include <utility>

#ifdef _WIN32
#include "common/RedtapeWindows.h"
#include <objbase.h>
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

void bind_mode_adapter(pcsx2_ax::core::IHardwareModeAdapter* adapter) noexcept { active_adapter() = adapter; }
pcsx2_ax::core::IHardwareModeAdapter* get_mode_adapter() noexcept { return active_adapter(); }

void set_media_path(std::string path, bool ready)
{
	active_disc_file() = std::move(path);
	active_disc_ready() = ready;
}

const std::string& get_media_path() noexcept { return active_disc_file(); }
bool is_media_ready() noexcept { return active_disc_ready(); }

void set_memcard_busy(bool busy) noexcept { active_memcard_busy() = busy; }
bool is_memcard_busy() noexcept { return active_memcard_busy(); }

bool init_mode_subsystems()
{
	auto* adapter = active_adapter();
	return (adapter != nullptr) ? adapter->initialize() : true;
}

bool reset_mode_subsystems()
{
	auto* adapter = active_adapter();
	return (adapter != nullptr) ? adapter->reset() : true;
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

pcsx2_ax::core::BiosDescriptor current_bios_info() noexcept
{
	if (const auto* adapter = active_adapter(); adapter != nullptr)
		return adapter->bios_descriptor();
	return pcsx2_ax::core::BiosDescriptor{};
}

} // namespace pcsx2_ax::vm_bridge

std::atomic<VMManager::VMState> VMManager::g_vm_state{VMState::Shutdown};
bool VMManager::g_cpu_implementation_changed = false;
Threading::ThreadHandle VMManager::g_vm_thread_handle;
std::string VMManager::g_elf_override;
std::string VMManager::g_game_settings_override;
u32 VMManager::g_frame_advance_count = 0;
bool VMManager::g_fast_boot_requested = false;
bool VMManager::g_gs_open_on_initialize = false;
LimiterModeType VMManager::g_limiter_mode = LimiterModeType::Nominal;
float VMManager::g_target_speed = 0.0f;
bool VMManager::g_target_speed_can_sync_to_host = false;
bool VMManager::g_target_speed_synced_to_host = false;
bool VMManager::g_use_vsync_for_timing = false;

extern R5900cpu GSDumpReplayerCpu;

bool VMManager::PerformEarlyHardwareChecks(const char** error)
{
#define COMMON_DOWNLOAD_MESSAGE "PCSX2_AX prototype build foundation."

#if defined(ARCH_X86)
	cpuinfo_initialize();
	if (!cpuinfo_has_x86_sse4_1())
	{
		*error = "PCSX2 requires SSE4.1 support.\n\n" COMMON_DOWNLOAD_MESSAGE;
		return false;
	}
#if _M_SSE >= 0x0501
	if (!cpuinfo_has_x86_avx2())
	{
		*error = "This build requires AVX2 support.\n\n" COMMON_DOWNLOAD_MESSAGE;
		return false;
	}
#endif
#elif defined(ARCH_ARM64)
	const size_t runtime_host_page_size = HostSys::GetRuntimePageSize();
	if (__pagesize != runtime_host_page_size)
	{
		*error = "Page size mismatch.\n\n" COMMON_DOWNLOAD_MESSAGE;
		return false;
	}
#endif

#undef COMMON_DOWNLOAD_MESSAGE
	return true;
}

VMManager::VMState VMManager::GetState() { return g_vm_state.load(std::memory_order_acquire); }

void VMManager::SetState(VMState state)
{
	const VMState old_state = g_vm_state.load(std::memory_order_acquire);
	pxAssert(state != VMState::Initializing && state != VMState::Shutdown);
	SetTimerResolutionIncreased(state == VMState::Running);
	g_vm_state.store(state, std::memory_order_release);

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
	const VMState state = g_vm_state.load(std::memory_order_acquire);
	return (state >= VMState::Running && state <= VMState::Resetting);
}

bool VMManager::HasValidOrInitializingVM()
{
	const VMState state = g_vm_state.load(std::memory_order_acquire);
	return (state >= VMState::Initializing && state <= VMState::Resetting);
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
	if (std::exchange(g_cpu_implementation_changed, false))
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
	if (HasValidVM())
		SetState(paused ? VMState::Paused : VMState::Running);
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

VMManager::VMBootResult VMManager::Initialize(const VMBootParameters& boot_params, Error* error)
{
	const Common::Timer init_timer;
	if (g_vm_state.load(std::memory_order_acquire) != VMState::Shutdown)
	{
		Error::SetString(error, TRANSLATE_STR("VMManager", "The virtual machine is already running."));
		return VMBootResult::StartupFailure;
	}

	Host::CancelGameListRefresh();
	g_vm_state.store(VMState::Initializing, std::memory_order_release);
	g_vm_thread_handle = Threading::ThreadHandle::GetForCallingThread();
	Host::OnVMStarting();
	VMManager::Internal::ResetVMHotkeyState();

	ScopedGuard close_state = [] {
		if (GSDumpReplayer::IsReplayingDump())
			GSDumpReplayer::Shutdown();

		pcsx2_ax::vm_bridge::shutdown_mode_subsystems();
		g_elf_override.clear();
		g_game_settings_override.clear();
		ClearELFInfo();
		ClearDiscDetails();

		Achievements::GameChanged(0, 0);
		FullscreenUI::GameChanged({}, {}, {}, 0, 0);
		UpdateDiscordPresence(true);
		Host::OnGameChanged({}, {}, {}, {}, 0, 0);

		UpdateGameSettingsLayer();
		g_vm_state.store(VMState::Shutdown, std::memory_order_release);
		Host::OnVMDestroyed();
		ApplySettings();
	};

	std::string state_to_load;
	g_elf_override = boot_params.elf_override;
	if (!boot_params.save_state.empty())
		state_to_load = boot_params.save_state;

	if (boot_params.state_index.has_value())
	{
		if (boot_params.filename.empty())
		{
			Error::SetString(error, TRANSLATE_STR("VMManager", "Cannot load an indexed save state without a boot filename."));
			return VMBootResult::StartupFailure;
		}
		state_to_load = GetSaveStateFileName(boot_params.filename.c_str(), boot_params.state_index.value());
		if (state_to_load.empty())
		{
			Error::SetString(error, TRANSLATE_STR("VMManager", "Could not resolve path for indexed save state load."));
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
			Error::SetStringFmt(error, TRANSLATE_FS("VMManager", "Requested game config '{}' is not an INI file."), boot_params.game_config);
			return VMBootResult::StartupFailure;
		}
		if (!FileSystem::FileExists(boot_params.game_config.c_str()))
		{
			Error::SetStringFmt(error, TRANSLATE_FS("VMManager", "Requested game config '{}' does not exist."), boot_params.game_config);
			return VMBootResult::StartupFailure;
		}
		g_game_settings_override = boot_params.game_config;
	}

	UpdateDiscDetails(true);

	g_fast_boot_requested =
		(boot_params.fast_boot.value_or(static_cast<bool>(EmuConfig.EnableFastBoot)) || !g_elf_override.empty()) &&
		(pcsx2_ax::vm_bridge::is_media_ready() || !g_elf_override.empty()) &&
		!GSDumpReplayer::IsReplayingDump();

	if (boot_params.disable_achievements_hardcore_mode || GSDumpReplayer::IsReplayingDump())
		Achievements::DisableHardcoreMode();
	else
		Achievements::ResetHardcoreMode(true);

	if (Achievements::IsHardcoreModeActive() && (!state_to_load.empty() || DebugInterface::getPauseOnEntry()))
		return VMBootResult::PromptDisableHardcoreMode;

	if (boot_params.start_unlimited.value_or(false))
		g_limiter_mode = LimiterModeType::Unlimited;
	else if (boot_params.start_turbo.value_or(false))
		g_limiter_mode = LimiterModeType::Turbo;
	else
		g_limiter_mode = LimiterModeType::Nominal;

	g_target_speed = GetTargetSpeedForLimiterMode(g_limiter_mode);
	g_use_vsync_for_timing = false;
	g_cpu_implementation_changed = false;

	UpdateCPUImplementations();
	mmap_ResetBlockTracking();
	memSetExtraMemMode(EmuConfig.Cpu.ExtraMemory);
	Internal::ClearCPUExecutionCaches();
	FPControlRegister::SetCurrent(EmuConfig.Cpu.FPUFPCR);
	memBindConditionalHandlers();
	SysMemory::Reset();
	cpuReset();

	Console.WriteLn("Opening GS...");
	g_gs_open_on_initialize = MTGS::IsOpen();
	if (!g_gs_open_on_initialize && !MTGS::WaitForOpen())
	{
		Error::SetString(error, TRANSLATE_STR("VMManager", "Failed to initialize GS."));
		return VMBootResult::StartupFailure;
	}
	ScopedGuard close_gs = []() {
		if (!g_gs_open_on_initialize)
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
	g_vm_state.store(VMState::Paused, std::memory_order_release);
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
	g_vm_state.store(VMState::Stopping, std::memory_order_release);
	SetTimerResolutionIncreased(false);

	if (THREAD_VU1)
		vu1Thread.WaitVU();
	MTGS::WaitGS();

	if (!GSDumpReplayer::IsReplayingDump() && save_resume_state)
	{
		const std::string resume_file_name(GetCurrentSaveStateFileName(-1));
		if (!resume_file_name.empty())
		{
			DoSaveState(resume_file_name.c_str(), -1, true, false, [](const std::string& err) {
				Host::AddIconOSDMessage("SaveResumeState", ICON_FA_TRIANGLE_EXCLAMATION,
					fmt::format(TRANSLATE_FS("VMManager", "Failed to save resume state: {}"), err), Host::OSD_QUICK_DURATION);
			});
		}
	}

	if (g_InputRecording.isActive())
		g_InputRecording.stop();

	g_elf_override.clear();
	g_game_settings_override.clear();
	ClearELFInfo();
	pcsx2_ax::vm_bridge::set_media_path({}, false);
	ClearDiscDetails();

	Achievements::GameChanged(0, 0);
	FullscreenUI::GameChanged({}, {}, {}, 0, 0);
	UpdateDiscordPresence(true);
	Host::OnGameChanged({}, {}, {}, {}, 0, 0);

	g_fast_boot_requested = false;
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

	if (g_gs_open_on_initialize)
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

	g_vm_state.store(VMState::Shutdown, std::memory_order_release);
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
			TRANSLATE_STR("VMManager", "The memory card is busy, so the reset operation has been cancelled to prevent data loss."),
			Host::OSD_WARNING_DURATION);
		return false;
	}
	VMManager::Reset();
	return true;
}

void VMManager::Reset()
{
	pxAssert(HasValidVM());

	if (g_vm_state.load(std::memory_order_acquire) == VMState::Running)
	{
		g_vm_state.store(VMState::Resetting, std::memory_order_release);
		return;
	}

	if (!Achievements::ConfirmSystemReset())
		return;

	if (!GSDumpReplayer::IsReplayingDump() && Achievements::ResetHardcoreMode(false))
		ApplySettings();

	vu1Thread.WaitVU();
	vu1Thread.Reset();
	MTGS::WaitGS();

	const bool elf_was_changed = HasBootedELF();
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

	if (g_vm_state.load(std::memory_order_acquire) == VMState::Resetting)
		g_vm_state.store(VMState::Running, std::memory_order_release);
}

void VMManager::Internal::VSyncOnCPUThread()
{
	pcsx2_ax::vm_bridge::notify_mode_vsync();
	Patch::ApplyVsyncPatches();

	if (g_frame_advance_count > 0 && --g_frame_advance_count == 0)
		SetState(VMState::Paused);

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

bool VMManager::Internal::IsExecutionInterrupted()
{
	return g_vm_state.load(std::memory_order_relaxed) != VMState::Running || g_cpu_implementation_changed;
}
