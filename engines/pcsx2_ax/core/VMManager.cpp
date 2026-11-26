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
#include "IHardwareModeAdapter.hpp"
#include "INISettingsInterface.h"
#include "ImGui/FullscreenUI.h"
#include "Input/InputManager.h"
#include "MTGS.h"
#include "MTVU.h"
#include "PINE.h"
#include "Patch.h"
#include "R3000A.h"
#include "SPU2/spu2.h"
#include "USB/USB.h"
#include "Vif_Dynarec.h"
#include "VMManager.h"

#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/FPControl.h"
#include "common/Path.h"
#include "common/ScopedGuard.h"
#include "common/SettingsWrapper.h"
#include "common/SmallString.h"
#include "common/StringUtil.h"
#include "common/Threading.h"
#include "common/Timer.h"
#include "common/emitter/x86emitter.h"

#include "cpuinfo.h"
#include "discord_rpc.h"
#include "fmt/format.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

#ifdef _WIN32
#include "common/RedtapeWindows.h"
#endif

namespace pcsx2_ax::vm_bridge {

namespace {

std::atomic<pcsx2_ax::core::VmContext*> g_active_ctx{nullptr};
std::mutex g_media_path_lock{};
std::atomic<uint64_t> g_snapshot_seq{0u};
std::atomic<uint64_t> g_slot_epochs[pcsx2_ax::core::kTelemetryRingCapacity]{};

void publish_snapshot(pcsx2_ax::core::VmContext& ctx, uint64_t new_epoch) noexcept
{
	const uint32_t next_slot = 1u - ctx.active_snapshot_slot.load(std::memory_order_acquire);
	auto* adp = ctx.adapter.load(std::memory_order_acquire);

	// Odd sequence indicates an in-flight snapshot update.
	g_snapshot_seq.fetch_add(1u, std::memory_order_acq_rel);
	ctx.snapshots[next_slot] = (adp != nullptr)
		? adp->capture_snapshot(new_epoch)
		: pcsx2_ax::core::ModeEpochSnapshot{.epoch = new_epoch};
	ctx.active_snapshot_slot.store(next_slot, std::memory_order_release);
	g_snapshot_seq.fetch_add(1u, std::memory_order_release);
	g_snapshot_seq.notify_all();
}

[[nodiscard]] constexpr uint64_t compute_crc64_sample(const pcsx2_ax::core::FrameTelemetrySample& s) noexcept
{
	uint64_t h = 0xcbf29ce484222325ULL;
	auto mix = [&h](uint64_t v) noexcept {
		h ^= v;
		h *= 0x100000001b3ULL;
	};
	mix(s.frame_index);
	mix(s.epoch);
	mix(s.ee_cycles);
	mix(s.vu1_cycles);
	mix(s.gs_cycles);
	mix(static_cast<uint64_t>(s.cdvd_cache_hits) | (static_cast<uint64_t>(s.cdvd_cache_misses) << 32));
	return h;
}

} // namespace

pcsx2_ax::core::VmContext& default_context() noexcept
{
	static pcsx2_ax::core::VmContext fallback{};
	return fallback;
}

void set_active_context(pcsx2_ax::core::VmContext* ctx) noexcept
{
	g_active_ctx.store(ctx, std::memory_order_release);
}

pcsx2_ax::core::VmContext& active_context() noexcept
{
	auto* ctx = g_active_ctx.load(std::memory_order_acquire);
	return (ctx != nullptr) ? *ctx : default_context();
}

uint64_t current_epoch(const pcsx2_ax::core::VmContext& ctx) noexcept
{
	return ctx.epoch.load(std::memory_order_acquire);
}

uint64_t advance_epoch(pcsx2_ax::core::VmContext& ctx) noexcept
{
	const uint64_t next = ctx.epoch.fetch_add(1u, std::memory_order_acq_rel) + 1u;
	publish_snapshot(ctx, next);
	return next;
}

pcsx2_ax::core::ModeEpochSnapshot current_snapshot(const pcsx2_ax::core::VmContext& ctx) noexcept
{
	for (;;)
	{
		const uint64_t s1 = g_snapshot_seq.load(std::memory_order_acquire);
		if (s1 & 1u)
			continue;

		const uint32_t slot = ctx.active_snapshot_slot.load(std::memory_order_acquire);
		pcsx2_ax::core::ModeEpochSnapshot out = ctx.snapshots[slot];
		const uint64_t s2 = g_snapshot_seq.load(std::memory_order_acquire);
		if (s1 == s2 && !(s2 & 1u))
		{
			out.epoch = ctx.epoch.load(std::memory_order_acquire);
			return out;
		}
	}
}

void record_telemetry(const pcsx2_ax::core::FrameTelemetrySample& sample,
                      pcsx2_ax::core::VmContext& ctx) noexcept
{
	pcsx2_ax::core::FrameTelemetrySample stamped = sample;
	stamped.epoch = current_epoch(ctx);
	stamped.framebuffer_crc64 = compute_crc64_sample(stamped);
	if (stamped.vram_allocated_gib >= pcsx2_ax::core::kVramPruneGib)
		stamped.vram_prune_triggered = true;

	const uint64_t idx = ctx.telemetry_write_idx.fetch_add(1u, std::memory_order_acq_rel);
	const uint64_t ring_slot = idx & pcsx2_ax::core::kTelemetryRingMask;
	const uint64_t gen = idx + 1u;

	g_slot_epochs[ring_slot].store(0u, std::memory_order_release);
	ctx.telemetry_ring[ring_slot] = stamped;
	g_slot_epochs[ring_slot].store(gen, std::memory_order_release);
}

bool read_latest_telemetry(pcsx2_ax::core::FrameTelemetrySample& out_sample,
                         const pcsx2_ax::core::VmContext& ctx) noexcept
{
	const uint64_t idx = ctx.telemetry_write_idx.load(std::memory_order_acquire);
	if (idx == 0u)
		return false;

	const uint64_t ring_slot = (idx - 1u) & pcsx2_ax::core::kTelemetryRingMask;
	const uint64_t expected_gen = idx;
	if (g_slot_epochs[ring_slot].load(std::memory_order_acquire) != expected_gen)
		return false;

	out_sample = ctx.telemetry_ring[ring_slot];
	return g_slot_epochs[ring_slot].load(std::memory_order_acquire) == expected_gen;
}

void bind_mode_adapter(pcsx2_ax::core::IHardwareModeAdapter* adapter,
                       pcsx2_ax::core::VmContext& ctx) noexcept
{
	ctx.adapter.store(adapter, std::memory_order_release);
	(void)advance_epoch(ctx);
}

pcsx2_ax::core::IHardwareModeAdapter* get_mode_adapter(const pcsx2_ax::core::VmContext& ctx) noexcept
{
	return ctx.adapter.load(std::memory_order_acquire);
}

void set_media_path(std::string path, bool ready, pcsx2_ax::core::VmContext& ctx)
{
	{
		std::lock_guard<std::mutex> lock(g_media_path_lock);
		ctx.media_path = std::move(path);
	}
	ctx.media_ready.store(ready, std::memory_order_release);
	(void)advance_epoch(ctx);
}

std::string get_media_path(const pcsx2_ax::core::VmContext& ctx)
{
	std::lock_guard<std::mutex> lock(g_media_path_lock);
	return ctx.media_path;
}

bool is_media_ready(const pcsx2_ax::core::VmContext& ctx) noexcept
{
	return ctx.media_ready.load(std::memory_order_acquire);
}

void set_memcard_busy(bool busy, pcsx2_ax::core::VmContext& ctx) noexcept
{
	ctx.memcard_busy.store(busy, std::memory_order_release);
}

bool is_memcard_busy(const pcsx2_ax::core::VmContext& ctx) noexcept
{
	return ctx.memcard_busy.load(std::memory_order_acquire);
}

bool init_mode_subsystems(pcsx2_ax::core::VmContext& ctx)
{
	auto* adapter = get_mode_adapter(ctx);
	(void)advance_epoch(ctx);
	return (adapter != nullptr) ? adapter->initialize() : true;
}

bool reset_mode_subsystems(pcsx2_ax::core::VmContext& ctx)
{
	auto* adapter = get_mode_adapter(ctx);
	(void)advance_epoch(ctx);
	return (adapter != nullptr) ? adapter->reset() : true;
}

void shutdown_mode_subsystems(pcsx2_ax::core::VmContext& ctx) noexcept
{
	auto* adapter = get_mode_adapter(ctx);
	(void)advance_epoch(ctx);
	if (adapter != nullptr)
		adapter->shutdown();
}

void notify_mode_vsync(pcsx2_ax::core::VmContext& ctx) noexcept
{
	if (auto* adapter = get_mode_adapter(ctx); adapter != nullptr)
		adapter->on_vsync();
}

pcsx2_ax::core::BiosDescriptor current_bios_info(const pcsx2_ax::core::VmContext& ctx) noexcept
{
	return current_snapshot(ctx).bios;
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

bool VMManager::Internal::HasBootedELF()
{
	return VMManager::HasBootedELF();
}

bool VMManager::PerformEarlyHardwareChecks(const char** error)
{
#define COMMON_DOWNLOAD_MESSAGE "PCSX2_AX Prototype Phase build foundation."

#if defined(ARCH_X86)
	cpuinfo_initialize();
#endif
	(void)error;
	return true;

#undef COMMON_DOWNLOAD_MESSAGE
}

VMManager::VMBootResult VMManager::Initialize(const VMBootParameters& boot_params, Error* error)
{
	auto& ctx = pcsx2_ax::vm_bridge::active_context();
	g_vm_state.store(VMState::Initializing, std::memory_order_release);

	ScopedGuard shutdown_guard([&ctx]() {
		if (GSDumpReplayer::IsReplayingDump())
			GSDumpReplayer::Shutdown();

		VMManager::Internal::ResetVMHotkeyState();
		pcsx2_ax::vm_bridge::shutdown_mode_subsystems(ctx);
		g_elf_override.clear();
		g_game_settings_override.clear();
		ClearELFInfo();
		g_vm_state.store(VMState::Shutdown, std::memory_order_release);
	});

	if (!AutoDetectSource(boot_params.filename, error))
		return VMBootResult::StartupFailure;

	if (!pcsx2_ax::vm_bridge::init_mode_subsystems(ctx))
	{
		Error::SetString(error, "Failed to initialize mode adapter subsystems.");
		return VMBootResult::StartupFailure;
	}

	g_fast_boot_requested =
		(boot_params.fast_boot.value_or(static_cast<bool>(EmuConfig.EnableFastBoot)) || !g_elf_override.empty()) &&
		(pcsx2_ax::vm_bridge::is_media_ready(ctx) || !g_elf_override.empty()) &&
		!GSDumpReplayer::IsReplayingDump();

	shutdown_guard.Cancel();
	g_vm_state.store(VMState::Running, std::memory_order_release);
	return VMBootResult::OK;
}

void VMManager::Shutdown(bool save_resume_state)
{
	(void)save_resume_state;
	auto& ctx = pcsx2_ax::vm_bridge::active_context();
	g_vm_state.store(VMState::Stopping, std::memory_order_release);
	SetTimerResolutionIncreased(false);

	g_elf_override.clear();
	g_game_settings_override.clear();
	ClearELFInfo();
	pcsx2_ax::vm_bridge::set_media_path({}, false, ctx);
	ClearDiscDetails();

	Achievements::GameChanged(0, 0);
	vtlb_Shutdown();
	USBclose();
	SPU2::Close();
	pcsx2_ax::vm_bridge::set_memcard_busy(false, ctx);
	DEV9close();
	FWclose();
	pcsx2_ax::vm_bridge::shutdown_mode_subsystems(ctx);

	if (g_gs_open_on_initialize)
		MTGS::ReleaseGS();

	g_vm_state.store(VMState::Shutdown, std::memory_order_release);
}

bool VMManager::RequestReset()
{
	auto& ctx = pcsx2_ax::vm_bridge::active_context();
	if (pcsx2_ax::vm_bridge::is_memcard_busy(ctx))
	{
		Host::AddIconOSDMessage("RequestReset", ICON_FA_TRIANGLE_EXCLAMATION,
			TRANSLATE_STR("VMManager",
				"The memory card is busy, so the VM cannot be reset right now."),
			Host::OSD_WARNING_DURATION);
		return false;
	}

	g_vm_state.store(VMState::Resetting, std::memory_order_release);
	return true;
}

void VMManager::Reset()
{
	auto& ctx = pcsx2_ax::vm_bridge::active_context();
	if (THREAD_VU1)
		vu1Thread.WaitVU();
	vu1Thread.Reset();
	MTGS::WaitGS();

	const bool elf_was_changed = HasBootedELF();
	ClearELFInfo();
	if (elf_was_changed)
		HandleELFChange(false);

	Achievements::ResetClient();
	(void)pcsx2_ax::vm_bridge::reset_mode_subsystems(ctx);
	mmap_ResetBlockTracking();
	memSetExtraMemMode(EmuConfig.Cpu.ExtraMemory);
	Internal::ClearCPUExecutionCaches();
	g_vm_state.store(VMState::Running, std::memory_order_release);
}

void VMManager::Internal::VSyncOnCPUThread()
{
	static uint64_t s_frame_counter = 0u;
	auto& ctx = pcsx2_ax::vm_bridge::active_context();
	pcsx2_ax::vm_bridge::notify_mode_vsync(ctx);

	pcsx2_ax::core::FrameTelemetrySample sample{};
	sample.frame_index = ++s_frame_counter;
	sample.ee_cycles = static_cast<uint64_t>(EEsCycle);
	sample.gs_cycles = static_cast<uint64_t>(EEoCycle);
	pcsx2_ax::vm_bridge::record_telemetry(sample, ctx);

	Patch::ApplyVsyncPatches();

	if (g_frame_advance_count > 0)
	{
		--g_frame_advance_count;
		if (g_frame_advance_count == 0)
			SetState(VMState::Paused);
	}
}

void VMManager::Internal::PollInputOnCPUThread()
{
	InputManager::PollSources();
}

bool VMManager::Internal::IsExecutionInterrupted()
{
	return g_vm_state.load(std::memory_order_relaxed) != VMState::Running || g_cpu_implementation_changed;
}
}
