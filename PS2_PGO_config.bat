@echo off
setlocal enabledelayedexpansion
title PCSX2_AX Configurator Hub (Prototype Phase)
mode con: cols=135 lines=45

chcp 65001 >nul
set "PYTHONIOENCODING=utf-8"
for /f %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"

set "HUB_DIR=%~dp0"
if "%HUB_DIR:~-1%"=="\" set "HUB_DIR=%HUB_DIR:~0,-1%"

set "PROJ_DIR="
if exist "%HUB_DIR%\engines\pcsx2_ax\CMakeLists.txt" (
    set "PROJ_DIR=%HUB_DIR%"
) else if exist "%HUB_DIR%\..\engines\pcsx2_ax\CMakeLists.txt" (
    for %%i in ("%HUB_DIR%\..") do set "PROJ_DIR=%%~fi"
) else if exist "%HUB_DIR%\..\..\engines\pcsx2_ax\CMakeLists.txt" (
    for %%i in ("%HUB_DIR%\..\..") do set "PROJ_DIR=%%~fi"
)
if not defined PROJ_DIR set "PROJ_DIR=%HUB_DIR%"
if "%PROJ_DIR:~-1%"=="\" set "PROJ_DIR=%PROJ_DIR:~0,-1%"

set "SCRIPTS_DIR=%HUB_DIR%\Scripts"
if not exist "%SCRIPTS_DIR%" if exist "%PROJ_DIR%\Scripts" set "SCRIPTS_DIR=%PROJ_DIR%\Scripts"

if exist "%SCRIPTS_DIR%\resolve_ax_tools.bat" call "%SCRIPTS_DIR%\resolve_ax_tools.bat" >nul 2>&1

set "HW_CHECK_LOG=%TEMP%\pcsx2_ax_hw_check.log"
if defined AX_TOOLS_DIR if exist "%AX_TOOLS_DIR%\check_hardware.ps1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%AX_TOOLS_DIR%\check_hardware.ps1" -TargetApp pcsx2_ax -RepoRoot "%PROJ_DIR%" > "%HW_CHECK_LOG%" 2>nul
) else (
    echo [WARN] ax-sdk\tools\check_hardware.ps1 not found > "%HW_CHECK_LOG%"
)

set "CORE0_STATUS=Core 0 (0x01) Audio / HiBy FC3 Lock"
set "CORE1_STATUS=Core 1 (0x02) Vulkan 1.3 GS / MTGS"
set "CORE5_STATUS=Core 5 (0x20) I/O / Sync / Prefetch"

for /F "tokens=*" %%A in ('findstr /C:"Audio/Timing" "%HW_CHECK_LOG%" 2^>nul') do set "CORE0_STATUS=%%A"
for /F "tokens=*" %%A in ('findstr /C:"Vulkan Submit" "%HW_CHECK_LOG%" 2^>nul') do set "CORE1_STATUS=%%A"
for /F "tokens=*" %%A in ('findstr /C:"Aux Quarantine" "%HW_CHECK_LOG%" 2^>nul') do set "CORE5_STATUS=%%A"

:menu_loop
cls
echo                      (   (   (
echo                       )   )   )             PCSX2_AX Configurator Hub (Prototype Phase)
echo                  ._________________.
echo                 /  ###############  \         --------------------------------------------
echo                /   ###############   \---\      === PS2 + COH-H [AX] Build Hub (stay tuned!) ===
echo               /    ###############    \   \   --------------------------------------------
echo              /     ###############     /  /   CPU: Intel i5-9600K / i5-8600K (6C/6T, 2:3:1)
echo              \     ###############    /  /    GPU: NVIDIA GTX 980 Ti (6 GB Maxwell, Vulkan 1.3)
echo               \    ###############   /--/     VRAM Budget: 5.00 GiB Prune / 5.50 GiB Ceiling
echo                \____________________/         SDK: ax-sdk (v1_36_2) ^| Target: pcsx2_ax_harness
echo                  `================'           Audio: HiBy FC3 (/run/lock/ax-dac.lock / FlexASIO)
echo                                               --------------------------------------------

echo CPU ^& GPU ^& Storage Affinity Status:
echo   Audio / DAC Status:   !CORE0_STATUS!
echo   Vulkan GS Status:     !CORE1_STATUS!
echo   Storage / Sync Status:!CORE5_STATUS!

echo =======================================================================
echo  PCSX2_AX Central Build ^& PGO Hub:
echo  [NOTE] Launch from an x64 Native Tools Command Prompt for VS (VS 18 + LLVM).
echo =======================================================================
echo  Select stage or utility (Q to Quit):
echo  Standard
echo    [0]   PGO Stage 0 - Control / High-Velocity Linkage
echo    [1]   Stage 1     - Instrumented Target Generation (-fprofile-generate)
echo    [2]   Stage 2     - PGO + ThinLTO + Skylake Optimizer (-fprofile-use)
echo  Advanced
echo    [CA]  Clean All   - Purge PCSX2_AX Build ^& PGO Trees
echo    [0LD] Stage 0LD   - Clean Cold Diagnostics Build
echo    [0I]  Stage 0I    - Incremental Delta Rebuild (Ninja -j 4)
echo    [SV]  Shader Gate - Validate PS2 Vulkan Fragment Shaders (SPIR-V 1.3)
echo    [TL]  Triage Log  - Live PS2 / PCSX2_AX Runtime Triage
echo =======================================================================
echo.

set "CHOICE_STAGE="
set /p "CHOICE_STAGE=Enter target stage [0 / 1 / 2 / CA / 0LD / 0I / SV / TL / Q]: "

if /i "%CHOICE_STAGE%"=="Q" exit /b 0

echo [%DATE% %TIME%] User selected: %CHOICE_STAGE% >> "%PROJ_DIR%\build_pcsx2_ax_session.log"

set "TARGET_NAME="
set "TARGET_SCRIPT="

if /i "%CHOICE_STAGE%"=="0" (
    set "TARGET_NAME=PS2 PGO Stage 0 - Control / High-Velocity"
    set "TARGET_SCRIPT=PS2_PGO_stage0.bat"
)
if /i "%CHOICE_STAGE%"=="0LD" (
    set "TARGET_NAME=PS2 Stage 0LD - Clean Diagnostics"
    set "TARGET_SCRIPT=PS2_PGO_stage0LD.bat"
)
if /i "%CHOICE_STAGE%"=="0I" (
    set "TARGET_NAME=PS2 Stage 0I - Incremental Delta Rebuild"
    set "TARGET_SCRIPT=PS2_PGO_stage0I.bat"
)
if /i "%CHOICE_STAGE%"=="1" (
    set "TARGET_NAME=PS2 Stage 1 - Instrumentation"
    set "TARGET_SCRIPT=PS2_PGO_stage1.bat"
)
if /i "%CHOICE_STAGE%"=="2" (
    set "TARGET_NAME=PS2 Stage 2 - Optimisation"
    set "TARGET_SCRIPT=PS2_PGO_stage2.bat"
)
if /i "%CHOICE_STAGE%"=="SV" (
    set "TARGET_NAME=PS2 Vulkan Shader Validation Gate"
    set "TARGET_SCRIPT=_shader_gate_runner"
)
if /i "%CHOICE_STAGE%"=="TL" (
    set "TARGET_NAME=Live PS2 Runtime Triage"
    set "TARGET_SCRIPT=_tail_ps2_runner"
)
if /i "%CHOICE_STAGE%"=="CA" (
    set "TARGET_NAME=Clean PCSX2_AX Build Trees"
    set "TARGET_SCRIPT=_clean_ps2_builds"
)

if "%TARGET_SCRIPT%"=="" (
    echo [ERROR] Invalid selection. Aborting.
    pause
    goto :menu_loop
)

echo.
set "CONFIRM="
set /p "CONFIRM=Start %TARGET_NAME%? [y/N]: "

if /i "%CONFIRM%"=="y" (
    echo [%DATE% %TIME%] Confirmed start: %TARGET_NAME% >> "%PROJ_DIR%\build_pcsx2_ax_session.log"
    if "%TARGET_SCRIPT%"=="_clean_ps2_builds" (
        if defined AX_TOOLS_DIR if exist "%AX_TOOLS_DIR%\clean_all.ps1" (
            powershell -NoProfile -ExecutionPolicy Bypass -File "%AX_TOOLS_DIR%\clean_all.ps1" -ProjDir "%PROJ_DIR%" -Wait
        )
        for %%D in (
            "%PROJ_DIR%\build\pcsx2_ax"
            "%PROJ_DIR%\build_ps2_stage0"
            "%PROJ_DIR%\build_ps2_stage1"
            "%PROJ_DIR%\build_ps2_stage2"
            "%PROJ_DIR%\pgo_ps2_data"
        ) do (
            if exist "%%~D" (
                echo [STATUS] Removing %%~D ...
                rmdir /s /q "%%~D" >nul 2>&1
            )
        )
        echo [PASS] PCSX2_AX build and PGO directories cleaned.
        pause
        goto :menu_loop
    )
    if "%TARGET_SCRIPT%"=="_shader_gate_runner" (
        echo [STATUS] Running ps2_shader_validate.ps1...
        powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPTS_DIR%\ps2_shader_validate.ps1"
        pause
        goto :menu_loop
    )
    if "%TARGET_SCRIPT%"=="_tail_ps2_runner" (
        echo [STATUS] Launching ps2_tail_log.ps1...
        powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPTS_DIR%\ps2_tail_log.ps1" -Mode ps2
        pause
        goto :menu_loop
    )
    echo [STATUS] Launching %SCRIPTS_DIR%\%TARGET_SCRIPT%...
    call "%SCRIPTS_DIR%\%TARGET_SCRIPT%"
    pause
) else (
    echo [STATUS] Operation cancelled.
    echo [%DATE% %TIME%] User cancelled operation >> "%PROJ_DIR%\build_pcsx2_ax_session.log"
    pause
    goto :menu_loop
)
exit /b 0