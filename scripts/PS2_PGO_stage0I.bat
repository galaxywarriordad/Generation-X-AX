@echo off
setlocal enabledelayedexpansion
title PCSX2_AX Pipeline - Stage 0I (Incremental Delta)

call "%~dp0_ps2_toolchain_env.bat"
if !errorlevel! neq 0 exit /b 1

set "BUILD_DIR=%PROJ_DIR%\build_ps2_stage0"
set "LOG_FILE=%PROJ_DIR%\build_ps2_stage0_incremental.log"

echo =======================================================================
echo  [PS2 STAGE 0I - INCREMENTAL] Ninja Delta Rebuild (-j 4)
echo  Source: %PCSX2_AX_SRC%
echo  Build : %BUILD_DIR%
echo =======================================================================

taskkill /F /IM pcsx2_ax_harness.exe /T >nul 2>&1

if not exist "%BUILD_DIR%\build.ninja" (
    echo [WARN] No existing build_ps2_stage0 tree found; running Stage 0LD...
    call "%~dp0PS2_PGO_stage0LD.bat"
    exit /b !errorlevel!
)

"%NINJA_EXE%" -C "%BUILD_DIR%" -j 4 pcsx2_ax_harness > "%LOG_FILE%" 2>&1
if !errorlevel! neq 0 (
    echo [@=FATAL] Incremental build failed. Log: %LOG_FILE%
    type "%LOG_FILE%"
    exit /b 1
)

echo [PASS] Stage 0I incremental build succeeded: %BUILD_DIR%
exit /b 0