// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team & The Silicon Preservation Society
// SPDX-License-Identifier: GPL-3.0+

#include "VMManager_Internal.h"

#include "Achievements.h"
#include "GS/GSCapture.h"
#include "GSDumpReplayer.h"
#include "GameList.h"
#include "Host.h"
#include "IHardwareModeAdapter.hpp"
#include "MTGS.h"
#include "Recording/InputRecording.h"
#include "SaveState.h"

#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/Timer.h"

#include "IconsFontAwesome.h"
#include "fmt/format.h"

#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

struct WorkerEntry {
	std::thread thread;
	bool completed{false};
};

std::vector<WorkerEntry> s_save_state_workers;
std::mutex s_save_state_workers_mutex;

void reap_completed_workers_locked()
{
	for (auto it = s_save_state_workers.begin(); it != s_save_state_workers.end();)
	{
		if (it->completed && it->thread.joinable())
		{
			it->thread.join();
			it = s_save_state_workers.erase(it);
		}
		else
		{
			++it;
		}
	}
}

[[nodiscard]] std::filesystem::path resolve_mode_save_dir()
{
	const auto snap = pcsx2_ax::vm_bridge::current_snapshot();
	if (!snap.save_directory.empty())
		return std::filesystem::path(snap.save_directory);

	if (const auto* adapter = pcsx2_ax::vm_bridge::get_mode_adapter(); adapter != nullptr)
	{
		const auto dir = adapter->save_directory();
		if (!dir.empty())
			return std::filesystem::path(dir);

		return (adapter->mode() == pcsx2_ax::core::HardwareMode::CohHArcade)
			? std::filesystem::path("/userdata/saves/pcsx2x6/")
			: std::filesystem::path("/userdata/saves/ps2/");
	}

	if (!EmuFolders::Savestates.empty())
		return std::filesystem::path(EmuFolders::Savestates);

	return std::filesystem::path("/userdata/saves/ps2/");
}

} // namespace

std::string VMManager::GetSaveStateFileName(const char* game_serial, u32 game_crc, s32 slot, bool backup)
{
	if (game_serial == nullptr || std::strlen(game_serial) == 0)
		return {};

	std::string stem;
	if (slot < 0)
		stem = fmt::format("{} ({:08X}).resume.p2s", game_serial, game_crc);
	else if (backup)
		stem = fmt::format("{} ({:08X}).{:02d}.p2s.backup", game_serial, game_crc, slot);
	else
		stem = fmt::format("{} ({:08X}).{:02d}.p2s", game_serial, game_crc, slot);

	return (resolve_mode_save_dir() / stem).string();
}

std::string VMManager::GetSaveStateFileName(const char* filename, s32 slot, bool backup)
{
	pxAssertRel(!HasValidVM(), "Should not have a VM when calling the non-gamelist GetSaveStateFileName()");

	std::string serial;
	u32 crc = 0;
	if (GameList::GetSerialAndCRCForFilename(filename, &serial, &crc))
		return GetSaveStateFileName(serial.c_str(), crc, slot, backup);

	return {};
}

bool VMManager::HasSaveStateInSlot(const char* game_serial, u32 game_crc, s32 slot)
{
	const std::string filename = GetSaveStateFileName(game_serial, game_crc, slot);
	return !filename.empty() && FileSystem::FileExists(filename.c_str());
}

std::string VMManager::GetCurrentSaveStateFileName(s32 slot, bool backup)
{
	const std::string serial = GetDiscSerial();
	const u32 crc = GetDiscCRC();
	return GetSaveStateFileName(serial.c_str(), crc, slot, backup);
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

void VMManager::DoSaveState(const char* filename, s32 slot_for_message, bool zip_on_thread, bool backup_old_state,
                            std::function<void(const std::string&)> error_callback)
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
		const std::string backup_filename = fmt::format("{}.backup", filename);
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
		std::unique_lock lock(s_save_state_workers_mutex);
		reap_completed_workers_locked();
		s_save_state_workers.emplace_back();
		WorkerEntry& entry = s_save_state_workers.back();
		entry.thread = std::thread([&entry,
		                            elist = std::move(elist),
		                            screenshot = std::move(screenshot),
		                            dest = std::string(filename),
		                            slot_for_message,
		                            error_callback = std::move(error_callback)]() mutable {
			ZipSaveState(std::move(elist), std::move(screenshot), dest.c_str(), slot_for_message, std::move(error_callback));
			std::lock_guard<std::mutex> wlock(s_save_state_workers_mutex);
			entry.completed = true;
		});
	}
	else
	{
		ZipSaveState(std::move(elist), std::move(screenshot), filename, slot_for_message, std::move(error_callback));
	}

	Host::OnSaveStateSaved(filename);
}

void VMManager::ZipSaveState(std::unique_ptr<ArchiveEntryList> elist, std::unique_ptr<SaveStateScreenshotData> screenshot,
                             const char* filename, s32 slot_for_message, std::function<void(const std::string&)> error_callback)
{
	const Common::Timer timer;

	// SaveState_ZipToDisk performs epoch-checked .tmp -> fsync -> rename commit.
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

void VMManager::ZipSaveStateOnThread(std::unique_ptr<ArchiveEntryList> elist, std::unique_ptr<SaveStateScreenshotData> screenshot,
                                     std::string filename, s32 slot_for_message, std::function<void(const std::string&)> error_callback)
{
	ZipSaveState(std::move(elist), std::move(screenshot), filename.c_str(), slot_for_message, std::move(error_callback));
}

void VMManager::WaitForSaveStateFlush()
{
	std::unique_lock lock(s_save_state_workers_mutex);
	for (auto& entry : s_save_state_workers)
	{
		if (entry.thread.joinable())
		{
			lock.unlock();
			entry.thread.join();
			lock.lock();
		}
	}
	s_save_state_workers.clear();
}

u32 VMManager::DeleteSaveStates(const char* game_serial, u32 game_crc, bool also_backups)
{
	WaitForSaveStateFlush();

	u32 deleted = 0;
	for (s32 i = -1; i <= NUM_SAVE_STATE_SLOTS; ++i)
	{
		std::string filename = GetSaveStateFileName(game_serial, game_crc, i);
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

void VMManager::SaveState(const char* filename, bool zip_on_thread, bool backup_old_state,
                          std::function<void(const std::string&)> error_callback)
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
	const std::string filename = GetCurrentSaveStateFileName(slot);
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
