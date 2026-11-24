#!/usr/bin/env bash
set -euo pipefail

CONFIG_PATH="${AX_ENGINE_INI:-/userdata/system/piccolo-ax/ax_engine.ini}"
ROM_PATH="${1:-}"
PRESET_OVERRIDE="${2:-}"

if [[ -z "${ROM_PATH}" ]]; then
    echo "Usage: $0 <path-to-snes-rom> [stock_cycle_exact|coprocessor_boost|low_latency_input]" >&2
    exit 2
fi

if [[ ! -f "${ROM_PATH}" ]]; then
    echo "[ax-snes] ROM not found: ${ROM_PATH}" >&2
    exit 1
fi

SHM_BASE="/dev/shm/piccolo-ax/snes"
SHM_ROM_DIR="${SHM_BASE}/roms"
SHM_SAVE_DIR="${SHM_BASE}/saves"
PERSIST_SAVE_DIR="/userdata/saves/snes"

mkdir -p "${SHM_ROM_DIR}" "${SHM_SAVE_DIR}" "${PERSIST_SAVE_DIR}"

ROM_BASENAME="$(basename "${ROM_PATH}")"
ROM_STEM="${ROM_BASENAME%.*}"
STAGED_ROM="${SHM_ROM_DIR}/${ROM_BASENAME}"
STAGED_SRAM="${SHM_SAVE_DIR}/${ROM_STEM}.srm"
PERSIST_SRAM="${PERSIST_SAVE_DIR}/${ROM_STEM}.srm"

cp -f "${ROM_PATH}" "${STAGED_ROM}"
if [[ -f "${PERSIST_SRAM}" ]]; then
    cp -f "${PERSIST_SRAM}" "${STAGED_SRAM}"
fi

cleanup() {
    local exit_code=$?
    if [[ -f "${STAGED_SRAM}" ]]; then
        local tmp_sram="${PERSIST_SRAM}.tmp.$$"
        cp -f "${STAGED_SRAM}" "${tmp_sram}"
        mv -f "${tmp_sram}" "${PERSIST_SRAM}"
    fi
    if command -v /etc/init.d/K01piccolo-sync >/dev/null 2>&1; then
        /etc/init.d/K01piccolo-sync save || true
    fi
    exit "${exit_code}"
}
trap cleanup EXIT INT TERM

if command -v nvidia-smi >/dev/null 2>&1; then
    nvidia-smi -pm 1 >/dev/null 2>&1 || true
fi

SNES_BIN="${AX_SNES_BIN:-/usr/bin/genx-snes-ax}"
exec "${SNES_BIN}" \
    --config "${CONFIG_PATH}" \
    --rom "${STAGED_ROM}" \
    --sram "${STAGED_SRAM}" \
    ${PRESET_OVERRIDE:+--preset "${PRESET_OVERRIDE}"}
