#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Automated staged PGO build & appliance verification harness for PCSX2_AX
# (Prototype Phase — stay tuned!).
# Stage 1: -fprofile-generate into /dev/shm (tmpfs) + active PS2 / COH-H sweeps
# Merge:  llvm-profdata merge into pcsx2_ax.profdata
# Stage 2: -fprofile-use + -flto=thin + -march=skylake + control & appliance gates

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PCSX2_AX_SRC="${REPO_ROOT}/engines/pcsx2_ax"

BUILD_ROOT="${PCSX2_AX_BUILD_ROOT:-/dev/shm/pcsx2_ax_pgo}"
PGO_RAW_DIR="${PCSX2_AX_PGO_RAW_DIR:-/dev/shm/pgo_profiles}"
PGO_PROFDATA="${PCSX2_AX_PGO_PROFDATA:-${BUILD_ROOT}/pcsx2_ax.profdata}"
EROF_IMAGE="${PICCOLO_AX_EROF_IMAGE:-}"
EROF_PREV_IMAGE="${PICCOLO_AX_EROF_PREV_IMAGE:-}"
EROF_MAX_KB=524288
VRAM_PRUNE_GIB="5.00"
VRAM_CEILING_GIB="5.50"

CC_BIN="${CC:-clang}"
CXX_BIN="${CXX:-clang++}"
LLVM_PROFDATA="${LLVM_PROFDATA:-llvm-profdata}"

PS2_WORKLOAD_CMD="${PCSX2_AX_PS2_SWEEP_CMD:-}"
COHH_WORKLOAD_CMD="${PCSX2_AX_COHH_SWEEP_CMD:-}"
SWEEP_TIMEOUT_SEC="${PCSX2_AX_SWEEP_TIMEOUT_SEC:-60}"

log() {
	printf '[PCSX2_AX PGO | %s] %s\n' "$(date -u +%H:%M:%S)" "$*"
}

die() {
	printf '[PCSX2_AX PGO ERROR] %s\n' "$*" >&2
	exit 1
}

require_tool() {
	command -v "$1" >/dev/null 2>&1 || die "Required tool not found in PATH: $1"
}

run_sweep_step() {
	local label="$1"
	local cmd="$2"
	if [[ -z "${cmd}" ]]; then
		log "Sweep '${label}' command not configured; skipping optional external workload step."
		return 0
	fi
	log "Running active gameplay PGO sweep: ${label}"
	timeout --signal=INT "${SWEEP_TIMEOUT_SEC}s" bash -lc "${cmd}"
}

verify_appliance_gates() {
	local harness_bin="$1"
	log "Verifying PCSX2_AX / Piccolo-AX / ax-sdk (v1_36_2) appliance gates..."
	[[ -x "${harness_bin}" ]] || die "Harness binary missing or not executable: ${harness_bin}"

	"${harness_bin}" --mode ps2 --dry-run
	"${harness_bin}" --mode coh-h --dry-run

	if [[ -n "${EROF_IMAGE}" && -f "${EROF_IMAGE}" ]]; then
		local size_kb
		size_kb="$(du -k "${EROF_IMAGE}" | awk '{print $1}')"
		log "EROFS image size: ${size_kb} KB (ceiling: ${EROF_MAX_KB} KB / 512 MB)"
		if (( size_kb > EROF_MAX_KB )); then
			die "EROFS image exceeds 524288 KB (512 MB) toram limit: ${size_kb} KB"
		fi
	fi

	if [[ -n "${EROF_PREV_IMAGE}" ]]; then
		[[ -f "${EROF_PREV_IMAGE}" ]] || die "Missing A/B fallback image: ${EROF_PREV_IMAGE}"
		log "A/B fallback image verified: ${EROF_PREV_IMAGE}"
	fi

	if command -v nvidia-smi >/dev/null 2>&1; then
		local gpu_mem_mib
		gpu_mem_mib="$(nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits 2>/dev/null | head -n1 | tr -d '[:space:]' || true)"
		if [[ -n "${gpu_mem_mib}" ]]; then
			log "Observed GPU memory used: ${gpu_mem_mib} MiB (prune=${VRAM_PRUNE_GIB} GiB, ceiling=${VRAM_CEILING_GIB} GiB)"
		fi
	fi

	if command -v ax_doctor.sh >/dev/null 2>&1; then
		log "Running read-only ax_doctor.sh pre-flight check..."
		ax_doctor.sh || die "ax_doctor.sh reported pre-flight failure"
	fi
}

require_tool cmake
require_tool ninja
require_tool "${CC_BIN}"
require_tool "${CXX_BIN}"
require_tool "${LLVM_PROFDATA}"

[[ -d "${PCSX2_AX_SRC}" ]] || die "PCSX2_AX source directory not found: ${PCSX2_AX_SRC}"
[[ -d /dev/shm ]] || die "/dev/shm tmpfs mount is required to avoid flash write wear during PGO"

rm -rf "${PGO_RAW_DIR}"
mkdir -p "${PGO_RAW_DIR}" "${BUILD_ROOT}/stage0_control" "${BUILD_ROOT}/stage1_gen" "${BUILD_ROOT}/stage2_use"

# ---------------------------------------------------------------------------
# Stage 0: Clean Control Baseline Build (unpinned/control comparison target)
# ---------------------------------------------------------------------------
log "Stage 0: Configuring & building control baseline (-O2 -march=skylake, no PGO/LTO)"
cmake -G Ninja -S "${PCSX2_AX_SRC}" -B "${BUILD_ROOT}/stage0_control" \
	-DCMAKE_C_COMPILER="${CC_BIN}" \
	-DCMAKE_CXX_COMPILER="${CXX_BIN}" \
	-DPCSX2_AX_PLANNING_ONLY=ON \
	-DPCSX2_AX_ENABLE_LTO=OFF \
	-DPCSX2_AX_BUILD_HARNESS=ON
cmake --build "${BUILD_ROOT}/stage0_control" -- -j 4

# ---------------------------------------------------------------------------
# Stage 1: Instrumented Build (-fprofile-generate -> /dev/shm)
# ---------------------------------------------------------------------------
log "Stage 1: Configuring & building instrumented target into ${PGO_RAW_DIR}"
cmake -G Ninja -S "${PCSX2_AX_SRC}" -B "${BUILD_ROOT}/stage1_gen" \
	-DCMAKE_C_COMPILER="${CC_BIN}" \
	-DCMAKE_CXX_COMPILER="${CXX_BIN}" \
	-DPCSX2_AX_PLANNING_ONLY=ON \
	-DPCSX2_AX_ENABLE_LTO=ON \
	-DPCSX2_AX_BUILD_HARNESS=ON \
	-DCMAKE_C_FLAGS="-march=skylake -fprofile-generate=${PGO_RAW_DIR}" \
	-DCMAKE_CXX_FLAGS="-march=skylake -fprofile-generate=${PGO_RAW_DIR}" \
	-DCMAKE_EXE_LINKER_FLAGS="-flto=thin -fprofile-generate=${PGO_RAW_DIR}"
cmake --build "${BUILD_ROOT}/stage1_gen" -- -j 4

export LLVM_PROFILE_FILE="${PGO_RAW_DIR}/pcsx2_ax-%p-%m.profraw"
"${BUILD_ROOT}/stage1_gen/pcsx2_ax_harness" --mode ps2 --dry-run
"${BUILD_ROOT}/stage1_gen/pcsx2_ax_harness" --mode coh-h --dry-run
run_sweep_step "ps2-active-gameplay" "${PS2_WORKLOAD_CMD}"
run_sweep_step "coh-h-system246-256-gameplay" "${COHH_WORKLOAD_CMD}"

shopt -s nullglob
profraw_files=("${PGO_RAW_DIR}"/*.profraw)
shopt -u nullglob
(( ${#profraw_files[@]} > 0 )) || die "No .profraw instrumentation files generated in ${PGO_RAW_DIR}"

log "Merging ${#profraw_files[@]} raw profiles into ${PGO_PROFDATA}"
"${LLVM_PROFDATA}" merge -output="${PGO_PROFDATA}" "${profraw_files[@]}"

# ---------------------------------------------------------------------------
# Stage 2: Optimized Build (-fprofile-use + -flto=thin + -march=skylake)
# ---------------------------------------------------------------------------
log "Stage 2: Building PGO + ThinLTO + Skylake optimized binary"
cmake -G Ninja -S "${PCSX2_AX_SRC}" -B "${BUILD_ROOT}/stage2_use" \
	-DCMAKE_C_COMPILER="${CC_BIN}" \
	-DCMAKE_CXX_COMPILER="${CXX_BIN}" \
	-DPCSX2_AX_PLANNING_ONLY=ON \
	-DPCSX2_AX_ENABLE_LTO=ON \
	-DPCSX2_AX_BUILD_HARNESS=ON \
	-DCMAKE_C_FLAGS="-march=skylake -fprofile-use=${PGO_PROFDATA} -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date" \
	-DCMAKE_CXX_FLAGS="-march=skylake -fprofile-use=${PGO_PROFDATA} -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date" \
	-DCMAKE_EXE_LINKER_FLAGS="-flto=thin -fprofile-use=${PGO_PROFDATA}"
cmake --build "${BUILD_ROOT}/stage2_use" -- -j 4

verify_appliance_gates "${BUILD_ROOT}/stage2_use/pcsx2_ax_harness"
log "Completed Stage 0 -> Stage 1 -> Stage 2 PGO pipeline cleanly."
