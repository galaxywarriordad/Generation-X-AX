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
#include <cstring>
#include <filesystem>
#include <limits>
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

inline pcsx2_ax::core::IHardwareModeAdapter*& active_adapter() noexcept
{
	static pcsx2_ax::core::IHardwareModeAdapter* adapter = nullptr;
	return adapter;
}

inline uint32_t& active_bios_crc() noexcept
{
	static uint32_t crc = 0u;
	return crc;
}

inline std::array<char, 256>& active_bios_desc_buf() noexcept
{
	static std::array<char, 256> buf{"PS2/COH-H Runtime"};
	return buf;
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
#else
	(void)target_path;
#endif
	return true;
}

} // namespace

void bind_mode_adapter(pcsx2_ax::core::IHardwareModeAdapter* adapter) noexcept
{
	active_adapter() = adapter;
	auto& desc_buf = active_bios_desc_buf();
	desc_buf.fill('\0');

	if (adapter != nullptr)
	{
		const auto info = adapter->bios_descriptor();
		active_bios_crc() = info.bios_crc;
		const std::string_view src = info.region_code.empty()
			? ((adapter->mode() == pcsx2_ax::core::HardwareMode::CohHArcade) ? std::string_view("COH-H") : std::string_view("PS2"))
			: info.region_code;
		const std::size_t n = std::min<std::size_t>(src.size(), desc_buf.size() - 1u);
		std::memcpy(desc_buf.data(), src.data(), n);
	}
	else
	{
		active_bios_crc() = 0u;
		std::memcpy(desc_buf.data(), "PS2/COH-H Runtime", 17u);
	}
}

[[nodiscard]] bool freeze_mode_state(StateWrapper& sw)
{
	if (auto* adapter = active_adapter(); adapter != nullptr)
		return adapter->freeze_state(sw);
	return true;
}

[[nodiscard]] bool commit_file_atomically(const char* final_filename, Error* error)
{
	if (final_filename == nullptr || final_filename[0] == '\0')
		return false;

	std::error_code ec;
	const std::filesystem::path dest_path(final_filename);
	const std::filesystem::path tmp_path =
		dest_path.parent_path() / (dest_path.filename().string() + ".tmp");

	if (!std::filesystem::exists(tmp_path, ec))
		return true;

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

	CBreakPoints::SetSkipFirst(BREAKPOINT_EE, 0);
	CBreakPoints::SetSkipFirst(BREAKPOINT_IOP, 0);
	UpdateVSyncRate(true);

	if (VMManager::Internal::HasBootedELF())
		R5900SymbolImporter.OnElfLoadedInMemory();
}

SaveStateBase::SaveStateBase(VmStateBuffer& memblock)
	: m_memory(memblock)
	, m_version(g_SaveVersion)
{
}

void SaveStateBase::PrepBlock(int size)
{
	if (m_error)
		return;

	const int end = m_idx + size;
	if (IsSaving())
	{
		if (static_cast<u32>(end) >= m_memory.size())
			m_memory.resize(static_cast<u32>(end));
	}
	else if (m_memory.size() < static_cast<u32>(end))
	{
		Console.Error("(SaveStateBase) Buffer overflow in PrepBlock(), expected %d got %zu", end, m_memory.size());
		m_error = true;
	}
}

bool SaveStateBase::FreezeTag(const char* src)
{
	if (m_error)
		return false;

	std::array<char, 32> tagspace{};
	const std::size_t src_len = std::strlen(src);
	pxAssertMsg(src_len < (tagspace.size() - 1u), "Tag name exceeds the allowed length");
	std::memcpy(tagspace.data(), src, std::min<std::size_t>(src_len, tagspace.size() - 1u));

	Freeze(tagspace.data());

	if (std::memcmp(tagspace.data(), src, std::min<std::size_t>(src_len + 1u, tagspace.size())) != 0)
	{
		Console.Error(fmt::format("Savestate data corruption detected while reading tag: {}", src));
		m_error = true;
		return false;
	}

	return true;
}

bool SaveStateBase::FreezeBios()
{
	if (!FreezeTag("BIOS"))
		return false;

	const u32 current_crc = pcsx2_ax::savestate_bridge::active_bios_crc();
	const auto& active_desc = pcsx2_ax::savestate_bridge::active_bios_desc_buf();

	u32 bioscheck = current_crc;
	std::array<char, 256> biosdesc{};
	std::memcpy(biosdesc.data(), active_desc.data(), biosdesc.size() - 1u);

	Freeze(bioscheck);
	Freeze(biosdesc.data());

	if (bioscheck != current_crc)
	{
		Console.Error("\n  Warning: BIOS Version Mismatch, savestate may be unstable!");
		Console.Error(
			"    Current BIOS:   %s (crc=0x%08x)\n"
			"    Savestate BIOS: %s (crc=0x%08x)\n",
			active_desc.data(), current_crc,
			biosdesc.data(), bioscheck);
	}

	return IsOkay();
}

bool SaveStateBase::FreezeInternals(Error* error)
{
	if (THREAD_VU1)
		Console.Warning("MTVU speedhack is enabled, saved states may not be stable");

	if (!vmFreeze())
		return false;

	if (!FreezeTag("cpuRegs"))
		return false;

	Freeze(cpuRegs);
	Freeze(psxRegs);
	Freeze(fpuRegs);
	Freeze(tlb);
	Freeze(cachedTlbs);
	Freeze(AllowParams1);
	Freeze(AllowParams2);

	if (!FreezeTag("Cycles"))
		return false;

	Freeze(EEsCycle);
	Freeze(EEoCycle);
	Freeze(nextDeltaCounter);
	Freeze(nextStartCounter);
	Freeze(psxNextStartCounter);
	Freeze(psxNextDeltaCounter);

	if (!FreezeTag("EE-Subsystems"))
		return false;

	bool okay = rcntFreeze();
	okay = okay && memFreeze(error);
	okay = okay && gsFreeze();
	okay = okay && vuMicroFreeze();
	okay = okay && vuJITFreeze();
	okay = okay && vif0Freeze();
	okay = okay && vif1Freeze();
	okay = okay && sifFreeze();
	okay = okay && ipuFreeze();
	okay = okay && ipuDmaFreeze();
	okay = okay && gifFreeze();
	okay = okay && gifDmaFreeze();
	okay = okay && sprFreeze();
	okay = okay && mtvuFreeze();
	if (!okay)
		return false;

	if (!FreezeTag("IOP-Subsystems"))
		return false;

	FreezeMem(iopMem->Sif, sizeof(iopMem->Sif));
	okay = okay && psxRcntFreeze();
	if (!okay)
		return false;

	{
		std::optional<StateWrapper::VectorMemoryStream> save_stream;
		std::optional<StateWrapper::ReadOnlyMemoryStream> load_stream;
		if (IsSaving())
			save_stream.emplace();
		else
			load_stream.emplace(&m_memory[m_idx], static_cast<int>(m_memory.size()) - m_idx);

		StateWrapper sw(
			IsSaving() ? static_cast<StateWrapper::IStream*>(&save_stream.value())
			           : static_cast<StateWrapper::IStream*>(&load_stream.value()),
			IsSaving() ? StateWrapper::Mode::Write : StateWrapper::Mode::Read,
			g_SaveVersion);

		okay = okay && g_Sio0.DoState(sw);
		okay = okay && pcsx2_ax::savestate_bridge::freeze_mode_state(sw);
		okay = okay && g_MultitapArr.at(0).DoState(sw);
		okay = okay && g_MultitapArr.at(1).DoState(sw);

		if (!okay || !sw.IsGood())
			return false;

		if (IsSaving())
		{
			FreezeMem(const_cast<u8*>(save_stream->GetBuffer().data()), save_stream->GetPosition());
		}
		else
		{
			const int new_idx = m_idx + static_cast<int>(load_stream->GetPosition());
			if (static_cast<size_t>(new_idx) >= m_memory.size())
				return false;
			m_idx = new_idx;
		}
	}

	okay = okay && cdrFreeze();
	okay = okay && cdvdFreeze();
	okay = okay && deci2Freeze();
	okay = okay && InputRecordingFreeze();
	okay = okay && handleFreeze();
	return okay;
}

memSavingState::memSavingState(VmStateBuffer& save_to) : SaveStateBase(save_to) {}

void memSavingState::FreezeMem(void* data, int size)
{
	if (!size)
		return;

	const int new_size = m_idx + size;
	if (static_cast<u32>(new_size) > m_memory.size())
		m_memory.resize(static_cast<u32>(new_size));

	std::memcpy(&m_memory[m_idx], data, static_cast<std::size_t>(size));
	m_idx += size;
}

memLoadingState::memLoadingState(const VmStateBuffer& load_from)
	: SaveStateBase(const_cast<VmStateBuffer&>(load_from))
{
}

void memLoadingState::FreezeMem(void* data, int size)
{
	if (static_cast<u32>(m_idx + size) > m_memory.size())
		m_error = true;

	if (m_error)
	{
		std::memset(data, 0, static_cast<std::size_t>(size));
		return;
	}

	const u8* const src = &m_memory[m_idx];
	m_idx += size;
	std::memcpy(data, src, static_cast<std::size_t>(size));
}

static const char* EntryFilename_StateVersion = "PCSX2 Savestate Version.id";
static const char* EntryFilename_Screenshot = "Screenshot.png";
static const char* EntryFilename_InternalStructures = "PCSX2 Internal Structures.dat";
static constexpr u32 STATE_PCSX2_VERSION_SIZE = 32;

struct SysState_Component
{
	const char* name;
	int (*freeze)(FreezeAction, freezeData*);
};

static int SysState_MTGSFreeze(FreezeAction mode, freezeData* fP)
{
	MTGS::FreezeData sstate = {fP, 0};
	MTGS::Freeze(mode, sstate);
	return sstate.retval;
}

static constexpr SysState_Component SPU2_{"SPU2", SPU2freeze};
static constexpr SysState_Component GS{"GS", SysState_MTGSFreeze};

static bool SysState_ComponentFreezeIn(zip_file_t* zf, SysState_Component comp)
{
	if (!zf)
		return true;

	freezeData fP = {0, nullptr};
	if (comp.freeze(FreezeAction::Size, &fP) != 0)
		fP.size = 0;

	std::unique_ptr<u8[]> data;
	if (fP.size > 0)
	{
		data = std::make_unique<u8[]>(static_cast<std::size_t>(fP.size));
		fP.data = data.get();
		if (zip_fread(zf, data.get(), static_cast<zip_uint64_t>(fP.size)) != static_cast<zip_int64_t>(fP.size))
			return false;
	}

	return comp.freeze(FreezeAction::Load, &fP) == 0;
}

static bool SysState_ComponentFreezeOut(SaveStateBase& writer, SysState_Component comp)
{
	freezeData fP = {};
	if (comp.freeze(FreezeAction::Size, &fP) != 0)
		return false;
	if (fP.size == 0)
		return true;

	const int size = fP.size;
	writer.PrepBlock(size);
	fP.data = writer.GetBlockPtr();
	if (comp.freeze(FreezeAction::Save, &fP) != 0)
		return false;

	writer.CommitBlock(size);
	return true;
}

static bool SysState_ComponentFreezeInNew(zip_file_t* zf, const char*, bool (*do_state_func)(StateWrapper&))
{
	std::vector<u8> data;
	if (zf)
	{
		if (auto optdata = ReadBinaryFileInZip(zf); optdata.has_value())
			data = std::move(optdata.value());
	}
	StateWrapper::ReadOnlyMemoryStream stream(data.empty() ? nullptr : data.data(), data.size());
	StateWrapper sw(&stream, StateWrapper::Mode::Read, g_SaveVersion);
	return do_state_func(sw);
}

static bool SysState_ComponentFreezeOutNew(SaveStateBase& writer, const char*, u32 reserve, bool (*do_state_func)(StateWrapper&))
{
	StateWrapper::VectorMemoryStream stream(reserve);
	StateWrapper sw(&stream, StateWrapper::Mode::Write, g_SaveVersion);
	if (!do_state_func(sw))
		return false;

	const int size = static_cast<int>(stream.GetBuffer().size());
	if (size > 0)
	{
		writer.PrepBlock(size);
		std::memcpy(writer.GetBlockPtr(), stream.GetBuffer().data(), static_cast<std::size_t>(size));
		writer.CommitBlock(size);
	}
	return true;
}

class BaseSavestateEntry
{
protected:
	BaseSavestateEntry() = default;
public:
	virtual ~BaseSavestateEntry() = default;
	virtual const char* GetFilename() const = 0;
	virtual bool FreezeIn(zip_file_t* zf) const = 0;
	virtual bool FreezeOut(SaveStateBase& writer) const = 0;
	virtual bool IsRequired() const = 0;
};

class MemorySavestateEntry : public BaseSavestateEntry
{
protected:
	MemorySavestateEntry() = default;
public:
	~MemorySavestateEntry() override = default;
	bool FreezeIn(zip_file_t* zf) const override;
	bool FreezeOut(SaveStateBase& writer) const override;
	bool IsRequired() const override { return true; }
protected:
	virtual u8* GetDataPtr() const = 0;
	virtual u32 GetDataSize() const = 0;
};

bool MemorySavestateEntry::FreezeIn(zip_file_t* zf) const
{
	const u32 expectedSize = GetDataSize();
	const s64 bytesRead = zip_fread(zf, GetDataPtr(), expectedSize);
	if (bytesRead != static_cast<s64>(expectedSize))
	{
		Console.WriteLn(Color_Yellow, " '%s' is incomplete (expected 0x%x bytes, loading only 0x%x bytes)",
			GetFilename(), expectedSize, static_cast<u32>(bytesRead));
	}
	return true;
}

bool MemorySavestateEntry::FreezeOut(SaveStateBase& writer) const
{
	writer.FreezeMem(GetDataPtr(), static_cast<int>(GetDataSize()));
	return writer.IsOkay();
}

class SavestateEntry_EmotionMemory final : public MemorySavestateEntry {
	const char* GetFilename() const override { return "eeMemory.bin"; }
	u8* GetDataPtr() const override { return eeMem->Main; }
	u32 GetDataSize() const override { return Ps2MemSize::ExposedRam; }
};
class SavestateEntry_IopMemory final : public MemorySavestateEntry {
	const char* GetFilename() const override { return "iopMemory.bin"; }
	u8* GetDataPtr() const override { return iopMem->Main; }
	u32 GetDataSize() const override { return Ps2MemSize::ExposedIopRam; }
};
class SavestateEntry_HwRegs final : public MemorySavestateEntry {
	const char* GetFilename() const override { return "eeHwRegs.bin"; }
	u8* GetDataPtr() const override { return eeHw; }
	u32 GetDataSize() const override { return sizeof(eeHw); }
};
class SavestateEntry_IopHwRegs final : public MemorySavestateEntry {
	const char* GetFilename() const override { return "iopHwRegs.bin"; }
	u8* GetDataPtr() const override { return iopHw; }
	u32 GetDataSize() const override { return sizeof(iopHw); }
};
class SavestateEntry_Scratchpad final : public MemorySavestateEntry {
	const char* GetFilename() const override { return "Scratchpad.bin"; }
	u8* GetDataPtr() const override { return eeMem->Scratch; }
	u32 GetDataSize() const override { return sizeof(eeMem->Scratch); }
};
class SavestateEntry_VU0mem final : public MemorySavestateEntry {
	const char* GetFilename() const override { return "vu0Memory.bin"; }
	u8* GetDataPtr() const override { return vuRegs[0].Mem; }
	u32 GetDataSize() const override { return VU0_MEMSIZE; }
};
class SavestateEntry_VU1mem final : public MemorySavestateEntry {
	const char* GetFilename() const override { return "vu1Memory.bin"; }
	u8* GetDataPtr() const override { return vuRegs[1].Mem; }
	u32 GetDataSize() const override { return VU1_MEMSIZE; }
};
class SavestateEntry_VU0prog final : public MemorySavestateEntry {
	const char* GetFilename() const override { return "vu0MicroMem.bin"; }
	u8* GetDataPtr() const override { return vuRegs[0].Micro; }
	u32 GetDataSize() const override { return VU0_PROGSIZE; }
};
class SavestateEntry_VU1prog final : public MemorySavestateEntry {
	const char* GetFilename() const override { return "vu1MicroMem.bin"; }
	u8* GetDataPtr() const override { return vuRegs[1].Micro; }
	u32 GetDataSize() const override { return VU1_PROGSIZE; }
};
class SavestateEntry_SPU2 final : public BaseSavestateEntry {
	const char* GetFilename() const override { return "SPU2.bin"; }
	bool FreezeIn(zip_file_t* zf) const override { return SysState_ComponentFreezeIn(zf, SPU2_); }
	bool FreezeOut(SaveStateBase& writer) const override { return SysState_ComponentFreezeOut(writer, SPU2_); }
	bool IsRequired() const override { return true; }
};
class SavestateEntry_USB final : public BaseSavestateEntry {
	const char* GetFilename() const override { return "USB.bin"; }
	bool FreezeIn(zip_file_t* zf) const override { return SysState_ComponentFreezeInNew(zf, "USB", &USB::DoState); }
	bool FreezeOut(SaveStateBase& writer) const override { return SysState_ComponentFreezeOutNew(writer, "USB", 16 * 1024, &USB::DoState); }
	bool IsRequired() const override { return false; }
};
class SavestateEntry_PAD final : public BaseSavestateEntry {
	const char* GetFilename() const override { return "PAD.bin"; }
	bool FreezeIn(zip_file_t* zf) const override { return SysState_ComponentFreezeInNew(zf, "PAD", &pcsx2_ax::savestate_bridge::freeze_mode_state); }
	bool FreezeOut(SaveStateBase& writer) const override { return SysState_ComponentFreezeOutNew(writer, "PAD", 16 * 1024, &pcsx2_ax::savestate_bridge::freeze_mode_state); }
	bool IsRequired() const override { return true; }
};
class SavestateEntry_GS final : public BaseSavestateEntry {
	const char* GetFilename() const override { return "GS.bin"; }
	bool FreezeIn(zip_file_t* zf) const override { return SysState_ComponentFreezeIn(zf, GS); }
	bool FreezeOut(SaveStateBase& writer) const override { return SysState_ComponentFreezeOut(writer, GS); }
	bool IsRequired() const override { return true; }
};
class SaveStateEntry_Achievements final : public BaseSavestateEntry {
	const char* GetFilename() const override { return "Achievements.bin"; }
	bool FreezeIn(zip_file_t* zf) const override {
		if (!Achievements::IsActive())
			return true;
		auto data = zf ? ReadBinaryFileInZip(zf) : std::nullopt;
		Achievements::LoadState(data.has_value() ? std::span<const u8>(data.value()) : std::span<const u8>());
		return true;
	}
	bool FreezeOut(SaveStateBase& writer) const override {
		if (!Achievements::IsActive())
			return true;
		Achievements::SaveState(writer);
		return writer.IsOkay();
	}
	bool IsRequired() const override { return false; }
};

static const std::array<std::unique_ptr<BaseSavestateEntry>, 14> SavestateEntries = {
	std::unique_ptr<BaseSavestateEntry>(new SavestateEntry_EmotionMemory),
	std::unique_ptr<BaseSavestateEntry>(new SavestateEntry_IopMemory),
	std::unique_ptr<BaseSavestateEntry>(new SavestateEntry_HwRegs),
	std::unique_ptr<BaseSavestateEntry>(new SavestateEntry_IopHwRegs),
	std::unique_ptr<BaseSavestateEntry>(new SavestateEntry_Scratchpad),
	std::unique_ptr<BaseSavestateEntry>(new SavestateEntry_VU0mem),
	std::unique_ptr<BaseSavestateEntry>(new SavestateEntry_VU1mem),
	std::unique_ptr<BaseSavestateEntry>(new SavestateEntry_VU0prog),
	std::unique_ptr<BaseSavestateEntry>(new SavestateEntry_VU1prog),
	std::unique_ptr<BaseSavestateEntry>(new SavestateEntry_SPU2),
	std::unique_ptr<BaseSavestateEntry>(new SavestateEntry_USB),
	std::unique_ptr<BaseSavestateEntry>(new SavestateEntry_PAD),
	std::unique_ptr<BaseSavestateEntry>(new SavestateEntry_GS),
	std::unique_ptr<BaseSavestateEntry>(new SaveStateEntry_Achievements),
};

std::unique_ptr<ArchiveEntryList> SaveState_DownloadState(Error* error)
{
	auto destlist = std::make_unique<ArchiveEntryList>();
	destlist->GetBuffer().reserve(64u * 1024u * 1024u);
	destlist->GetBuffer().resize(1024u * 1024u * 16u);

	memSavingState saveme(destlist->GetBuffer());
	ArchiveEntry internals(EntryFilename_InternalStructures);
	internals.SetDataIndex(saveme.GetCurrentPos());

	if (!saveme.FreezeBios())
	{
		Error::SetString(error, "FreezeBios() failed");
		return nullptr;
	}
	if (!saveme.FreezeInternals(error))
	{
		if (!error->IsValid())
			Error::SetString(error, "FreezeInternals() failed");
		return nullptr;
	}

	internals.SetDataSize(saveme.GetCurrentPos() - internals.GetDataIndex());
	destlist->Add(internals);

	for (const auto& entry : SavestateEntries)
	{
		const uint startpos = saveme.GetCurrentPos();
		if (!entry->FreezeOut(saveme))
		{
			Error::SetString(error, fmt::format("FreezeOut() failed for {}.", entry->GetFilename()));
			return nullptr;
		}
		destlist->Add(
			ArchiveEntry(entry->GetFilename())
				.SetDataIndex(startpos)
				.SetDataSize(saveme.GetCurrentPos() - startpos));
	}

	return destlist;
}

std::unique_ptr<SaveStateScreenshotData> SaveState_SaveScreenshot()
{
	u32 width = 0, height = 0;
	std::vector<u32> pixels;
	if (!MTGS::SaveMemorySnapshot(640, 480, true, false, &width, &height, &pixels))
		return nullptr;

	auto data = std::make_unique<SaveStateScreenshotData>();
	data->width = width;
	data->height = height;
	data->pixels = std::move(pixels);
	return data;
}

static bool SaveState_CompressScreenshot(SaveStateScreenshotData* data, zip_t* zf)
{
	zip_error_t ze = {};
	zip_source_t* const zs = zip_source_buffer_create(nullptr, 0, 0, &ze);
	if (!zs || zip_source_begin_write(zs) != 0)
	{
		if (zs)
			zip_source_free(zs);
		return false;
	}
	ScopedGuard zs_free([zs]() { zip_source_free(zs); });

	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	png_infop info_ptr = nullptr;
	if (!png_ptr)
		return false;
	ScopedGuard cleanup([&png_ptr, &info_ptr]() {
		if (png_ptr)
			png_destroy_write_struct(&png_ptr, info_ptr ? &info_ptr : nullptr);
	});

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr || setjmp(png_jmpbuf(png_ptr)))
		return false;

	png_set_write_fn(png_ptr, zs,
		[](png_structp p, png_bytep data_ptr, png_size_t size) {
			zip_source_write(static_cast<zip_source_t*>(png_get_io_ptr(p)), data_ptr, size);
		},
		[](png_structp) {});
	png_set_compression_level(png_ptr, 5);
	png_set_IHDR(png_ptr, info_ptr, data->width, data->height, 8, PNG_COLOR_TYPE_RGBA,
		PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png_ptr, info_ptr);

	for (u32 y = 0; y < data->height; ++y)
	{
		u32* row = &data->pixels[y * data->width];
		for (u32 x = 0; x < data->width; ++x)
			row[x] |= 0xFF000000u;
		png_write_row(png_ptr, reinterpret_cast<png_bytep>(row));
	}

	png_write_end(png_ptr, nullptr);
	if (zip_source_commit_write(zs) != 0)
		return false;

	const s64 file_index = zip_file_add(zf, EntryFilename_Screenshot, zs, 0);
	if (file_index < 0)
		return false;

	zip_set_file_compression(zf, file_index, ZIP_CM_STORE, 0);
	zs_free.Cancel();
	return true;
}

static bool SaveState_ReadScreenshot(zip_t* zf, u32* out_width, u32* out_height, std::vector<u32>* out_pixels)
{
	auto zff = zip_fopen_managed(zf, EntryFilename_Screenshot, 0);
	if (!zff)
		return false;

	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (!png_ptr)
		return false;

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
	{
		png_destroy_read_struct(&png_ptr, nullptr, nullptr);
		return false;
	}
	ScopedGuard cleanup([&png_ptr, &info_ptr]() {
		png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
	});

	if (setjmp(png_jmpbuf(png_ptr)))
		return false;

	png_set_read_fn(png_ptr, zff.get(), [](png_structp p, png_bytep data_ptr, png_size_t size) {
		zip_fread(static_cast<zip_file_t*>(png_get_io_ptr(p)), data_ptr, size);
	});
	png_read_info(png_ptr, info_ptr);

	png_uint_32 width = 0, height = 0;
	int bitDepth = 0, colorType = -1;
	if (png_get_IHDR(png_ptr, info_ptr, &width, &height, &bitDepth, &colorType, nullptr, nullptr, nullptr) != 1 ||
		width == 0 || height == 0)
	{
		return false;
	}

	std::vector<u8> rowData(png_get_rowbytes(png_ptr, info_ptr));
	*out_width = width;
	*out_height = height;
	out_pixels->resize(width * height);

	for (u32 y = 0; y < height; ++y)
	{
		png_read_row(png_ptr, static_cast<png_bytep>(rowData.data()), nullptr);
		const u8* row_ptr = rowData.data();
		u32* out_ptr = &out_pixels->at(y * width);
		if (colorType == PNG_COLOR_TYPE_RGB)
		{
			for (u32 x = 0; x < width; ++x)
			{
				u32 pixel = static_cast<u32>(*(row_ptr)++);
				pixel |= static_cast<u32>(*(row_ptr)++) << 8;
				pixel |= static_cast<u32>(*(row_ptr)++) << 16;
				pixel |= static_cast<u32>(*(row_ptr)++) << 24;
				*(out_ptr++) = pixel | 0xFF000000u;
			}
		}
		else if (colorType == PNG_COLOR_TYPE_RGBA)
		{
			for (u32 x = 0; x < width; ++x)
			{
				u32 pixel = 0;
				std::memcpy(&pixel, row_ptr, sizeof(u32));
				row_ptr += sizeof(u32);
				*(out_ptr++) = pixel | 0xFF000000u;
			}
		}
	}
	return true;
}

static bool SaveState_AddToZip(zip_t* zf, ArchiveEntryList* srclist, SaveStateScreenshotData* screenshot)
{
	u32 compression = ZIP_CM_DEFAULT;
	u32 compression_level = 0;

	if (EmuConfig.Savestate.CompressionType == SavestateCompressionMethod::Zstandard)
	{
		compression = ZIP_CM_ZSTD;
		switch (EmuConfig.Savestate.CompressionRatio)
		{
			case SavestateCompressionLevel::Low:      compression_level = 1; break;
			case SavestateCompressionLevel::Medium:   compression_level = 3; break;
			case SavestateCompressionLevel::High:     compression_level = 10; break;
			case SavestateCompressionLevel::VeryHigh: compression_level = 22; break;
			default:                                  compression_level = 3; break;
		}
	}
	else if (EmuConfig.Savestate.CompressionType == SavestateCompressionMethod::Deflate)
	{
		compression = ZIP_CM_DEFLATE;
		switch (EmuConfig.Savestate.CompressionRatio)
		{
			case SavestateCompressionLevel::Low:      compression_level = 1; break;
			case SavestateCompressionLevel::Medium:   compression_level = 5; break;
			case SavestateCompressionLevel::High:     compression_level = 7; break;
			case SavestateCompressionLevel::VeryHigh: compression_level = 9; break;
			default:                                  compression_level = 5; break;
		}
	}
	else if (EmuConfig.Savestate.CompressionType == SavestateCompressionMethod::Uncompressed)
	{
		compression = ZIP_CM_STORE;
	}

	{
		struct VersionIndicator {
			u32 save_version;
			char version[STATE_PCSX2_VERSION_SIZE];
		};

		auto* vi = static_cast<VersionIndicator*>(std::malloc(sizeof(VersionIndicator)));
		if (!vi)
			return false;
		vi->save_version = g_SaveVersion;
		StringUtil::Strlcpy(vi->version, BuildVersion::GitTaggedCommit ? BuildVersion::GitTag : "Unknown", std::size(vi->version));

		zip_source_t* const zs = zip_source_buffer(zf, vi, sizeof(*vi), 1);
		if (!zs)
		{
			std::free(vi);
			return false;
		}
		const s64 fi = zip_file_add(zf, EntryFilename_StateVersion, zs, ZIP_FL_ENC_UTF_8);
		if (fi < 0)
		{
			zip_source_free(zs);
			return false;
		}
		zip_set_file_compression(zf, fi, ZIP_CM_STORE, 0);
	}

	const uint listlen = srclist->GetLength();
	for (uint i = 0; i < listlen; ++i)
	{
		const ArchiveEntry& entry = (*srclist)[i];
		if (!entry.GetDataSize())
			continue;
		zip_source_t* const zs = zip_source_buffer(zf, srclist->GetPtr(entry.GetDataIndex()), entry.GetDataSize(), 0);
		if (!zs)
			return false;
		const s64 fi = zip_file_add(zf, entry.GetFilename().c_str(), zs, ZIP_FL_ENC_UTF_8);
		if (fi < 0)
		{
			zip_source_free(zs);
			return false;
		}
		zip_set_file_compression(zf, fi, compression, compression_level);
	}

	return !screenshot || SaveState_CompressScreenshot(screenshot, zf);
}

bool SaveState_ZipToDisk(std::unique_ptr<ArchiveEntryList> srclist, std::unique_ptr<SaveStateScreenshotData> screenshot,
                         const char* filename, Error* error)
{
	std::error_code ec;
	const std::filesystem::path dest_path(filename);
	if (!dest_path.parent_path().empty())
		std::filesystem::create_directories(dest_path.parent_path(), ec);

	const std::filesystem::path tmp_path =
		dest_path.parent_path() / (dest_path.filename().string() + ".tmp");
	const std::string tmp_str = tmp_path.string();

	zip_error_t ze = {};
	zip_source_t* zs = zip_source_file_create(tmp_str.c_str(), 0, 0, &ze);
	zip_t* zf = nullptr;
	if (!zs || !(zf = zip_open_from_source(zs, ZIP_CREATE | ZIP_TRUNCATE, &ze)))
	{
		Error::SetStringFmt(error, TRANSLATE_FS("SaveState", "Failed to open zip file '{}' for save state: {}."),
			filename, zip_error_strerror(&ze));
		if (zs)
			zip_source_free(zs);
		std::filesystem::remove(tmp_path, ec);
		return false;
	}

	if (!SaveState_AddToZip(zf, srclist.get(), screenshot.get()) || zip_close(zf) != 0)
	{
		Error::SetStringFmt(error, TRANSLATE_FS("SaveState", "Failed to save state to zip file '{}'."), filename);
		zip_discard(zf);
		std::filesystem::remove(tmp_path, ec);
		return false;
	}

	return pcsx2_ax::savestate_bridge::commit_file_atomically(filename, error);
}

bool SaveState_ReadScreenshot(const std::string& filename, u32* out_width, u32* out_height, std::vector<u32>* out_pixels)
{
	zip_error_t ze = {};
	auto zf = zip_open_managed(filename.c_str(), ZIP_RDONLY, &ze);
	if (!zf)
		return false;
	return SaveState_ReadScreenshot(zf.get(), out_width, out_height, out_pixels);
}

static bool CheckVersion(const std::string&, zip_t* zf, Error* error)
{
	u32 savever = 0;
	auto zff = zip_fopen_managed(zf, EntryFilename_StateVersion, 0);
	if (!zff || zip_fread(zff.get(), &savever, sizeof(savever)) != sizeof(savever))
	{
		Error::SetString(error, "Savestate file does not contain version indicator.");
		return false;
	}

	char version_string[STATE_PCSX2_VERSION_SIZE];
	if (zip_fread(zff.get(), version_string, STATE_PCSX2_VERSION_SIZE) == STATE_PCSX2_VERSION_SIZE)
		version_string[STATE_PCSX2_VERSION_SIZE - 1] = 0;
	else
		StringUtil::Strlcpy(version_string, "Unknown", std::size(version_string));

	if (savever > g_SaveVersion || (savever >> 16) != (g_SaveVersion >> 16))
	{
		const char* current_emulator_version =
			(BuildVersion::GitTag && BuildVersion::GitTag[0] != '\0') ? BuildVersion::GitTag : "Unknown";
		Error::SetString(error, fmt::format(TRANSLATE_FS("SaveState",
			"This save state was created with PCSX2 version {0} and is incompatible with {1}."),
			version_string, current_emulator_version));
		return false;
	}
	return true;
}

static zip_int64_t CheckFileExistsInState(zip_t* zf, const char* name, bool required)
{
	const zip_int64_t index = zip_name_locate(zf, name, 0);
	if (index < 0 && required)
		Console.WriteLn(Color_Red, " ... not found '%s'!", name);
	return index;
}

static bool LoadInternalStructuresState(zip_t* zf, s64 index, Error* error)
{
	zip_stat_t zst;
	if (zip_stat_index(zf, static_cast<zip_uint64_t>(index), 0, &zst) != 0 ||
		zst.size > static_cast<zip_uint64_t>(std::numeric_limits<int>::max()))
	{
		return false;
	}

	auto zff = zip_fopen_index_managed(zf, static_cast<zip_uint64_t>(index), 0);
	if (!zff)
		return false;

	std::vector<u8> buffer(static_cast<std::size_t>(zst.size));
	if (zip_fread(zff.get(), buffer.data(), buffer.size()) != static_cast<zip_int64_t>(buffer.size()))
		return false;

	memLoadingState state(buffer);
	return state.FreezeBios() && state.FreezeInternals(error);
}

bool SaveState_UnzipFromDisk(const std::string& filename, Error* error)
{
	zip_error_t ze = {};
	auto zf = zip_open_managed(filename.c_str(), ZIP_RDONLY, &ze);
	if (!zf)
	{
		Error::SetString(error, (zip_error_code_zip(&ze) == ZIP_ER_NOENT)
			? std::string("Savestate file does not exist.")
			: fmt::format("Savestate zip error: {}", zip_error_strerror(&ze)));
		return false;
	}

	if (!CheckVersion(filename, zf.get(), error))
		return false;

	const s64 internal_index = CheckFileExistsInState(zf.get(), EntryFilename_InternalStructures, true);
	std::array<zip_int64_t, SavestateEntries.size()> entryIndices{};
	bool allPresent = (internal_index >= 0);

	for (std::size_t i = 0; i < SavestateEntries.size(); ++i)
	{
		entryIndices[i] = CheckFileExistsInState(zf.get(), SavestateEntries[i]->GetFilename(), SavestateEntries[i]->IsRequired());
		if (entryIndices[i] < 0 && SavestateEntries[i]->IsRequired())
		{
			allPresent = false;
			break;
		}
	}
	if (!allPresent)
	{
		Error::SetString(error, "Some required components were not found or are incomplete.");
		return false;
	}

	PreLoadPrep();
	if (!LoadInternalStructuresState(zf.get(), internal_index, error))
	{
		if (!error->IsValid())
			Error::SetString(error, "Save state corruption in internal structures.");
		VMManager::Reset();
		return false;
	}

	for (std::size_t i = 0; i < SavestateEntries.size(); ++i)
	{
		if (entryIndices[i] < 0)
		{
			SavestateEntries[i]->FreezeIn(nullptr);
			continue;
		}
		auto zff = zip_fopen_index_managed(zf.get(), static_cast<zip_uint64_t>(entryIndices[i]), 0);
		if (!zff || !SavestateEntries[i]->FreezeIn(zff.get()))
		{
			Error::SetString(error, fmt::format("Save state corruption in {}.", SavestateEntries[i]->GetFilename()));
			VMManager::Reset();
			return false;
		}
	}

	PostLoadPrep();
	return true;
}

void SaveState_ReportLoadErrorOSD(const std::string& message, std::optional<s32> slot, bool backup)
{
	const std::string full_message = slot.has_value()
		? (backup
			? fmt::format(TRANSLATE_FS("SaveState", "Failed to load state from backup slot {}: {}"), *slot, message)
			: fmt::format(TRANSLATE_FS("SaveState", "Failed to load state from slot {}: {}"), *slot, message))
		: fmt::format(TRANSLATE_FS("SaveState", "Failed to load state: {}"), message);

	Host::AddIconOSDMessage("LoadState", ICON_FA_TRIANGLE_EXCLAMATION, full_message, Host::OSD_WARNING_DURATION);
}

void SaveState_ReportSaveErrorOSD(const std::string& message, std::optional<s32> slot)
{
	const std::string full_message = slot.has_value()
		? fmt::format(TRANSLATE_FS("SaveState", "Failed to save state to slot {}: {}"), *slot, message)
		: fmt::format(TRANSLATE_FS("SaveState", "Failed to save state: {}"), message);

	Host::AddIconOSDMessage("SaveState", ICON_FA_TRIANGLE_EXCLAMATION, full_message, Host::OSD_WARNING_DURATION);
}
