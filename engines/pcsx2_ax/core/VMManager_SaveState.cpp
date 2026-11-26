// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team & The Silicon Preservation Society
// SPDX-License-Identifier: GPL-3.0+

#include "Achievements.h"
#include "GS/GSCapture.h"
#include "GSDumpReplayer.h"
#include "GameList.h"
#include "Host.h"
#include "IHardwareModeAdapter.hpp"
#include "MTGS.h"
#include "Recording/InputRecording.h"
#include "SaveState.h"
#include "VMManager.h"

#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/Timer.h"

#include "IconsFontAwesome.h"
#include "fmt/format.h"

#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace pcsx2_ax::vm_bridge {
[[nodiscard]] pcsx2_ax::core::IHardwareModeAdapter* get_mode_adapter() noexcept;
[[nodiscard]] bool is_memcard_busy() noexcept;
} // namespace pcsx2_ax::vm_bridge

namespace VMManager {

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
void Reset();

} // namespace VMManager

namespace {

std::deque<std::thread> s_save_state_threads;
std::mutex s_save_state_threads_mutex;

[[nodiscard]] std::filesystem::path resolve_mode_save_dir()
{
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

[[nodiscard]] bool atomic_commit_file(const std::filesystem::path& final_path) noexcept
{
#if defined(__unix__) || defined(__APPLE__)
	std::error_code ec;
	const std::filesystem::path tmp_path =
		final_path.parent_path() / (final_path.filename().string() + ".tmp");
	if (!std::filesystem::exists(tmp_path, ec))
		return true;

	const int fd = ::open(tmp_path.c_str(), O_RDONLY);
	if (fd >= 0)
	{
		(void)::fsync(fd);
		::close(fd);
	}

	std::filesystem::rename(tmp_path, final_path, ec);
	if (ec)
	{
		std::filesystem::remove(tmp_path, ec);
		return false;
	}

	const int dir_fd = ::open(final_path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
	if (dir_fd >= 0)
	{
		(void)::fsync(dir_fd);
		::close(dir_fd);
	}
	return true;
#else
	(void)final_path;
	return true;
#endif
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

void VMManager::ZipSaveState(std::unique_ptr<ArchiveEntryList> elist, std::unique_ptr<SaveStateScreenshotData> screenshot,
                             const char* filename, s32 slot_for_message, std::function<void(const std::string&)> error_callback)
{
	const Common::Timer timer;

	Error error;
	if (!SaveState_ZipToDisk(std::move(elist), std::move(screenshot), filename, &error))
	{
		error_callback(error.GetDescription());
		return;
	}

	if (!atomic_commit_file(std::filesystem::path(filename)))
	{
		error_callback(fmt::format("Failed atomic commit for '{}'", filename));
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
