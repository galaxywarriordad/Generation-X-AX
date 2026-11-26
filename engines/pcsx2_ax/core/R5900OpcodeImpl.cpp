// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team & The Silicon Preservation Society
// SPDX-License-Identifier: GPL-3.0+

#include "Common.h"

#include <cstdint>
#include <float.h>

#include "R5900.h"
#include "R5900OpcodeTables.h"
#include "GS.h"
#include "IHardwareModeAdapter.hpp"
#include "VMManager_Internal.h"
#include "DebugTools/DebugInterface.h"
#include "DebugTools/Breakpoints.h"
#include "Host.h"
#include "VMManager.h"

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
	"RFU000_FullReset", "ResetEE", "SetGsCrt", "RFU003",
	"Exit", "RFU005", "LoadExecPS2", "ExecPS2",
	"RFU008", "RFU009", "AddSbusIntcHandler", "RemoveSbusIntcHandler",
	"Interrupt2Iop", "SetVTLBRefillHandler", "SetVCommonHandler", "SetVInterruptHandler",
//0x10
	"AddIntcHandler", "RemoveIntcHandler", "AddDmacHandler", "RemoveDmacHandler",
	"_EnableIntc", "_DisableIntc", "_EnableDmac", "_DisableDmac",
	"_SetAlarm", "_ReleaseAlarm", "_iEnableIntc", "_iDisableIntc",
	"_iEnableDmac", "_iDisableDmac", "_iSetAlarm", "_iReleaseAlarm",
//0x20
	"CreateThread", "DeleteThread", "StartThread", "ExitThread",
	"ExitDeleteThread", "TerminateThread", "iTerminateThread", "DisableDispatchThread",
	"EnableDispatchThread", "ChangeThreadPriority", "iChangeThreadPriority", "RotateThreadReadyQueue",
	"iRotateThreadReadyQueue", "ReleaseWaitThread", "iReleaseWaitThread", "GetThreadId",
//0x30
	"ReferThreadStatus","iReferThreadStatus", "SleepThread", "WakeupThread",
	"_iWakeupThread", "CancelWakeupThread", "iCancelWakeupThread", "SuspendThread",
	"iSuspendThread", "ResumeThread", "iResumeThread", "JoinThread",
	"RFU060", "RFU061", "EndOfHeap", "RFU063",
//0x40
	"CreateSema", "DeleteSema", "SignalSema", "iSignalSema",
	"WaitSema", "PollSema", "iPollSema", "ReferSemaStatus",
	"iReferSemaStatus", "RFU073", "SetOsdConfigParam", "GetOsdConfigParam",
	"GetGsHParam", "GetGsVParam", "SetGsHParam", "SetGsVParam",
//0x50
	"RFU080_CreateEventFlag", "RFU081_DeleteEventFlag",
	"RFU082_SetEventFlag", "RFU083_iSetEventFlag",
	"RFU084_ClearEventFlag", "RFU085_iClearEventFlag",
	"RFU086_WaitEventFlag", "RFU087_PollEventFlag",
	"RFU088_iPollEventFlag", "RFU089_ReferEventFlagStatus",
	"RFU090_iReferEventFlagStatus", "RFU091_GetEntryAddress",
	"EnableIntcHandler_iEnableIntcHandler",
	"DisableIntcHandler_iDisableIntcHandler",
	"EnableDmacHandler_iEnableDmacHandler",
	"DisableDmacHandler_iDisableDmacHandler",
//0x60
	"KSeg0", "EnableCache", "DisableCache", "GetCop0",
	"FlushCache", "RFU101", "CpuConfig", "iGetCop0",
	"iFlushCache", "RFU105", "iCpuConfig", "sceSifStopDma",
	"SetCPUTimerHandler", "SetCPUTimer", "SetOsdConfigParam2", "GetOsdConfigParam2",
//0x70
	"GsGetIMR_iGsGetIMR", "GsGetIMR_iGsPutIMR", "SetPgifHandler", "SetVSyncFlag",
	"RFU116", "print", "sceSifDmaStat_isceSifDmaStat", "sceSifSetDma_isceSifSetDma",
	"sceSifSetDChain_isceSifSetDChain", "sceSifSetReg", "sceSifGetReg", "RFU123",
	"ExecPS2", "RFU125", "RFU126", "RFU127",
};

namespace R5900 {
namespace Interpreter {
namespace OpcodeImpl {

void TLTI() { if (cpuRegs.GPR.r[_Rs_].SD[0] < _Imm_) cpuException(0x34, cpuRegs.branch); }
void TLTIU() { if (cpuRegs.GPR.r[_Rs_].UD[0] < (u32)_Imm_) cpuException(0x34, cpuRegs.branch); }
void MTSAB() { cpuRegs.sa = ((cpuRegs.GPR.r[_Rs_].UL[0] & 0xFu) ^ (static_cast<u32>(_Imm_) & 0xFu)); }
void MTSAH() { cpuRegs.sa = ((cpuRegs.GPR.r[_Rs_].UL[0] & 0x7u) ^ (static_cast<u32>(_Imm_) & 0x7u)) << 1; }

} } } // end namespace R5900::Interpreter::OpcodeImpl
