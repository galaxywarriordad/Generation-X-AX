// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team & The Silicon Preservation Society
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "IopMem.h"
#include "R3000A.h"

#include "fmt/format.h"

#include <string>

static std::string psxout_buf;
static std::string psxout_last;
static unsigned psxout_repeat = 0u;

static void flush_stdout(bool closing = false)
{
	while (!psxout_buf.empty())
	{
		size_t linelen = psxout_buf.find_first_of("\n\0", 0, 2);
		if (linelen == std::string::npos)
		{
			if (!closing)
				return;
		}
		else
		{
			psxout_buf[linelen++] = '\n';
		}

		if (linelen != 1)
		{
			if (!psxout_buf.compare(0, linelen, psxout_last))
			{
				++psxout_repeat;
			}
			else
			{
				if (psxout_repeat > 0u)
				{
					iopConLog(fmt::format("[{} more]\n", psxout_repeat));
					psxout_repeat = 0u;
				}
				psxout_last = psxout_buf.substr(0, linelen);
				iopConLog(ShiftJIS_ConvertString(psxout_last.data()));
			}
		}
		psxout_buf.erase(0, linelen);
	}

	if (closing && psxout_repeat > 0u)
	{
		iopConLog(fmt::format("[{} more]\n", psxout_repeat));
		psxout_repeat = 0u;
	}
}

void psxBiosReset()
{
	flush_stdout(true);
}

bool psxBiosCall()
{
	switch (((psxRegs.pc << 4) & 0xf00) | (psxRegs.GPR.n.t1 & 0xff))
	{
		case 0xa03:
		case 0xb35:
		{
			const int fd = psxRegs.GPR.n.a0;
			if (fd != 1)
				return false;

			u32 data = psxRegs.GPR.n.a1;
			u32 size = psxRegs.GPR.n.a2;
			while (size-- > 0u)
				psxout_buf.push_back(static_cast<char>(iopMemRead8(data++)));
			flush_stdout(false);
			return false;
		}
		case 0xa09:
		case 0xb3b:
			if (psxRegs.GPR.n.a1 != 1)
				return false;
			[[fallthrough]];
		case 0xa3c:
		case 0xb3d:
			psxout_buf.push_back(static_cast<char>(psxRegs.GPR.n.a0));
			flush_stdout(false);
			return false;
		case 0xa3e:
		case 0xb3f:
		{
			u32 str = psxRegs.GPR.n.a0;
			while (const char c = static_cast<char>(iopMemRead8(str++)))
				psxout_buf.push_back(c);
			psxout_buf.push_back('\n');
			flush_stdout(false);
			return false;
		}
		default:
			return false;
	}
}
