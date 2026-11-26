// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team & The Silicon Preservation Society
// SPDX-License-Identifier: GPL-3.0+

#include "CDVDdiscReader.h"
#include "CDVD/CDVD.h"
#include "Host.h"
#include "common/Console.h"
#include "common/Error.h"

#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

void (*newDiscCB)();

static std::mutex s_keepalive_lock;
static std::condition_variable s_keepalive_cv;
static std::thread s_keepalive_thread;

///////////////////////////////////////////////////////////////////////////////
// State Information                                                         //

int curDiskType = CDVD_TYPE_NODISC;
int curTrayStatus = CDVD_TRAY_OPEN;

static u32 csector = 0u;
int cmode = CDVD_MODE_2048;

static int lastReadInNewDiskCB = 0;
static u8 directReadSectorBuffer[2448];

///////////////////////////////////////////////////////////////////////////////
// Utility Functions                                                         //

static u8 dec_to_bcd(u8 dec) noexcept
{
	return static_cast<u8>(((dec / 10u) << 4u) | (dec % 10u));
}

static void lba_to_bcd_msf(u8* minute, u8* second, u8* frame, u32 lsn) noexcept
{
	*frame = dec_to_bcd(static_cast<u8>(lsn % 75u));
	lsn /= 75u;
	*second = dec_to_bcd(static_cast<u8>(lsn % 60u));
	lsn /= 60u;
	*minute = dec_to_bcd(static_cast<u8>(lsn % 100u));
}

// TocStuff
void cdvdParseTOC()
{
	tracks.fill(cdvdTrack{});

	if (!src || !src->GetSectorCount())
	{
		curDiskType = CDVD_TYPE_NODISC;
		strack = 1;
		etrack = 0;
		return;
	}

	if (src->GetMediaType() >= 0)
	{
		tracks[1].type = CDVD_MODE1_TRACK;
		strack = 1;
		etrack = 1;
		return;
	}

	strack = 0xFF;
	etrack = 0;

	for (const auto& entry : src->ReadTOC())
	{
		const u8 track = entry.track;
		if (track < 1 || track >= tracks.size())
		{
			Console.Warning("CDVD: Invalid track index %u, ignoring\n", track);
			continue;
		}
		strack = std::min(strack, track);
		etrack = std::max(etrack, track);
		tracks[track].start_lba = entry.lba;
		if ((entry.control & 0x0C) == 0x04)
		{
			std::array<u8, 2352> buffer{};
			if (src->ReadSectors2352(entry.lba, 1, buffer.data()) && (buffer[15] & 3) == 2)
				tracks[track].type = CDVD_MODE2_TRACK;
			else
				tracks[track].type = CDVD_MODE1_TRACK;
		}
		else
		{
			tracks[track].type = CDVD_AUDIO_TRACK;
		}
#ifdef PCSX2_DEBUG
		DevCon.WriteLn("cdvdParseTOC: Track %u: LBA %u, Type %u\n", track, tracks[track].start_lba, tracks[track].type);
#endif
	}
}

///////////////////////////////////////////////////////////////////////////////
// CDVD processing functions                                                 //

std::atomic<bool> s_keepalive_is_open{false};
bool disc_has_changed = false;
bool weAreInNewDiskCB = false;

std::unique_ptr<IOCtlSrc> src;

extern u32 g_last_sector_block_lsn;

static void keepAliveThread()
{
	u8 throwaway[2352];

	std::printf(" * CDVD: KeepAlive thread started...\n");
	std::unique_lock<std::mutex> guard(s_keepalive_lock);

	while (!s_keepalive_cv.wait_for(guard, std::chrono::seconds(30),
		[]() noexcept { return !s_keepalive_is_open.load(std::memory_order_acquire); }))
	{
		if (!src)
			continue;

		if (src->GetMediaType() >= 0)
			(void)src->ReadSectors2048(g_last_sector_block_lsn, 1, throwaway);
		else
			(void)src->ReadSectors2352(g_last_sector_block_lsn, 1, throwaway);
	}

	std::printf(" * CDVD: KeepAlive thread finished.\n");
}

bool StartKeepAliveThread()
{
	if (!s_keepalive_is_open.load(std::memory_order_acquire))
	{
		s_keepalive_is_open.store(true, std::memory_order_release);
		s_keepalive_thread = std::thread(keepAliveThread);
	}

	return s_keepalive_is_open.load(std::memory_order_acquire);
}

void StopKeepAliveThread()
{
	if (!s_keepalive_thread.joinable())
		return;

	{
		std::lock_guard<std::mutex> guard(s_keepalive_lock);
		s_keepalive_is_open.store(false, std::memory_order_release);
	}
	s_keepalive_cv.notify_one();
	s_keepalive_thread.join();
}

static bool DISCopen(std::string filename, Error* error)
{
	std::string drive = std::move(filename);
	GetValidDrive(drive);
	if (drive.empty())
	{
		Error::SetString(error, fmt::format("Failed to get drive for {}", drive));
		return false;
	}

	src = std::make_unique<IOCtlSrc>(std::move(drive));
	if (!src->Reopen(error))
	{
		src.reset();
		return false;
	}

	cdvdStartThread();
	StartKeepAliveThread();

	cdvdRefreshData();
	return true;
}

static bool DISCprecache(ProgressCallback* /*progress*/, Error* error)
{
	Error::SetStringView(error, TRANSLATE_SV("CDVD", "Precaching is not supported for discs."));
	return false;
}

static void DISCclose()
{
	StopKeepAliveThread();
	cdvdStopThread();
	src.reset();
}

static s32 DISCreadTrack(u32 lsn, int mode)
{
	csector = lsn;
	cmode = mode;

	if (weAreInNewDiskCB)
	{
		const int ret = cdvdDirectReadSector(lsn, mode, directReadSectorBuffer);
		if (ret == 0)
			lastReadInNewDiskCB = 1;
		return ret;
	}

	cdvdRequestSector(lsn, mode);
	return 0;
}

static s32 DISCgetBuffer(u8* dest)
{
	if (!src || csector >= src->GetSectorCount())
		return 0;

	int csize = 2352;
	switch (cmode)
	{
		case CDVD_MODE_2048: csize = 2048; break;
		case CDVD_MODE_2328: csize = 2328; break;
		case CDVD_MODE_2340: csize = 2340; break;
		default: break;
	}

	if (lastReadInNewDiskCB)
	{
		lastReadInNewDiskCB = 0;
		std::memcpy(dest, directReadSectorBuffer, static_cast<std::size_t>(csize));
		return 0;
	}

	if (const u8* sector_ptr = cdvdGetSector(csector, cmode); sector_ptr != nullptr)
		std::memcpy(dest, sector_ptr, static_cast<std::size_t>(csize));

	return 0;
}

static s32 DISCreadSubQ(u32 lsn, cdvdSubQ* subq)
{
	if (!src || lsn >= src->GetSectorCount())
		return -1;

	std::memset(subq, 0, sizeof(cdvdSubQ));

	lba_to_bcd_msf(&subq->discM, &subq->discS, &subq->discF, lsn + 150u);

	u8 i = strack;
	while (i < etrack && lsn >= tracks[i + 1].start_lba)
		++i;

	lsn -= tracks[i].start_lba;

	lba_to_bcd_msf(&subq->trackM, &subq->trackS, &subq->trackF, lsn);

	subq->ctrl = tracks[i].type;

	if (!src->ReadTrackSubQ(subq))
	{
		subq->adr = 1;
		subq->trackNum = i;
		subq->trackIndex = 1;
	}

	return 0;
}

static s32 DISCgetTN(cdvdTN* Buffer)
{
	Buffer->strack = strack;
	Buffer->etrack = etrack;
	return 0;
}

static s32 DISCgetTD(u8 Track, cdvdTD* Buffer)
{
	if (Track == 0)
	{
		if (!src)
			return -1;

		Buffer->lsn = src->GetSectorCount();
		Buffer->type = 0;
		return 0;
	}

	if (Track < strack || Track > etrack)
		return -1;

	Buffer->lsn = tracks[Track].start_lba;
	Buffer->type = tracks[Track].type;
	return 0;
}

static s32 DISCgetTOC(void* toc)
{
	if (!src)
		return -1;

	u8* tocBuff = static_cast<u8*>(toc);
	if (curDiskType == CDVD_TYPE_NODISC)
		return -1;

	if (curDiskType == CDVD_TYPE_DETCTDVDS || curDiskType == CDVD_TYPE_DETCTDVDD)
	{
		std::memset(tocBuff, 0, 2048);

		const s32 mt = src->GetMediaType();
		if (mt < 0)
			return -1;

		if (mt == 0)
		{
			tocBuff[0] = 0x04;
			tocBuff[1] = 0x02;
			tocBuff[2] = 0xF2;
			tocBuff[3] = 0x00;
			tocBuff[4] = 0x86;
			tocBuff[5] = 0x72;

			tocBuff[12] = 0x01;
			tocBuff[13] = 0x02;
			tocBuff[14] = 0x01;
			tocBuff[15] = 0x00;

			tocBuff[16] = 0x00;
			tocBuff[17] = 0x03;
			tocBuff[18] = 0x00;
			tocBuff[19] = 0x00;

			cdvdTD trackInfo{};
			if (DISCgetTD(0, &trackInfo) == -1)
				trackInfo.lsn = 0;
			const s32 maxlsn = static_cast<s32>(trackInfo.lsn) + (0x30000 - 1);
			tocBuff[20] = static_cast<u8>((maxlsn >> 24) & 0xff);
			tocBuff[21] = static_cast<u8>((maxlsn >> 16) & 0xff);
			tocBuff[22] = static_cast<u8>((maxlsn >> 8) & 0xff);
			tocBuff[23] = static_cast<u8>(maxlsn & 0xff);
		}
		else if (mt == 1)
		{
			const s32 layer1start = static_cast<s32>(src->GetLayerBreakAddress()) + 0x30000;

			tocBuff[0] = 0x24;
			tocBuff[1] = 0x02;
			tocBuff[2] = 0xF2;
			tocBuff[3] = 0x00;
			tocBuff[4] = 0x41;
			tocBuff[5] = 0x95;

			tocBuff[12] = 0x01;
			tocBuff[13] = 0x02;
			tocBuff[14] = 0x21;
			tocBuff[15] = 0x10;

			tocBuff[16] = 0x00;
			tocBuff[17] = 0x03;
			tocBuff[18] = 0x00;
			tocBuff[19] = 0x00;

			tocBuff[20] = static_cast<u8>((layer1start >> 24) & 0xff);
			tocBuff[21] = static_cast<u8>((layer1start >> 16) & 0xff);
			tocBuff[22] = static_cast<u8>((layer1start >> 8) & 0xff);
			tocBuff[23] = static_cast<u8>(layer1start & 0xff);
		}
		else
		{
			const s32 layer1start = static_cast<s32>(src->GetLayerBreakAddress()) + 0x30000;

			tocBuff[0] = 0x24;
			tocBuff[1] = 0x02;
			tocBuff[2] = 0xF2;
			tocBuff[3] = 0x00;
			tocBuff[4] = 0x41;
			tocBuff[5] = 0x95;

			tocBuff[12] = 0x01;
			tocBuff[13] = 0x02;
			tocBuff[14] = 0x31;
			tocBuff[15] = 0x10;

			tocBuff[16] = 0x00;
			tocBuff[17] = 0x03;
			tocBuff[18] = 0x00;
			tocBuff[19] = 0x00;

			tocBuff[24] = static_cast<u8>((layer1start >> 24) & 0xff);
			tocBuff[25] = static_cast<u8>((layer1start >> 16) & 0xff);
			tocBuff[26] = static_cast<u8>((layer1start >> 8) & 0xff);
			tocBuff[27] = static_cast<u8>(layer1start & 0xff);
		}
	}
	else if (curDiskType == CDVD_TYPE_DETCTCD)
	{
		u8 min = 0, sec = 0, frm = 0;
		cdvdTN diskInfo{};
		cdvdTD trackInfo{};
		std::memset(tocBuff, 0, 1024);
		if (DISCgetTN(&diskInfo) == -1)
		{
			diskInfo.etrack = 0;
			diskInfo.strack = 1;
		}
		if (DISCgetTD(0, &trackInfo) == -1)
			trackInfo.lsn = 0;

		tocBuff[0] = 0x41;
		tocBuff[1] = 0x00;

		tocBuff[2] = 0xA0;
		tocBuff[7] = dec_to_bcd(diskInfo.strack);

		tocBuff[12] = 0xA1;
		tocBuff[17] = dec_to_bcd(diskInfo.etrack);

		lba_to_msf(static_cast<s32>(trackInfo.lsn), &min, &sec, &frm);
		tocBuff[22] = 0xA2;
		tocBuff[27] = dec_to_bcd(min);
		tocBuff[28] = dec_to_bcd(sec);
		tocBuff[29] = dec_to_bcd(frm);

		for (u8 i = diskInfo.strack; i <= diskInfo.etrack; ++i)
		{
			const s32 err = DISCgetTD(i, &trackInfo);
			lba_to_msf(static_cast<s32>(trackInfo.lsn), &min, &sec, &frm);

			const u8 tocIndex = static_cast<u8>(i - diskInfo.strack);
			tocBuff[tocIndex * 10 + 30] = trackInfo.type;
			tocBuff[tocIndex * 10 + 32] = (err == -1) ? 0 : dec_to_bcd(i);
			tocBuff[tocIndex * 10 + 37] = dec_to_bcd(min);
			tocBuff[tocIndex * 10 + 38] = dec_to_bcd(sec);
			tocBuff[tocIndex * 10 + 39] = dec_to_bcd(frm);
		}
	}
	else
	{
		return -1;
	}

	return 0;
}

static s32 DISCgetDiskType()
{
	return curDiskType;
}

static s32 DISCgetTrayStatus()
{
	return curTrayStatus;
}

static s32 DISCctrlTrayOpen()
{
	curTrayStatus = CDVD_TRAY_OPEN;
	return 0;
}

static s32 DISCctrlTrayClose()
{
	curTrayStatus = CDVD_TRAY_CLOSE;
	return 0;
}

static void DISCnewDiskCB(void (*callback)())
{
	newDiscCB = callback;
}

static s32 DISCreadSector(u8* buffer, u32 lsn, int mode)
{
	return cdvdDirectReadSector(lsn, mode, buffer);
}

static s32 DISCgetDualInfo(s32* dualType, u32* _layer1start)
{
	if (!src)
		return -1;
	switch (src->GetMediaType())
	{
		case 1:
			*dualType = 1;
			*_layer1start = src->GetLayerBreakAddress() + 1;
			return 0;
		case 2:
			*dualType = 2;
			*_layer1start = src->GetLayerBreakAddress() + 1;
			return 0;
		case 0:
			*dualType = 0;
			*_layer1start = 0;
			return 0;
		default:
			return -1;
	}
}

const CDVD_API CDVDapi_Disc = {
	DISCclose,
	DISCopen,
	DISCprecache,
	DISCreadTrack,
	DISCgetBuffer,
	DISCreadSubQ,
	DISCgetTN,
	DISCgetTD,
	DISCgetTOC,
	DISCgetDiskType,
	DISCgetTrayStatus,
	DISCctrlTrayOpen,
	DISCctrlTrayClose,
	DISCnewDiskCB,
	DISCreadSector,
	DISCgetDualInfo,
};
