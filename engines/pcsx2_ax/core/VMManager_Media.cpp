// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team & The Silicon Preservation Society
// SPDX-License-Identifier: GPL-3.0+

#include "VMManager_Internal.h"

#include "Achievements.h"
#include "CDVD/IsoReader.h"
#include "Elfheader.h"
#include "GameDatabase.h"
#include "GameList.h"
#include "GSDumpReplayer.h"
#include "Host.h"
#include "ImGui/FullscreenUI.h"
#include "MTGS.h"
#include "Patch.h"

#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/StringUtil.h"

#include "fmt/format.h"

#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace {

std::recursive_mutex s_info_mutex;
std::string s_disc_serial;
std::string s_disc_elf;
std::string s_disc_version;
std::string s_title;
std::string s_title_en_search;
std::string s_title_en_replace;
std::string s_cur_region;
u32 s_disc_crc = 0;
u32 s_current_crc = 0;
u32 s_elf_entry_point = 0xFFFFFFFFu;
std::string s_elf_path;
std::pair<u32, u32> s_elf_text_range{};
bool s_elf_executed = false;

} // namespace

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

std::string VMManager::GetSerialForGameSettings()
{
	std::unique_lock lock(s_info_mutex);
	return g_elf_override.empty() ? s_disc_serial : std::string();
}

void VMManager::UpdateDiscDetails(bool booting)
{
	const auto snap = pcsx2_ax::vm_bridge::current_snapshot();
	const auto& bios = snap.bios;
	const std::string media_path = pcsx2_ax::vm_bridge::get_media_path();
	const bool media_ready = pcsx2_ax::vm_bridge::is_media_ready();

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
			s_disc_elf.clear();
			s_disc_version.clear();
			s_cur_region = "NTSC";
			serial_is_valid = !s_disc_serial.empty();
		}
		else if (media_ready)
		{
			s_disc_serial = Path::GetFileTitle(media_path);
			s_disc_version = fmt::format("{:04X}", bios.rom_version);
			s_cur_region = bios.region_code.empty() ? "NTSC" : std::string(bios.region_code);
			serial_is_valid = !s_disc_serial.empty();
		}
		else if (!g_elf_override.empty())
		{
			s_disc_serial = Path::GetFileTitle(g_elf_override);
			s_disc_version.clear();
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
		std::string custom_title = GameList::GetCustomTitleForPath(media_path);
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
				title = !g_elf_override.empty()
					? fmt::format("{} [{}]", game_title, Path::GetFileTitle(g_elf_override))
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
		UpdateDiscordPresence(g_vm_state.load(std::memory_order_relaxed) == VMState::Initializing);
	}
}

void VMManager::ClearDiscDetails()
{
	std::unique_lock lock(s_info_mutex);
	s_disc_crc = 0;
	s_title.clear();
	s_disc_version.clear();
	s_disc_elf.clear();
	s_disc_serial.clear();
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
		s_elf_path.clear();
		s_elf_text_range = {};
		s_elf_entry_point = 0xFFFFFFFFu;
		s_current_crc = 0;
		return;
	}

	ElfObject elfo;
	if (!elfo.OpenFile(elf_path, false, nullptr))
	{
		Console.Error(fmt::format("Failed to read ELF being loaded: {}", elf_path));
		s_elf_path.clear();
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
}

void VMManager::ClearELFInfo()
{
	s_current_crc = 0;
	s_elf_executed = false;
	s_elf_text_range = {};
	s_elf_path.clear();
	s_elf_entry_point = 0xFFFFFFFFu;
}

void VMManager::ReportGameChangeToHost()
{
	const std::string disc_path = pcsx2_ax::vm_bridge::get_media_path();
	const u32 crc_to_report = HasBootedELF() ? s_current_crc : 0;
	const bool prefer_english = Host::GetBaseBoolSettingValue("UI", "PreferEnglishGameList", false);
	const std::string game_title = GetTitle(prefer_english);
	FullscreenUI::GameChanged(disc_path, s_disc_serial, game_title, s_disc_crc, crc_to_report);
	Host::OnGameChanged(game_title, g_elf_override, disc_path, s_disc_serial, s_disc_crc, crc_to_report);
}

bool VMManager::HasBootedELF()
{
	return s_current_crc != 0 && s_elf_executed;
}

bool VMManager::Internal::HasBootedELF()
{
	return VMManager::HasBootedELF();
}

u32 VMManager::Internal::GetCurrentELFEntryPoint()
{
	return s_elf_entry_point;
}

const std::string& VMManager::Internal::GetELFOverride()
{
	return g_elf_override;
}

std::string VMManager::Internal::GetCurrentRegion()
{
	std::unique_lock lock(s_info_mutex);
	return s_cur_region;
}

bool VMManager::AutoDetectSource(const std::string& filename, Error* error)
{
	auto& ctx = pcsx2_ax::vm_bridge::active_context();
	if (filename.empty())
	{
		pcsx2_ax::vm_bridge::set_media_path({}, false, ctx);
		return true;
	}

	if (!FileSystem::FileExists(filename.c_str()))
	{
		Error::SetStringFmt(error, TRANSLATE_FS("VMManager", "Requested filename '{}' does not exist."), filename);
		return false;
	}

	if (IsGSDumpFileName(filename))
	{
		pcsx2_ax::vm_bridge::set_media_path({}, false, ctx);
		return GSDumpReplayer::Initialize(filename.c_str(), error);
	}

	if (IsElfFileName(filename))
	{
		std::string disc_path = GetDiscOverrideFromGameSettings(filename);
		pcsx2_ax::vm_bridge::set_media_path(disc_path, !disc_path.empty(), ctx);
		g_elf_override = filename;
		return true;
	}

	pcsx2_ax::vm_bridge::set_media_path(filename, true, ctx);
	return true;
}

bool VMManager::ChangeDisc(CDVD_SourceType source, std::string path)
{
	auto& ctx = pcsx2_ax::vm_bridge::active_context();
	const std::string old_path(pcsx2_ax::vm_bridge::get_media_path(ctx));
	const bool ready = (source != CDVD_SourceType::NoDisc) && (!path.empty() || !old_path.empty());
	pcsx2_ax::vm_bridge::set_media_path(!path.empty() ? std::move(path) : old_path, ready, ctx);
	UpdateDiscDetails(false);
	return true;
}

bool VMManager::SetELFOverride(std::string path)
{
	if (!HasValidVM() || (!path.empty() && !FileSystem::FileExists(path.c_str())))
		return false;

	g_elf_override = std::move(path);
	UpdateDiscDetails(false);
	g_fast_boot_requested = !g_elf_override.empty() || EmuConfig.EnableFastBoot;
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

	s_elf_executed = true;
	HandleELFChange(true);
	Patch::ApplyBootPatches();
	mmap_ResetBlockTracking();
	ClearCPUExecutionCaches();
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
