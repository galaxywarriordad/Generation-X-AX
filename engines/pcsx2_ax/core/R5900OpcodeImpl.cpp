// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team & The Silicon Preservation Society
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"

#include <float.h>

#include "R5900.h"
#include "R5900OpcodeTables.h"
#include "GS.h"
#include "IHardwareModeAdapter.hpp"
#include "DebugTools/DebugInterface.h"
#include "DebugTools/Breakpoints.h"
#include "Host.h"
#include "VMManager.h"

#include "fmt/format.h"

namespace pcsx2_ax::r5900 {

inline const pcsx2_ax::core::IHardwareModeAdapter*& active_mode_adapter() noexcept
{
	static const pcsx2_ax::core::IHardwareModeAdapter* adapter = nullptr;
	return adapter;
}

inline u32& ee_thread_list_addr() noexcept
{
	static u32 addr = 0;
	return addr;
}

inline void set_mode_adapter(const pcsx2_ax::core::IHardwareModeAdapter* adapter) noexcept
{
	active_mode_adapter() = adapter;
	ee_thread_list_addr() = 0;
}

[[nodiscard]] inline pcsx2_ax::core::BiosDescriptor current_bios_descriptor() noexcept
{
	if (const auto* adapter = active_mode_adapter(); adapter != nullptr)
		return adapter->bios_descriptor();
	return pcsx2_ax::core::BiosDescriptor{};
}

} // namespace pcsx2_ax::r5900

GS_VideoMode gsVideoMode = GS_VideoMode::Uninitialized;
bool gsIsInterlaced = false;

static __fi bool _add64_Overflow(s64 x, s64 y, s64& ret)
{
	const s64 result = x + y;

	if (((~(x ^ y)) & (x ^ result)) < 0)
	{
		cpuException(0x30, cpuRegs.branch);
		return true;
	}

	ret = result;
	return false;
}

static __fi bool _add32_Overflow(s32 x, s32 y, s64& ret)
{
	GPR_reg64 result;
	result.SD[0] = static_cast<s64>(x) + y;

	if ((result.UL[0] >> 31) != (result.UL[1] & 1))
	{
		cpuException(0x30, cpuRegs.branch);
		return true;
	}

	ret = result.SD[0];
	return false;
}

const R5900::OPCODE& R5900::GetCurrentInstruction()
{
	const OPCODE* opcode = &R5900::OpcodeTables::tbl_Standard[_Opcode_];

	while (opcode->getsubclass != nullptr)
		opcode = &opcode->getsubclass(cpuRegs.code);

	return *opcode;
}

const R5900::OPCODE& R5900::GetInstruction(u32 op)
{
	const OPCODE* opcode = &R5900::OpcodeTables::tbl_Standard[op >> 26];

	while (opcode->getsubclass != nullptr)
		opcode = &opcode->getsubclass(op);

	return *opcode;
}

const char* const R5900::bios[256] = {
//0x00
	"RFU000_FullReset", "ResetEE",				"SetGsCrt",				"RFU003",
	"Exit",				"RFU005",				"LoadExecPS2",			"ExecPS2",
	"RFU008",			"RFU009",				"AddSbusIntcHandler",	"RemoveSbusIntcHandler",
	"Interrupt2Iop",	"SetVTLBRefillHandler", "SetVCommonHandler",	"SetVInterruptHandler",
//0x10
	"AddIntcHandler",	"RemoveIntcHandler",	"AddDmacHandler",		"RemoveDmacHandler",
	"_EnableIntc",		"_DisableIntc",			"_EnableDmac",			"_DisableDmac",
	"_SetAlarm",		"_ReleaseAlarm",		"_iEnableIntc",			"_iDisableIntc",
	"_iEnableDmac",		"_iDisableDmac",		"_iSetAlarm",			"_iReleaseAlarm",
//0x20
	"CreateThread",			"DeleteThread",		"StartThread",			"ExitThread",
	"ExitDeleteThread",		"TerminateThread",	"iTerminateThread",		"DisableDispatchThread",
	"EnableDispatchThread",		"ChangeThreadPriority", "iChangeThreadPriority",	"RotateThreadReadyQueue",
	"iRotateThreadReadyQueue",	"ReleaseWaitThread",	"iReleaseWaitThread",		"GetThreadId",
//0x30
	"ReferThreadStatus","iReferThreadStatus",	"SleepThread",		"WakeupThread",
	"_iWakeupThread",   "CancelWakeupThread",	"iCancelWakeupThread",	"SuspendThread",
	"iSuspendThread",   "ResumeThread",		"iResumeThread",	"JoinThread",
	"RFU060",	    "RFU061",			"EndOfHeap",		 "RFU063",
//0x40
	"CreateSema",	    "DeleteSema",	"SignalSema",		"iSignalSema",
	"WaitSema",	    "PollSema",		"iPollSema",		"ReferSemaStatus",
	"iReferSemaStatus", "RFU073",		"SetOsdConfigParam", 	"GetOsdConfigParam",
	"GetGsHParam",	    "GetGsVParam",	"SetGsHParam",		"SetGsVParam",
//0x50
	"RFU080_CreateEventFlag",	"RFU081_DeleteEventFlag",
	"RFU082_SetEventFlag",		"RFU083_iSetEventFlag",
	"RFU084_ClearEventFlag",	"RFU085_iClearEventFlag",
	"RFU086_WaitEventFlag",		"RFU087_PollEventFlag",
	"RFU088_iPollEventFlag",	"RFU089_ReferEventFlagStatus",
	"RFU090_iReferEventFlagStatus", "RFU091_GetEntryAddress",
	"EnableIntcHandler_iEnableIntcHandler",
	"DisableIntcHandler_iDisableIntcHandler",
	"EnableDmacHandler_iEnableDmacHandler",
	"DisableDmacHandler_iDisableDmacHandler",
//0x60
	"KSeg0",				"EnableCache",	"DisableCache",			"GetCop0",
	"FlushCache",			"RFU101",		"CpuConfig",			"iGetCop0",
	"iFlushCache",			"RFU105",		"iCpuConfig", 			"sceSifStopDma",
	"SetCPUTimerHandler",	"SetCPUTimer",	"SetOsdConfigParam2",	"GetOsdConfigParam2",
//0x70
	"GsGetIMR_iGsGetIMR",				"GsGetIMR_iGsPutIMR",	"SetPgifHandler", 				"SetVSyncFlag",
	"RFU116",							"print", 				"sceSifDmaStat_isceSifDmaStat", "sceSifSetDma_isceSifSetDma",
	"sceSifSetDChain_isceSifSetDChain", "sceSifSetReg",			"sceSifGetReg",					"ExecOSD",
	"Deci2Call",						"PSMode",				"MachineType",					"GetMemorySize",
};

static u32 deci2addr = 0;
static u32 deci2handler = 0;
static char deci2buffer[256];

void Deci2Reset()
{
	deci2handler = 0;
	deci2addr = 0;
	std::memset(deci2buffer, 0, sizeof(deci2buffer));
}

bool SaveStateBase::deci2Freeze()
{
	if (!FreezeTag("deci2"))
		return false;

	Freeze(deci2addr);
	Freeze(deci2handler);
	Freeze(deci2buffer);

	return IsOkay();
}

static int __Deci2Call(int call, u32* addr)
{
	if (call > 0x10)
		return -1;

	switch (call)
	{
		case 1:
			if (addr != nullptr)
			{
				deci2addr = addr[1];
				BIOS_LOG("deci2open: %x,%x,%x,%x", addr[3], addr[2], addr[1], addr[0]);
				deci2handler = addr[2];
			}
			else
			{
				deci2handler = 0;
				DevCon.Warning("Deci2Call.Open > NULL address ignored.");
			}
			return 1;

		case 2:
			deci2addr = 0;
			deci2handler = 0;
			return 1;

		case 3:
		{
			char reqaddr[128];
			if (addr != nullptr)
				std::snprintf(reqaddr, std::size(reqaddr), "%x %x %x %x", addr[3], addr[2], addr[1], addr[0]);

			if (!deci2addr)
				return 1;

			const u32* d2ptr = static_cast<const u32*>(PSM(deci2addr));

			BIOS_LOG("deci2reqsend: %s: deci2addr: %x,%x,%x,buf=%x %x,%x,len=%x,%x",
				(addr == nullptr) ? "NULL" : reqaddr,
				d2ptr[7], d2ptr[6], d2ptr[5], d2ptr[4],
				d2ptr[3], d2ptr[2], d2ptr[1], d2ptr[0]);

			if (d2ptr[1] > 0xc)
			{
				u8* pdeciaddr = static_cast<u8*>(dmaGetAddr(d2ptr[4] + 0xc, false));
				if (pdeciaddr == nullptr)
					pdeciaddr = static_cast<u8*>(PSM(d2ptr[4] + 0xc));
				else
					pdeciaddr += (d2ptr[4] + 0xc) % 16;

				const int copylen = std::min<int>(255, static_cast<int>(d2ptr[1] - 0xc));
				std::memcpy(deci2buffer, pdeciaddr, static_cast<std::size_t>(copylen));
				deci2buffer[copylen] = '\0';

				eeConLog(ShiftJIS_ConvertString(deci2buffer));
			}
			static_cast<u32*>(PSM(deci2addr))[3] = 0;
			return 1;
		}

		case 4:
			if (addr != nullptr)
				BIOS_LOG("deci2poll: %x,%x,%x,%x\n", addr[3], addr[2], addr[1], addr[0]);
			return 1;

		case 5:
		case 6:
			return 1;

		case 0x10:
			if (addr != nullptr)
				eeDeci2Log(ShiftJIS_ConvertString(static_cast<const char*>(PSM(*addr))));
			return 1;

		default:
			return 0;
	}
}

namespace R5900 {
namespace Interpreter {
namespace OpcodeImpl {

void COP2()
{
	Int_COP2PrintTable[_Rs_]();
}

void Unknown()
{
	CPU_LOG("%8.8lx: Unknown opcode called", cpuRegs.pc);
}

void MMI_Unknown()  { Console.Warning("Unknown MMI opcode called"); }
void COP0_Unknown() { Console.Warning("Unknown COP0 opcode called"); }
void COP1_Unknown() { Console.Warning("Unknown FPU/COP1 opcode called"); }

void ADDI()
{
	s64 result;
	if (_add32_Overflow(cpuRegs.GPR.r[_Rs_].SD[0], _Imm_, result) || !_Rt_)
		return;
	cpuRegs.GPR.r[_Rt_].SD[0] = result;
}

void ADDIU()
{
	if (!_Rt_)
		return;
	cpuRegs.GPR.r[_Rt_].UD[0] = static_cast<u64>(static_cast<s64>(static_cast<s32>(cpuRegs.GPR.r[_Rs_].UL[0] + static_cast<u32>(static_cast<s32>(_Imm_)))));
}

void DADDI()
{
	s64 result;
	if (_add64_Overflow(cpuRegs.GPR.r[_Rs_].SD[0], _Imm_, result) || !_Rt_)
		return;
	cpuRegs.GPR.r[_Rt_].SD[0] = result;
}

void DADDIU()
{
	if (!_Rt_)
		return;
	cpuRegs.GPR.r[_Rt_].UD[0] = cpuRegs.GPR.r[_Rs_].UD[0] + static_cast<u64>(static_cast<s64>(_Imm_));
}

void ANDI()  { if (!_Rt_) return; cpuRegs.GPR.r[_Rt_].UD[0] = cpuRegs.GPR.r[_Rs_].UD[0] & static_cast<u64>(_ImmU_); }
void ORI()   { if (!_Rt_) return; cpuRegs.GPR.r[_Rt_].UD[0] = cpuRegs.GPR.r[_Rs_].UD[0] | static_cast<u64>(_ImmU_); }
void XORI()  { if (!_Rt_) return; cpuRegs.GPR.r[_Rt_].UD[0] = cpuRegs.GPR.r[_Rs_].UD[0] ^ static_cast<u64>(_ImmU_); }
void SLTI()  { if (!_Rt_) return; cpuRegs.GPR.r[_Rt_].UD[0] = (cpuRegs.GPR.r[_Rs_].SD[0] < static_cast<s64>(_Imm_)) ? 1u : 0u; }
void SLTIU() { if (!_Rt_) return; cpuRegs.GPR.r[_Rt_].UD[0] = (cpuRegs.GPR.r[_Rs_].UD[0] < static_cast<u64>(_Imm_)) ? 1u : 0u; }

void ADD()
{
	s64 result;
	if (_add32_Overflow(cpuRegs.GPR.r[_Rs_].SD[0], cpuRegs.GPR.r[_Rt_].SD[0], result) || !_Rd_)
		return;
	cpuRegs.GPR.r[_Rd_].SD[0] = result;
}

void DADD()
{
	s64 result;
	if (_add64_Overflow(cpuRegs.GPR.r[_Rs_].SD[0], cpuRegs.GPR.r[_Rt_].SD[0], result) || !_Rd_)
		return;
	cpuRegs.GPR.r[_Rd_].SD[0] = result;
}

void SUB()
{
	s64 result;
	if (_add32_Overflow(cpuRegs.GPR.r[_Rs_].SD[0], -cpuRegs.GPR.r[_Rt_].SD[0], result) || !_Rd_)
		return;
	cpuRegs.GPR.r[_Rd_].SD[0] = result;
}

void DSUB()
{
	s64 result;
	if (_add64_Overflow(cpuRegs.GPR.r[_Rs_].SD[0], -cpuRegs.GPR.r[_Rt_].SD[0], result) || !_Rd_)
		return;
	cpuRegs.GPR.r[_Rd_].SD[0] = result;
}

void ADDU()  { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = static_cast<u64>(static_cast<s64>(static_cast<s32>(cpuRegs.GPR.r[_Rs_].UL[0] + cpuRegs.GPR.r[_Rt_].UL[0]))); }
void DADDU() { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = cpuRegs.GPR.r[_Rs_].UD[0] + cpuRegs.GPR.r[_Rt_].UD[0]; }
void SUBU()  { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = static_cast<u64>(static_cast<s64>(static_cast<s32>(cpuRegs.GPR.r[_Rs_].UL[0] - cpuRegs.GPR.r[_Rt_].UL[0]))); }
void DSUBU() { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = cpuRegs.GPR.r[_Rs_].UD[0] - cpuRegs.GPR.r[_Rt_].UD[0]; }
void AND()   { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = cpuRegs.GPR.r[_Rs_].UD[0] & cpuRegs.GPR.r[_Rt_].UD[0]; }
void OR()    { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = cpuRegs.GPR.r[_Rs_].UD[0] | cpuRegs.GPR.r[_Rt_].UD[0]; }
void XOR()   { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = cpuRegs.GPR.r[_Rs_].UD[0] ^ cpuRegs.GPR.r[_Rt_].UD[0]; }
void NOR()   { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = ~(cpuRegs.GPR.r[_Rs_].UD[0] | cpuRegs.GPR.r[_Rt_].UD[0]); }
void SLT()   { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = (cpuRegs.GPR.r[_Rs_].SD[0] < cpuRegs.GPR.r[_Rt_].SD[0]) ? 1u : 0u; }
void SLTU()  { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = (cpuRegs.GPR.r[_Rs_].UD[0] < cpuRegs.GPR.r[_Rt_].UD[0]) ? 1u : 0u; }

void DIV()
{
	if (cpuRegs.GPR.r[_Rs_].UL[0] == 0x80000000u && cpuRegs.GPR.r[_Rt_].UL[0] == 0xffffffffu)
	{
		cpuRegs.LO.SD[0] = static_cast<s32>(0x80000000u);
		cpuRegs.HI.SD[0] = 0;
	}
	else if (cpuRegs.GPR.r[_Rt_].SL[0] != 0)
	{
		cpuRegs.LO.SD[0] = cpuRegs.GPR.r[_Rs_].SL[0] / cpuRegs.GPR.r[_Rt_].SL[0];
		cpuRegs.HI.SD[0] = cpuRegs.GPR.r[_Rs_].SL[0] % cpuRegs.GPR.r[_Rt_].SL[0];
	}
	else
	{
		cpuRegs.LO.SD[0] = (cpuRegs.GPR.r[_Rs_].SL[0] < 0) ? 1 : -1;
		cpuRegs.HI.SD[0] = cpuRegs.GPR.r[_Rs_].SL[0];
	}
}

void DIVU()
{
	if (cpuRegs.GPR.r[_Rt_].UL[0] != 0)
	{
		cpuRegs.LO.SD[0] = static_cast<s32>(cpuRegs.GPR.r[_Rs_].UL[0] / cpuRegs.GPR.r[_Rt_].UL[0]);
		cpuRegs.HI.SD[0] = static_cast<s32>(cpuRegs.GPR.r[_Rs_].UL[0] % cpuRegs.GPR.r[_Rt_].UL[0]);
	}
	else
	{
		cpuRegs.LO.SD[0] = -1;
		cpuRegs.HI.SD[0] = cpuRegs.GPR.r[_Rs_].SL[0];
	}
}

void MULT()
{
	const s64 res = static_cast<s64>(cpuRegs.GPR.r[_Rs_].SL[0]) * cpuRegs.GPR.r[_Rt_].SL[0];
	cpuRegs.LO.SD[0] = static_cast<s32>(res & 0xffffffffLL);
	cpuRegs.HI.SD[0] = static_cast<s32>(res >> 32);
	if (_Rd_)
		cpuRegs.GPR.r[_Rd_].UD[0] = cpuRegs.LO.UD[0];
}

void MULTU()
{
	const u64 res = static_cast<u64>(cpuRegs.GPR.r[_Rs_].UL[0]) * cpuRegs.GPR.r[_Rt_].UL[0];
	cpuRegs.LO.SD[0] = static_cast<s32>(res & 0xffffffffULL);
	cpuRegs.HI.SD[0] = static_cast<s32>(res >> 32);
	if (_Rd_)
		cpuRegs.GPR.r[_Rd_].UD[0] = cpuRegs.LO.UD[0];
}

void LUI()
{
	if (!_Rt_)
		return;
	cpuRegs.GPR.r[_Rt_].UD[0] = static_cast<s32>(cpuRegs.code << 16);
}

void MFHI() { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = cpuRegs.HI.UD[0]; }
void MFLO() { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = cpuRegs.LO.UD[0]; }
void MTHI() { cpuRegs.HI.UD[0] = cpuRegs.GPR.r[_Rs_].UD[0]; }
void MTLO() { cpuRegs.LO.UD[0] = cpuRegs.GPR.r[_Rs_].UD[0]; }

void SRA()    { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].SD[0] = static_cast<s32>(cpuRegs.GPR.r[_Rt_].SL[0] >> _Sa_); }
void SRL()    { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].SD[0] = static_cast<s32>(cpuRegs.GPR.r[_Rt_].UL[0] >> _Sa_); }
void SLL()    { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].SD[0] = static_cast<s32>(cpuRegs.GPR.r[_Rt_].UL[0] << _Sa_); }
void DSLL()   { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = static_cast<u64>(cpuRegs.GPR.r[_Rt_].UD[0] << _Sa_); }
void DSLL32() { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = static_cast<u64>(cpuRegs.GPR.r[_Rt_].UD[0] << (_Sa_ + 32)); }
void DSRA()   { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].SD[0] = cpuRegs.GPR.r[_Rt_].SD[0] >> _Sa_; }
void DSRA32() { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].SD[0] = cpuRegs.GPR.r[_Rt_].SD[0] >> (_Sa_ + 32); }
void DSRL()   { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = cpuRegs.GPR.r[_Rt_].UD[0] >> _Sa_; }
void DSRL32() { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = cpuRegs.GPR.r[_Rt_].UD[0] >> (_Sa_ + 32); }

void SLLV()  { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].SD[0] = static_cast<s32>(cpuRegs.GPR.r[_Rt_].UL[0] << (cpuRegs.GPR.r[_Rs_].UL[0] & 0x1fu)); }
void SRAV()  { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].SD[0] = static_cast<s32>(cpuRegs.GPR.r[_Rt_].SL[0] >> (cpuRegs.GPR.r[_Rs_].UL[0] & 0x1fu)); }
void SRLV()  { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].SD[0] = static_cast<s32>(cpuRegs.GPR.r[_Rt_].UL[0] >> (cpuRegs.GPR.r[_Rs_].UL[0] & 0x1fu)); }
