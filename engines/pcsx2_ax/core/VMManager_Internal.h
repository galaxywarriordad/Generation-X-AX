// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "IHardwareModeAdapter.hpp"
#include "VMManager.h"

#include "common/Error.h"
#include "common/Threading.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

struct ArchiveEntryList;
struct SaveStateScreenshotData;
class SettingsInterface;

namespace pcsx2_ax::vm_bridge {

void bind_mode_adapter(pcsx2_ax::core::IHardwareModeAdapter* adapter) noexcept;
[[nodiscard]] pcsx2_ax::core::IHardwareModeAdapter* get_mode_adapter() noexcept;
[[nodiscard]] uint64_t current_epoch() noexcept;
[[nodiscard]] uint64_t advance_epoch() noexcept;
[[nodiscard]] pcsx2_ax::core::ModeEpochSnapshot current_snapshot() noexcept;

void set_media_path(std::string path, bool ready);
[[nodiscard]] const std::string& get_media_path() noexcept;
[[nodiscard]] bool is_media_ready() noexcept;

void set_memcard_busy(bool busy) noexcept;
[[nodiscard]] bool is_memcard_busy() noexcept;

[[nodiscard]] bool init_mode_subsystems();
[[nodiscard]] bool reset_mode_subsystems();
void shutdown_mode_subsystems() noexcept;
void notify_mode_vsync() noexcept;
[[nodiscard]] pcsx2_ax::core::BiosDescriptor current_bios_info() noexcept;

} // namespace pcsx2_ax::vm_bridge

namespace VMManager {

extern std::atomic<VMState> g_vm_state;
extern bool g_cpu_implementation_changed;
extern Threading::ThreadHandle g_vm_thread_handle;
extern std::string g_elf_override;
extern std::string g_game_settings_override;
extern u32 g_frame_advance_count;
extern bool g_fast_boot_requested;
extern bool g_gs_open_on_initialize;
extern LimiterModeType g_limiter_mode;
extern float g_target_speed;
extern bool g_target_speed_can_sync_to_host;
extern bool g_target_speed_synced_to_host;
extern bool g_use_vsync_for_timing;

void LogCPUCapabilities();
void InitializeCPUProviders();
void ShutdownCPUProviders();
void UpdateCPUImplementations();

bool AutoDetectSource(const std::string& filename, Error* error = nullptr);
void UpdateDiscDetails(bool booting);
void ClearDiscDetails();
void HandleELFChange(bool verbose_patches_if_changed);
void UpdateELFInfo(std::string elf_path);
void ClearELFInfo();
void ReportGameChangeToHost();
bool HasBootedELF();
std::string GetDiscOverrideFromGameSettings(const std::string& elf_path);

void LoadSettings();
void LoadCoreSettings(SettingsInterface& si);
void ApplySettings();
void ApplyCoreSettings();
bool UpdateGameSettingsLayer();
void LoadInputBindings(SettingsInterface& si, std::unique_lock<std::mutex>& lock);
void ApplyGameFixes();
void EnforceAchievementsChallengeModeSettings();
void LogUnsafeSettingsToConsole(const std::string& messages);
void WarnAboutUnsafeSettings();
void CheckForConfigChanges(const Pcsx2Config& old_config);
void CheckForCPUConfigChanges(const Pcsx2Config& old_config);
void CheckForGSConfigChanges(const Pcsx2Config& old_config);
void CheckForEmulationSpeedConfigChanges(const Pcsx2Config& old_config);
void CheckForPatchConfigChanges(const Pcsx2Config& old_config);
void CheckForDEV9ConfigChanges(const Pcsx2Config& old_config);
void CheckForMiscConfigChanges(const Pcsx2Config& old_config);
void SetDefaultLoggingSettings(SettingsInterface& si);
void UpdateLoggingSettings(SettingsInterface& si);
void UpdateInhibitScreensaver(bool inhibit);
void AccumulateSessionPlaytime();
void ResetResumeTimestamp();
void SaveSessionTime(const std::string& prev_serial);
u64 GetSessionPlayedTime();
void ReloadPINE();
float GetTargetSpeedForLimiterMode(LimiterModeType mode);
void UpdateTargetSpeed();
bool IsTargetSpeedAdjustedToHost();
float GetFrameRate();
void ResetFrameLimiter();
void SetTimerResolutionIncreased(bool enabled);
void SetHardwareDependentDefaultSettings(SettingsInterface& si);
void EnsureCPUInfoInitialized();
void SetEmuThreadAffinities();
void InitializeDiscordPresence();
void ShutdownDiscordPresence();
void UpdateDiscordPresence(bool update_session_time);
void PollDiscordPresence();

std::string GetSaveStateFileName(const char* game_serial, u32 game_crc, s32 slot, bool backup = false);
std::string GetSaveStateFileName(const char* filename, s32 slot, bool backup = false);
bool HasSaveStateInSlot(const char* game_serial, u32 game_crc, s32 slot);
std::string GetCurrentSaveStateFileName(s32 slot, bool backup = false);
bool DoLoadState(const char* filename, Error* error = nullptr);
void DoSaveState(const char* filename, s32 slot_for_message, bool zip_on_thread, bool backup_old_state,
                 std::function<void(const std::string&)> error_callback);
void ZipSaveState(std::unique_ptr<ArchiveEntryList> elist, std::unique_ptr<SaveStateScreenshotData> screenshot,
                  const char* filename, s32 slot_for_message, std::function<void(const std::string&)> error_callback);
void ZipSaveStateOnThread(std::unique_ptr<ArchiveEntryList> elist, std::unique_ptr<SaveStateScreenshotData> screenshot,
                          std::string filename, s32 slot_for_message, std::function<void(const std::string&)> error_callback);
void WaitForSaveStateFlush();
u32 DeleteSaveStates(const char* game_serial, u32 game_crc, bool also_backups = true);
bool LoadState(const char* filename, Error* error = nullptr);
bool LoadStateFromSlot(s32 slot, bool backup, Error* error = nullptr);
void SaveState(const char* filename, bool zip_on_thread, bool backup_old_state,
               std::function<void(const std::string&)> error_callback);
void SaveStateToSlot(s32 slot, bool zip_on_thread, std::function<void(const std::string&)> error_callback);

} // namespace VMManager
