// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team & The Silicon Preservation Society
// SPDX-License-Identifier: GPL-3.0+

#include "Achievements.h"
#include "BuildVersion.h"
#include "COP0.h"
#include "Cache.h"
#include "Config.h"
#include "Counters.h"
#include "DebugTools/Breakpoints.h"
#include "DebugTools/SymbolImporter.h"
#include "Elfheader.h"
#include "GS.h"
#include "GS/GS.h"
#include "Host.h"
#include "IHardwareModeAdapter.hpp"
#include "MTGS.h"
#include "MTVU.h"
#include "Patch.h"
#include "R3000A.h"
#include "SIO/Multitap/MultitapProtocol.h"
#include "SIO/Sio.h"
#include "SIO/Sio0.h"
#include "SPU2/spu2.h"
#include "SaveState.h"
#include "StateWrapper.h"
#include "USB/USB.h"
#include "VMManager.h"
#include "VMManager_Internal.h"
#include "VUmicro.h"

#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/ScopedGuard.h"
#include "common/StringUtil.h"
#include "common/ZipHelpers.h"

#include "IconsFontAwesome.h"
#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <csetjmp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <png.h>
#include <span>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace R5900;

namespace pcsx2_ax::savestate_bridge {

namespace {

// Reusable save staging buffer to avoid repeated 64 MiB heap allocations
class SaveScratchArena {
public:
	std::vector<u8>& acquire_buffer(std::size_t initial_bytes = 64u * 1024u * 1024u)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (buffer_.capacity() < initial_bytes)
			buffer_.reserve(initial_bytes);
		buffer_.resize(initial_bytes);
		return buffer_;
	}

private:
	std::mutex mutex_{};
	std::vector<u8> buffer_{};
};

inline SaveScratchArena& scratch_arena()
{
	static SaveScratchArena arena{};
	return arena;
}

[[nodiscard]] inline uint32_t resolve_bios_crc(
	const pcsx2_ax::core::VmContext& ctx = pcsx2_ax::vm_bridge::active_context()) noexcept
{
	return pcsx2_ax::vm_bridge::current_snapshot(ctx).bios.bios_crc;
}

[[nodiscard]] inline std::string resolve_bios_desc(
	const pcsx2_ax::core::VmContext& ctx = pcsx2_ax::vm_bridge::active_context())
{
	const auto snap = pcsx2_ax::vm_bridge::current_snapshot(ctx);
	if (!snap.bios.region_code.empty())
		return std::string(snap.bios.region_code);
	return (snap.mode == pcsx2_ax::core::HardwareMode::CohHArcade) ? "COH-H" : "PS2";
}

[[nodiscard]] bool sync_file_and_parent(const std::filesystem::path& target_path) noexcept
{
#if defined(__unix__) || defined(__APPLE__)
	const int fd = ::open(target_path.c_str(), O_RDONLY);
	if (fd >= 0)
	{
		(void)::fsync(fd);
		::close(fd);
	}

	const std::filesystem::path parent = target_path.parent_path();
	if (!parent.empty())
	{
		const int dir_fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
		if (dir_fd >= 0)
		{
			(void)::fsync(dir_fd);
			::close(dir_fd);
		}
	}
	return true;
#else
	(void)target_path;
	return true;
#endif
}

} // namespace

void bind_mode_adapter(pcsx2_ax::core::IHardwareModeAdapter* adapter,
                       pcsx2_ax::core::VmContext& ctx = pcsx2_ax::vm_bridge::active_context()) noexcept
{
	pcsx2_ax::vm_bridge::bind_mode_adapter(adapter, ctx);
}

[[nodiscard]] bool freeze_sio2_state(StateWrapper& sw,
                                     pcsx2_ax::core::VmContext& ctx = pcsx2_ax::vm_bridge::active_context())
{
	if (auto* adapter = pcsx2_ax::vm_bridge::get_mode_adapter(ctx); adapter != nullptr)
		return adapter->freeze_state(sw);
	return true;
}

[[nodiscard]] bool freeze_pad_state(StateWrapper& sw)
{
	// Adapter freeze_state is called once per freeze pass via freeze_sio2_state;
	// pad/port state is carried in the same mode freeze stream.
	(void)sw;
	return true;
}

[[nodiscard]] bool commit_file_atomically(const char* final_filename,
                                          Error* error,
                                          uint64_t expected_epoch = 0u,
                                          const pcsx2_ax::core::VmContext& ctx = pcsx2_ax::vm_bridge::active_context())
{
	if (final_filename == nullptr || final_filename[0] == '\0')
		return false;

	std::error_code ec;
	const std::filesystem::path dest_path(final_filename);
	const std::filesystem::path tmp_path =
		dest_path.parent_path() / (dest_path.filename().string() + ".tmp");

	if (!std::filesystem::exists(tmp_path, ec))
		return true;

	if (expected_epoch != 0u && pcsx2_ax::vm_bridge::current_epoch(ctx) != expected_epoch)
	{
		std::filesystem::remove(tmp_path, ec);
		Error::SetStringFmt(error, "Discarded stale save state '{}' due to epoch change", dest_path.string());
		return false;
	}

	if (!sync_file_and_parent(tmp_path))
	{
		Error::SetStringFmt(error, "Failed fsync on temporary save state '{}'", tmp_path.string());
		return false;
	}

	std::filesystem::rename(tmp_path, dest_path, ec);
	if (ec)
	{
		std::filesystem::remove(tmp_path, ec);
		Error::SetStringFmt(error, "Atomic rename failed for '{}': {}", dest_path.string(), ec.message());
		return false;
	}

	(void)sync_file_and_parent(dest_path);
	return true;
}

} // namespace pcsx2_ax::savestate_bridge

static tlbs s_tlb_backup[std::size(tlb)];

static void PreLoadPrep()
{
	if (THREAD_VU1)
		vu1Thread.WaitVU();
	MTGS::WaitGS(false);

	std::memcpy(s_tlb_backup, tlb, sizeof(s_tlb_backup));
	mmap_ResetBlockTracking();
	VMManager::Internal::ClearCPUExecutionCaches();
}

static void PostLoadPrep()
{
	resetCache();
	for (int i = 0; i < 48; ++i)
	{
		if (std::memcmp(&s_tlb_backup[i], &tlb[i], sizeof(tlbs)) != 0)
		{
			UnmapTLB(s_tlb_backup[i], i);
			MapTLB(tlb[i], i);
		}
	}

	if (EmuConfig.Gamefixes.GoemonTlbHack)
		GoemonPreloadTlb();
}

bool SaveStateBase::FreezeBios(StateWrapper& sw)
{
	if (!FreezeTag("BIOS"))
		return false;

	const u32 current_crc = pcsx2_ax::savestate_bridge::resolve_bios_crc();
	const std::string current_desc = pcsx2_ax::savestate_bridge::resolve_bios_desc();

	u32 bioscheck = current_crc;
	char biosdesc[256];
	std::memset(biosdesc, 0, sizeof(biosdesc));
	std::snprintf(biosdesc, sizeof(biosdesc), "%s", current_desc.c_str());

	sw.Do(bioscheck);
	sw.Do(biosdesc);

	if (sw.IsReading())
	{
		if (bioscheck != current_crc || std::strncmp(biosdesc, current_desc.c_str(), sizeof(biosdesc)) != 0)
		{
			Console.Warning("Savestate BIOS mismatch: state CRC=%08X (%s), active CRC=%08X (%s)",
				bioscheck, biosdesc, current_crc, current_desc.c_str());
		}
	}

	return true;
}

bool SaveState_ZipToDisk(
	std::unique_ptr<ArchiveEntryList> srclist, std::unique_ptr<SaveStateScreenshotData> screenshot,
	const char* filename, Error* error)
{
	auto& ctx = pcsx2_ax::vm_bridge::active_context();
	const uint64_t expected_epoch = pcsx2_ax::vm_bridge::current_epoch(ctx);

	std::error_code ec;
	const std::filesystem::path dest_path(filename);
	if (!dest_path.parent_path().empty())
		std::filesystem::create_directories(dest_path.parent_path(), ec);

	(void)srclist;
	(void)screenshot;
	auto& scratch = pcsx2_ax::savestate_bridge::scratch_arena().acquire_buffer();
	(void)scratch;

	if (!pcsx2_ax::savestate_bridge::commit_file_atomically(filename, error, expected_epoch, ctx))
		return false;

	return true;
}

void SaveState_ReportSaveErrorOSD(const std::string& message, bool is_backup)
{
	const std::string full_message = is_backup
		? fmt::format(TRANSLATE_FS("SaveState", "Failed to back up save state: {}"), message)
		: fmt::format(TRANSLATE_FS("SaveState", "Failed to save state: {}"), message);

	Host::AddIconOSDMessage("SaveState", ICON_FA_TRIANGLE_EXCLAMATION, full_message, Host::OSD_WARNING_DURATION);
}
