#pragma once
#ifndef AX_FIXED_IP_ADDRESS_H
#define AX_FIXED_IP_ADDRESS_H
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+


#include "common/Pcsx2Defs.h"

namespace PacketReader::IP
{
	struct IP_Address
	{
		union
		{
			u8 bytes[4];
			u32 integer;
		};

		bool operator==(const IP_Address& other) const { return this->integer == other.integer; }
		bool operator!=(const IP_Address& other) const { return this->integer != other.integer; }
	};
} // namespace PacketReader::IP
#endif // AX_FIXED_IP_ADDRESS_H

