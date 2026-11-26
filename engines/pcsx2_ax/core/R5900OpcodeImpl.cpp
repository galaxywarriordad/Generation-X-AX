// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team & The Silicon Preservation Society
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"
#include "VMManager_Internal.h"

#include <float.h>

#include "R5900.h"
#include "R5900OpcodeTables.h"
#include "GS.h"
#include "IHardwareModeAdapter.hpp"
#include "DebugTools/DebugInterface.h"
#include "DebugTools/Breakpoints.h"
#include "Host.h"

#include "fmt/format.h"

namespace pcsx2_ax::r5900 {

namespace {

struct R5900BridgeState {
	const pcsx2_ax::core::IHardwareModeAdapter* adapter{nullptr};
	uint64_t bound_epoch{0u};
	u32 ee_thread_list_addr{0u};
};

inline R5900BridgeState& bridge_state() noexcept
{
	static R5900BridgeState state{};
	return state;
}

} // namespace

inline void set_mode_adapter(const pcsx2_ax::core::IHardwareModeAdapter* adapter,
                             const pcsx2_ax::core::VmContext& ctx = pcsx2_ax::vm_bridge::active_context()) noexcept
{
	auto& st = bridge_state();
	st.adapter = adapter;
	st.bound_epoch = pcsx2_ax::vm_bridge::current_epoch(ctx);
	st.ee_thread_list_addr = 0u;
}

[[nodiscard]] inline const pcsx2_ax::core::IHardwareModeAdapter* active_mode_adapter(
	const pcsx2_ax::core::VmContext& ctx = pcsx2_ax::vm_bridge::active_context()) noexcept
{
	auto& st = bridge_state();
	const uint64_t now_epoch = pcsx2_ax::vm_bridge::current_epoch(ctx);
	if (st.bound_epoch != now_epoch)
	{
		st.bound_epoch = now_epoch;
		st.ee_thread_list_addr = 0u;
		st.adapter = pcsx2_ax::vm_bridge::get_mode_adapter(ctx);
	}
	return (st.adapter != nullptr) ? st.adapter : pcsx2_ax::vm_bridge::get_mode_adapter(ctx);
}

inline u32& ee_thread_list_addr(const pcsx2_ax::core::VmContext& ctx = pcsx2_ax::vm_bridge::active_context()) noexcept
{
	auto& st = bridge_state();
	const uint64_t now_epoch = pcsx2_ax::vm_bridge::current_epoch(ctx);
	if (st.bound_epoch != now_epoch)
	{
		st.bound_epoch = now_epoch;
		st.ee_thread_list_addr = 0u;
	}
	return st.ee_thread_list_addr;
}

[[nodiscard]] inline pcsx2_ax::core::BiosDescriptor current_bios_descriptor(
	const pcsx2_ax::core::VmContext& ctx = pcsx2_ax::vm_bridge::active_context()) noexcept
{
	if (const auto* adapter = active_mode_adapter(ctx); adapter != nullptr)
		return adapter->bios_descriptor();
	return pcsx2_ax::vm_bridge::current_bios_info(ctx);
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
void DSLLV() { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = static_cast<u64>(cpuRegs.GPR.r[_Rt_].UD[0] << (cpuRegs.GPR.r[_Rs_].UL[0] & 0x3fu)); }
void DSRAV() { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].SD[0] = static_cast<s64>(cpuRegs.GPR.r[_Rt_].SD[0] >> (cpuRegs.GPR.r[_Rs_].UL[0] & 0x3fu)); }
void DSRLV() { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = static_cast<u64>(cpuRegs.GPR.r[_Rt_].UD[0] >> (cpuRegs.GPR.r[_Rs_].UL[0] & 0x3fu)); }

__noinline static void RaiseAddressError(u32 addr, bool store)
{
	Console.Error(fmt::format("Address Error, addr=0x{:x} [{}]", addr, store ? "store" : "load"));
	Cpu->CancelInstruction();
}

static __fi void sync_ee_counter_if_mmio(u32 addr)
{
	if ((addr & 0xFFFFE000u) == 0x10000000u)
	{
		intUpdateCPUCycles();
		intEventTest();
	}
}

void LB()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	const s8 temp = memRead8(addr);
	if (_Rt_)
		cpuRegs.GPR.r[_Rt_].SD[0] = temp;
	sync_ee_counter_if_mmio(addr);
}

void LBU()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	const u8 temp = memRead8(addr);
	if (_Rt_)
		cpuRegs.GPR.r[_Rt_].UD[0] = temp;
	sync_ee_counter_if_mmio(addr);
}

void LH()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	if (addr & 1u) [[unlikely]]
		RaiseAddressError(addr, false);
	const s16 temp = memRead16(addr);
	if (_Rt_)
		cpuRegs.GPR.r[_Rt_].SD[0] = temp;
	sync_ee_counter_if_mmio(addr);
}

void LHU()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	if (addr & 1u) [[unlikely]]
		RaiseAddressError(addr, false);
	const u16 temp = memRead16(addr);
	if (_Rt_)
		cpuRegs.GPR.r[_Rt_].UD[0] = temp;
	sync_ee_counter_if_mmio(addr);
}

void LW()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	if (addr & 3u) [[unlikely]]
		RaiseAddressError(addr, false);
	const u32 temp = memRead32(addr);
	if (_Rt_)
		cpuRegs.GPR.r[_Rt_].SD[0] = static_cast<s32>(temp);
	sync_ee_counter_if_mmio(addr);
}

void LWU()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	if (addr & 3u) [[unlikely]]
		RaiseAddressError(addr, false);
	const u32 temp = memRead32(addr);
	if (_Rt_)
		cpuRegs.GPR.r[_Rt_].UD[0] = temp;
}

static constexpr u32 LWL_MASK[4]  = { 0xffffffu, 0x0000ffffu, 0x000000ffu, 0x00000000u };
static constexpr u32 LWR_MASK[4]  = { 0x000000u, 0xff000000u, 0xffff0000u, 0xffffff00u };
static constexpr u8  LWL_SHIFT[4] = { 24, 16, 8, 0 };
static constexpr u8  LWR_SHIFT[4] = { 0, 8, 16, 24 };

void LWL()
{
	const s32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	const u32 shift = static_cast<u32>(addr) & 3u;
	const u32 mem = memRead32(static_cast<u32>(addr) & ~3u);
	if (!_Rt_)
		return;
	cpuRegs.GPR.r[_Rt_].SD[0] = static_cast<s32>((cpuRegs.GPR.r[_Rt_].UL[0] & LWL_MASK[shift]) | (mem << LWL_SHIFT[shift]));
}

void LWR()
{
	const s32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	const u32 shift = static_cast<u32>(addr) & 3u;
	u32 mem = memRead32(static_cast<u32>(addr) & ~3u);
	if (!_Rt_)
		return;
	mem = (cpuRegs.GPR.r[_Rt_].UL[0] & LWR_MASK[shift]) | (mem >> LWR_SHIFT[shift]);
	if (shift == 0u)
		cpuRegs.GPR.r[_Rt_].SD[0] = static_cast<s32>(mem);
	else
		cpuRegs.GPR.r[_Rt_].UL[0] = mem;
}

alignas(16) static GPR_reg m_dummy_gpr_zero;

static __fi GPR_reg* gpr_GetWritePtr(uint gpr)
{
	return (gpr == 0) ? &m_dummy_gpr_zero : &cpuRegs.GPR.r[gpr];
}

void LD()
{
	const s32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	if (static_cast<u32>(addr) & 7u) [[unlikely]]
		RaiseAddressError(static_cast<u32>(addr), false);
	if (!_Rt_)
		return;
	cpuRegs.GPR.r[_Rt_].UD[0] = memRead64(static_cast<u32>(addr));
}

static constexpr u64 LDL_MASK[8] = {
	0x00ffffffffffffffULL, 0x0000ffffffffffffULL, 0x000000ffffffffffULL, 0x00000000ffffffffULL,
	0x0000000000ffffffULL, 0x000000000000ffffULL, 0x00000000000000ffULL, 0x0000000000000000ULL
};
static constexpr u64 LDR_MASK[8] = {
	0x0000000000000000ULL, 0xff00000000000000ULL, 0xffff000000000000ULL, 0xffffff0000000000ULL,
	0xffffffff00000000ULL, 0xffffffffff000000ULL, 0xffffffffffff0000ULL, 0xffffffffffffff00ULL
};
static constexpr u8 LDR_SHIFT[8] = { 0, 8, 16, 24, 32, 40, 48, 56 };
static constexpr u8 LDL_SHIFT[8] = { 56, 48, 40, 32, 24, 16, 8, 0 };

void LDL()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	const u32 shift = addr & 7u;
	const u64 mem = memRead64(addr & ~7u);
	if (!_Rt_)
		return;
	cpuRegs.GPR.r[_Rt_].UD[0] = (cpuRegs.GPR.r[_Rt_].UD[0] & LDL_MASK[shift]) | (mem << LDL_SHIFT[shift]);
}

void LDR()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	const u32 shift = addr & 7u;
	const u64 mem = memRead64(addr & ~7u);
	if (!_Rt_)
		return;
	cpuRegs.GPR.r[_Rt_].UD[0] = (cpuRegs.GPR.r[_Rt_].UD[0] & LDR_MASK[shift]) | (mem >> LDR_SHIFT[shift]);
}

void LQ()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	memRead128(addr & ~0xfu, reinterpret_cast<u128*>(gpr_GetWritePtr(_Rt_)));
}

void SB()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	memWrite8(addr, cpuRegs.GPR.r[_Rt_].UC[0]);
}

void SH()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	if (addr & 1u) [[unlikely]]
		RaiseAddressError(addr, true);
	memWrite16(addr, cpuRegs.GPR.r[_Rt_].US[0]);
}

void SW()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	if (addr & 3u) [[unlikely]]
		RaiseAddressError(addr, true);
	memWrite32(addr, cpuRegs.GPR.r[_Rt_].UL[0]);
}

static constexpr u32 SWL_MASK[4]  = { 0xffffff00u, 0xffff0000u, 0xff000000u, 0x00000000u };
static constexpr u32 SWR_MASK[4]  = { 0x00000000u, 0x000000ffu, 0x0000ffffu, 0x00ffffffu };
static constexpr u8  SWR_SHIFT[4] = { 0, 8, 16, 24 };
static constexpr u8  SWL_SHIFT[4] = { 24, 16, 8, 0 };

void SWL()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	const u32 shift = addr & 3u;
	const u32 mem = memRead32(addr & ~3u);
	memWrite32(addr & ~3u, (cpuRegs.GPR.r[_Rt_].UL[0] >> SWL_SHIFT[shift]) | (mem & SWL_MASK[shift]));
}

void SWR()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	const u32 shift = addr & 3u;
	const u32 mem = memRead32(addr & ~3u);
	memWrite32(addr & ~3u, (cpuRegs.GPR.r[_Rt_].UL[0] << SWR_SHIFT[shift]) | (mem & SWR_MASK[shift]));
}

void SD()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	if (addr & 7u) [[unlikely]]
		RaiseAddressError(addr, true);
	memWrite64(addr, cpuRegs.GPR.r[_Rt_].UD[0]);
}

static constexpr u64 SDL_MASK[8] = {
	0xffffffffffffff00ULL, 0xffffffffffff0000ULL, 0xffffffffff000000ULL, 0xffffffff00000000ULL,
	0xffffff0000000000ULL, 0xffff000000000000ULL, 0xff00000000000000ULL, 0x0000000000000000ULL
};
static constexpr u64 SDR_MASK[8] = {
	0x0000000000000000ULL, 0x00000000000000ffULL, 0x000000000000ffffULL, 0x0000000000ffffffULL,
	0x00000000ffffffffULL, 0x000000ffffffffffULL, 0x0000ffffffffffffULL, 0x00ffffffffffffffULL
};
static constexpr u8 SDL_SHIFT[8] = { 56, 48, 40, 32, 24, 16, 8, 0 };
static constexpr u8 SDR_SHIFT[8] = { 0, 8, 16, 24, 32, 40, 48, 56 };

void SDL()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	const u32 shift = addr & 7u;
	const u64 mem = memRead64(addr & ~7u);
	memWrite64(addr & ~7u, (cpuRegs.GPR.r[_Rt_].UD[0] >> SDL_SHIFT[shift]) | (mem & SDL_MASK[shift]));
}

void SDR()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	const u32 shift = addr & 7u;
	const u64 mem = memRead64(addr & ~7u);
	memWrite64(addr & ~7u, (cpuRegs.GPR.r[_Rt_].UD[0] << SDR_SHIFT[shift]) | (mem & SDR_MASK[shift]));
}

void SQ()
{
	const u32 addr = cpuRegs.GPR.r[_Rs_].UL[0] + _Imm_;
	memWrite128(addr & ~0xfu, cpuRegs.GPR.r[_Rt_].UQ);
}

void MOVZ()
{
	if (_Rd_ && cpuRegs.GPR.r[_Rt_].UD[0] == 0)
		cpuRegs.GPR.r[_Rd_].UD[0] = cpuRegs.GPR.r[_Rs_].UD[0];
}

void MOVN()
{
	if (_Rd_ && cpuRegs.GPR.r[_Rt_].UD[0] != 0)
		cpuRegs.GPR.r[_Rd_].UD[0] = cpuRegs.GPR.r[_Rs_].UD[0];
}

#include "Sifcmd.h"

void SYSCALL()
{
	const u8 call = (cpuRegs.GPR.n.v1.SL[0] < 0)
		? static_cast<u8>(-cpuRegs.GPR.n.v1.SL[0])
		: cpuRegs.GPR.n.v1.UC[0];

	BIOS_LOG("Bios call: %s (%x)", R5900::bios[call], call);

	switch (static_cast<Syscall>(call))
	{
		case Syscall::SetGsCrt:
		{
			gsIsInterlaced = (cpuRegs.GPR.n.a0.UL[0] & 1u) != 0;
			const bool gsIsFrameMode = (cpuRegs.GPR.n.a2.UL[0] & 1u) != 0;
			const char* inter = gsIsInterlaced ? "Interlaced" : "Progressive";
			const char* field = gsIsFrameMode ? "FRAME" : "FIELD";
			const char* mode = "Unknown";

			switch (cpuRegs.GPR.n.a1.UC[0])
			{
				case 0x0:
				case 0x2:
					mode = "NTSC 640x448 @ 59.940 (59.82)"; gsSetVideoMode(GS_VideoMode::NTSC); break;
				case 0x1:
				case 0x3:
					mode = "PAL  640x512 @ 50.000 (49.76)"; gsSetVideoMode(GS_VideoMode::PAL); break;
				case 0x1A: mode = "VESA 640x480 @ 59.940"; gsSetVideoMode(GS_VideoMode::VESA); break;
				case 0x1B: mode = "VESA 640x480 @ 72.809"; gsSetVideoMode(GS_VideoMode::VESA); break;
				case 0x1C: mode = "VESA 640x480 @ 75.000"; gsSetVideoMode(GS_VideoMode::VESA); break;
				case 0x1D: mode = "VESA 640x480 @ 85.008"; gsSetVideoMode(GS_VideoMode::VESA); break;
				case 0x2A: mode = "VESA 800x600 @ 56.250"; gsSetVideoMode(GS_VideoMode::VESA); break;
				case 0x2B: mode = "VESA 800x600 @ 60.317"; gsSetVideoMode(GS_VideoMode::VESA); break;
				case 0x2C: mode = "VESA 800x600 @ 72.188"; gsSetVideoMode(GS_VideoMode::VESA); break;
				case 0x2D: mode = "VESA 800x600 @ 75.000"; gsSetVideoMode(GS_VideoMode::VESA); break;
				case 0x2E: mode = "VESA 800x600 @ 85.061"; gsSetVideoMode(GS_VideoMode::VESA); break;
				case 0x3B: mode = "VESA 1024x768 @ 60.004"; gsSetVideoMode(GS_VideoMode::VESA); break;
				case 0x3C: mode = "VESA 1024x768 @ 70.069"; gsSetVideoMode(GS_VideoMode::VESA); break;
				case 0x3D: mode = "VESA 1024x768 @ 75.029"; gsSetVideoMode(GS_VideoMode::VESA); break;
				case 0x3E: mode = "VESA 1024x768 @ 84.997"; gsSetVideoMode(GS_VideoMode::VESA); break;
				case 0x4A: mode = "VESA 1280x1024 @ 63.981"; gsSetVideoMode(GS_VideoMode::VESA); break;
				case 0x4B: mode = "VESA 1280x1024 @ 79.976"; gsSetVideoMode(GS_VideoMode::VESA); break;
				case 0x50: mode = "SDTV   720x480 @ 59.94"; gsSetVideoMode(GS_VideoMode::SDTV_480P); break;
				case 0x51: mode = "HDTV 1920x1080 @ 60.00"; gsSetVideoMode(GS_VideoMode::HDTV_1080I); break;
				case 0x52: mode = "HDTV  1280x720 @ ??.???"; gsSetVideoMode(GS_VideoMode::HDTV_720P); break;
				case 0x53: mode = "SDTV   768x576 @ ??.???"; gsSetVideoMode(GS_VideoMode::SDTV_576P); break;
				case 0x54: mode = "HDTV 1920x1080 @ ??.???"; gsSetVideoMode(GS_VideoMode::HDTV_1080P); break;
				case 0x72:
				case 0x82: mode = "DVD NTSC 640x448 @ ??.???"; gsSetVideoMode(GS_VideoMode::DVD_NTSC); break;
				case 0x73:
				case 0x83: mode = "DVD PAL 720x480 @ ??.???"; gsSetVideoMode(GS_VideoMode::DVD_PAL); break;
				default:
					DevCon.Error("Mode %x is not supported. Report me upstream", cpuRegs.GPR.n.a1.UC[0]);
					gsSetVideoMode(GS_VideoMode::Unknown);
					break;
			}
			DevCon.Warning("Set GS CRTC configuration. %s %s (%s)", mode, inter, field);
			break;
		}

		case Syscall::ExecPS2:
		{
			if (DebugInterface::getPauseOnEntry())
			{
				CBreakPoints::AddBreakPoint(BREAKPOINT_EE, cpuRegs.GPR.n.a0.UL[0], true);
				DebugInterface::setPauseOnEntry(false);
			}
			break;
		}

		case Syscall::RFU060:
			if (CHECK_EXTRAMEM && cpuRegs.GPR.n.a1.UL[0] == 0xFFFFFFFFu)
				cpuRegs.GPR.n.a1.UL[0] = Ps2MemSize::ExposedRam - cpuRegs.GPR.n.a2.SL[0];
			break;

		case Syscall::SetOsdConfigParam:
			AllowParams1 = true;
			break;

		case Syscall::GetOsdConfigParam:
			if (!NoOSD && !AllowParams1)
			{
				ReadOSDConfigParames();
				const u32 memaddr = cpuRegs.GPR.n.a0.UL[0];
				memWrite32(memaddr, configParams1.UL[0]);

				if (cpuRegs.GPR.n.v1.SL[0] < 0)
					cpuRegs.GPR.n.v1.SL[0] = -Syscall::SetOsdConfigParam;
				else
					cpuRegs.GPR.n.v1.UC[0] = Syscall::SetOsdConfigParam;

				AllowParams1 = true;
			}
			break;

		case Syscall::SetOsdConfigParam2:
			if (!AllowParams2)
			{
				ReadOSDConfigParames();
				u32 memaddr = cpuRegs.GPR.n.a0.UL[0];
				const u32 size = cpuRegs.GPR.n.a1.UL[0];
				u32 offset = cpuRegs.GPR.n.a2.UL[0];

				if (offset == 0 && size >= 4)
					AllowParams2 = true;

				for (u32 i = 0; i < size && offset < 4; ++i)
					configParams2.UC[offset++] = memRead8(memaddr++);
			}
			break;

		case Syscall::GetOsdConfigParam2:
			if (!NoOSD && !AllowParams2)
			{
				ReadOSDConfigParames();
				u32 memaddr = cpuRegs.GPR.n.a0.UL[0];
				const u32 size = cpuRegs.GPR.n.a1.UL[0];
				u32 offset = cpuRegs.GPR.n.a2.UL[0];

				if (offset + size > 2)
					Console.Warning("Warning: GetOsdConfigParam2 Reading extended language/version configs, may be incorrect!");

				for (u32 i = 0; i < size; ++i)
				{
					if (offset >= 4)
						memWrite8(memaddr++, 0);
					else
						memWrite8(memaddr++, configParams2.UC[offset++]);
				}
				return;
			}
			break;

		case Syscall::SetVTLBRefillHandler:
			DevCon.Warning("A tlb refill handler is set. New handler %x", static_cast<u32*>(PSM(cpuRegs.GPR.n.a1.UL[0])));
			break;

		case Syscall::StartThread:
		case Syscall::ChangeThreadPriority:
		{
			u32& ee_thread_list = pcsx2_ax::r5900::ee_thread_list_addr();
			if (ee_thread_list == 0)
			{
				u32 offset = 0x0;
				while (offset < 0x5000u)
				{
					u32 addr = 0x80000000u + offset;
					const u32 inst1 = memRead32(addr);
					const u32 inst2 = memRead32(addr += 4u);
					const u32 inst3 = memRead32(addr += 4u);

					if (ThreadListInstructions[0] == inst1 &&
						ThreadListInstructions[1] == inst2 &&
						ThreadListInstructions[2] == inst3)
					{
						const u32 op = memRead32(0x80000000u + offset + (sizeof(u32) * 6u));
						ee_thread_list = 0x80010000u + static_cast<u16>(op) - 8u;
						DevCon.WriteLn("BIOS: Successfully found the instruction pattern. Assuming the thread list is here: %0x", ee_thread_list);
						break;
					}
					offset += 4u;
				}
				if (ee_thread_list == 0)
				{
					ee_thread_list = static_cast<u32>(-1);
					Console.Warning("BIOS Warning: Unable to get a thread list offset. The debugger thread and stack frame views will not be functional.");
				}
			}
			break;
		}

		case Syscall::sceSifSetDma:
			if (TraceActive(EE.Bios))
			{
				const int n_transfer = static_cast<int>(cpuRegs.GPR.n.a1.UL[0]) - 1;
				if (n_transfer >= 0)
				{
					const u32 addr = cpuRegs.GPR.n.a0.UL[0] + static_cast<u32>(n_transfer) * sizeof(t_sif_dma_transfer);
					const auto* dmat = static_cast<const t_sif_dma_transfer*>(PSM(addr));
					BIOS_LOG("bios_%s: n_transfer=%d, size=%x, attr=%x, dest=%x, src=%x",
						R5900::bios[cpuRegs.GPR.n.v1.UC[0]], n_transfer,
						dmat->size, dmat->attr, dmat->dest, dmat->src);
				}
			}
			break;

		case Syscall::Deci2Call:
			if (cpuRegs.GPR.n.a0.UL[0] == 0x10u)
				eeConLog(ShiftJIS_ConvertString(static_cast<const char*>(PSM(memRead32(cpuRegs.GPR.n.a1.UL[0])))));
			else
				__Deci2Call(static_cast<int>(cpuRegs.GPR.n.a0.UL[0]), static_cast<u32*>(PSM(cpuRegs.GPR.n.a1.UL[0])));
			break;

		case Syscall::sysPrintOut:
			if (cpuRegs.GPR.n.a0.UL[0] != 0)
			{
				const char* fmt = static_cast<const char*>(PSM(cpuRegs.GPR.n.a0.UL[0]));
				u64 regs[7] = {
					cpuRegs.GPR.n.a1.UL[0],
					cpuRegs.GPR.n.a2.UL[0],
					cpuRegs.GPR.n.a3.UL[0],
					cpuRegs.GPR.n.t0.UL[0],
					cpuRegs.GPR.n.t1.UL[0],
					cpuRegs.GPR.n.t2.UL[0],
					cpuRegs.GPR.n.t3.UL[0],
				};

				int curRegArg = 0;
				for (int i = 0; fmt[i] != '\0'; ++i)
				{
					if (fmt[i] == '%')
					{
						if (i == 0 || fmt[i - 1] != '%')
						{
							if (fmt[i + 1] == 's' && curRegArg < 7)
								regs[curRegArg] = reinterpret_cast<u64>(PSM(static_cast<u32>(regs[curRegArg])));
							++curRegArg;
						}
					}
				}

				char buf[2048];
				std::snprintf(buf, sizeof(buf), fmt,
					regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6]);
				eeConLog(buf);
			}
			break;

		case Syscall::GetMemorySize:
			if (CHECK_EXTRAMEM)
			{
				cpuRegs.GPR.n.v0.UL[0] = Ps2MemSize::ExposedRam;
				return;
			}
			break;

		default:
			break;
	}

	cpuRegs.pc -= 4;
	cpuException(0x20, cpuRegs.branch);
}

void BREAK()
{
	cpuRegs.pc -= 4;
	cpuException(0x24, cpuRegs.branch);
}

void MFSA() { if (!_Rd_) return; cpuRegs.GPR.r[_Rd_].UD[0] = static_cast<u64>(cpuRegs.sa); }
void MTSA() { cpuRegs.sa = static_cast<u32>(cpuRegs.GPR.r[_Rs_].UD[0]); }
void SYNC() {}
void PREF() {}

static void trap(u16 code = 0)
{
	cpuRegs.pc -= 4;
	Console.Warning("Trap exception at 0x%08x", cpuRegs.pc);
	cpuException(0x34, cpuRegs.branch);
}

void TGE()  { if (cpuRegs.GPR.r[_Rs_].SD[0] >= cpuRegs.GPR.r[_Rt_].SD[0]) trap(_TrapCode_); }
void TGEU() { if (cpuRegs.GPR.r[_Rs_].UD[0] >= cpuRegs.GPR.r[_Rt_].UD[0]) trap(_TrapCode_); }
void TLT()  { if (cpuRegs.GPR.r[_Rs_].SD[0] <  cpuRegs.GPR.r[_Rt_].SD[0]) trap(_TrapCode_); }
void TLTU() { if (cpuRegs.GPR.r[_Rs_].UD[0] <  cpuRegs.GPR.r[_Rt_].UD[0]) trap(_TrapCode_); }
void TEQ()  { if (cpuRegs.GPR.r[_Rs_].SD[0] == cpuRegs.GPR.r[_Rt_].SD[0]) trap(_TrapCode_); }
void TNE()  { if (cpuRegs.GPR.r[_Rs_].SD[0] != cpuRegs.GPR.r[_Rt_].SD[0]) trap(_TrapCode_); }

void TGEI()  { if (cpuRegs.GPR.r[_Rs_].SD[0] >= _Imm_) trap(); }
void TLTI()  { if (cpuRegs.GPR.r[_Rs_].SD[0] <  _Imm_) trap(); }
void TEQI()  { if (cpuRegs.GPR.r[_Rs_].SD[0] == _Imm_) trap(); }
void TNEI()  { if (cpuRegs.GPR.r[_Rs_].SD[0] != _Imm_) trap(); }
void TGEIU() { if (cpuRegs.GPR.r[_Rs_].UD[0] >= static_cast<u64>(_Imm_)) trap(); }
void TLTIU() { if (cpuRegs.GPR.r[_Rs_].UD[0] <  static_cast<u64>(_Imm_)) trap(); }

void MTSAB() { cpuRegs.sa = ((cpuRegs.GPR.r[_Rs_].UL[0] & 0xFu) ^ (static_cast<u32>(_Imm_) & 0xFu)); }
void MTSAH() { cpuRegs.sa = ((cpuRegs.GPR.r[_Rs_].UL[0] & 0x7u) ^ (static_cast<u32>(_Imm_) & 0x7u)) << 1; }

} } } // end namespace R5900::Interpreter::OpcodeImpl
