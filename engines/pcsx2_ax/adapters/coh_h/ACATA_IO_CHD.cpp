// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ACATA_IO_CHD.h"
#include "common/Console.h"
#include "common/Path.h"
#include "common/StringUtil.h"

#include <cstring>

ChdImage::ChdImage() = default;

ChdImage::~ChdImage()
{
	Close();
}

bool ChdImage::Open(const std::string& path)
{
	Close();

	const chd_error err = chd_open(path.c_str(), CHD_OPEN_READ, nullptr, &m_chd);
	if (err != CHDERR_NONE)
	{
		Console.ErrorFmt("{} failed to open CHD: {}", __FUNCTION__, static_cast<int>(err));
		return false;
	}

	const chd_header* hdr = chd_get_header(m_chd);
	if (hdr == nullptr || hdr->hunkbytes == 0u || hdr->unitbytes == 0u || hdr->logicalbytes < hdr->unitbytes)
	{
		Console.ErrorFmt("{} invalid CHD header dimensions in '{}'", __FUNCTION__, path);
		Close();
		return false;
	}

	m_hunkSize = hdr->hunkbytes;
	m_unitBytes = hdr->unitbytes;
	m_totalUnits = hdr->logicalbytes / hdr->unitbytes;

	switch (hdr->unitbytes)
	{
		case 2048u:
			m_type = ACMEDIATYPE::ACDVD;
			break;
		case 512u:
			m_type = ACMEDIATYPE::ACHDD;
			break;
		default:
			m_type = ACMEDIATYPE::ACUNK;
			break;
	}

	// CD units are raw frames; locate the 2048-byte user payload (DVD/HDD keep payload at offset 0).
	if (m_unitBytes == 2352u || m_unitBytes == 2448u)
		m_frameDataOffset = DetectCdDataOffset();

	m_hunkBuffer.resize(m_hunkSize);

	DevCon.WriteLnFmt("{}: opened ok (unit {}, hunk {}, data offset {})",
	                 __FUNCTION__, m_unitBytes, m_hunkSize, m_frameDataOffset);
	return true;
}

void ChdImage::Close()
{
	if (m_chd)
	{
		chd_close(m_chd);
		m_chd = nullptr;
	}

	m_hunkBuffer.clear();
	m_cachedHunk = UINT32_MAX;
	m_hunkSize = 0u;
	m_unitBytes = 0u;
	m_frameDataOffset = 0u;
	m_totalUnits = 0u;
	m_type = ACMEDIATYPE::ACUNK;
}

bool ChdImage::IsOpen() const
{
	return m_chd != nullptr;
}

ACMEDIATYPE ChdImage::GetType() const
{
	return m_type;
}

u32 ChdImage::GetSectorSize() const
{
	if (m_unitBytes == 2448u || m_unitBytes == 2352u)
		return 2048u;
	return m_unitBytes;
}

u64 ChdImage::GetSectorCount() const
{
	return m_totalUnits;
}

u32 ChdImage::DetectCdDataOffset()
{
	char meta[256] = {};
	u32 len = 0u;
	if (chd_get_metadata(m_chd, CDROM_TRACK_METADATA2_TAG, 0,
	                     meta, sizeof(meta), &len, nullptr, nullptr) != CHDERR_NONE &&
	    chd_get_metadata(m_chd, CDROM_TRACK_METADATA_TAG, 0,
	                     meta, sizeof(meta), &len, nullptr, nullptr) != CHDERR_NONE)
	{
		return 0u;
	}

	if (std::strstr(meta, " TYPE:MODE2_RAW") != nullptr)
		return 24u;
	if (std::strstr(meta, " TYPE:MODE1_RAW") != nullptr)
		return 16u;
	return 0u;
}

bool ChdImage::ReadHunk(u32 hunk)
{
	if (hunk == m_cachedHunk && !m_hunkBuffer.empty())
		return true;

	const chd_error err = chd_read(m_chd, hunk, m_hunkBuffer.data());
	if (err != CHDERR_NONE)
		return false;

	m_cachedHunk = hunk;
	return true;
}

bool ChdImage::ReadSector(u64 lba, void* buffer)
{
	if (!m_chd || buffer == nullptr || m_unitBytes == 0u || m_hunkSize == 0u || lba >= m_totalUnits)
		return false;

	const u64 byteOffset = lba * m_unitBytes;
	const u32 hunk = static_cast<u32>(byteOffset / m_hunkSize);
	const u32 offset = static_cast<u32>(byteOffset % m_hunkSize);

	if ((static_cast<u64>(offset) + m_unitBytes) > m_hunkSize)
		return false;

	if (!ReadHunk(hunk))
		return false;

	if (m_unitBytes == 2448u || m_unitBytes == 2352u)
	{
		if ((static_cast<u64>(offset) + m_frameDataOffset + 2048u) > m_hunkSize)
			return false;
		std::memcpy(buffer, m_hunkBuffer.data() + offset + m_frameDataOffset, 2048u);
	}
	else
	{
		std::memcpy(buffer, m_hunkBuffer.data() + offset, m_unitBytes);
	}

	return true;
}

bool ChdImage::ReadSectors(u64 lba, u32 count, void* buffer)
{
	if (!m_chd || buffer == nullptr || count == 0u || m_unitBytes == 0u || m_hunkSize == 0u)
		return false;
	if (lba > m_totalUnits || static_cast<u64>(count) > (m_totalUnits - lba))
		return false;

	u8* dst = static_cast<u8*>(buffer);
	const bool is_raw_cd = (m_unitBytes == 2448u || m_unitBytes == 2352u);
	const u32 outBytes = is_raw_cd ? 2048u : m_unitBytes;

	for (u32 i = 0u; i < count; ++i)
	{
		const u64 cur_lba = lba + i;
		const u64 byteOffset = cur_lba * m_unitBytes;
		const u32 hunk = static_cast<u32>(byteOffset / m_hunkSize);
		const u32 offset = static_cast<u32>(byteOffset % m_hunkSize);

		if (!ReadHunk(hunk))
			return false;

		if (is_raw_cd)
		{
			if ((static_cast<u64>(offset) + m_frameDataOffset + outBytes) > m_hunkSize)
				return false;
			std::memcpy(dst + (static_cast<std::size_t>(i) * outBytes),
			            m_hunkBuffer.data() + offset + m_frameDataOffset,
			            outBytes);
		}
		else
		{
			if ((static_cast<u64>(offset) + outBytes) > m_hunkSize)
				return false;
			std::memcpy(dst + (static_cast<std::size_t>(i) * outBytes),
			            m_hunkBuffer.data() + offset,
			            outBytes);
		}
	}

	return true;
}

bool ChdImage::IsChdFileName(const std::string& path)
{
	return StringUtil::compareNoCase(Path::GetExtension(path), "chd");
}
