@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: ============================================================================
:: PS2_PGO_config.bat - [0] Build & Configure App Settings Hub (PCSX2_AX)
:: ============================================================================
:: Interactive hub for PCSX2_AX / PCSX2x6 (COH-H) app configuration,
:: [0] build & configuration stages (Stage 0 / 0LD / 0I),
:: [1] instrumentation (Stage 1), and [2] optimisation (Stage 2).
:: ============================================================================

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

set "GENX_ROOT="
if exist "%SCRIPT_DIR%\engines\pcsx2_ax\CMakeLists.txt" (
  set "GENX_ROOT=%SCRIPT_DIR%"
) else if exist "%SCRIPT_DIR%\core\IHardwareModeAdapter.hpp" (
  for %%I in ("%SCRIPT_DIR%\..\..") do set "GENX_ROOT=%%~fI"
) else if exist "%SCRIPT_DIR%\..\engines\pcsx2_ax\CMakeLists.txt" (
  for %%I in ("%SCRIPT_DIR%\..") do set "GENX_ROOT=%%~fI"
) else (
  set "GENX_ROOT=%SCRIPT_DIR%"
)

for %%I in ("%GENX_ROOT%\..") do set "PARENT_DIR=%%~fI"

if not defined PS2_SRC_DIR set "PS2_SRC_DIR=%GENX_ROOT%\engines\pcsx2_ax"
if not defined PS2_BUILD_DIR set "PS2_BUILD_DIR=%GENX_ROOT%\build_ps2_stage0"
if not defined PGO_PROF_DIR set "PGO_PROF_DIR=%GENX_ROOT%\build_ps2_pgo_prof"
if not defined NINJA_JOBS set "NINJA_JOBS=4"

if not defined AX_SDK_DIR (
  if exist "%PARENT_DIR%\ax-sdk\CMakeLists.txt" (
    set "AX_SDK_DIR=%PARENT_DIR%\ax-sdk"
  ) else if exist "%USERPROFILE%\Desktop\ax-sdk\CMakeLists.txt" (
    set "AX_SDK_DIR=%USERPROFILE%\Desktop\ax-sdk"
  ) else if exist "%USERPROFILE%\ax-sdk\CMakeLists.txt" (
    set "AX_SDK_DIR=%USERPROFILE%\ax-sdk"
  )
)

set "SCRIPTS_DIR=%GENX_ROOT%\scripts"
set "ENV_SCRIPT=%SCRIPTS_DIR%\_ps2_toolchain_env.bat"
if not exist "%ENV_SCRIPT%" if exist "%GENX_ROOT%\_ps2_toolchain_env.bat" set "ENV_SCRIPT=%GENX_ROOT%\_ps2_toolchain_env.bat"

:menu_loop
cls
echo ============================================================================
echo   PCSX2_AX / PCSX2x6 (COH-H) - PGO ^& BUILD CONFIGURATION HUB
echo ============================================================================
echo   GENX_ROOT     : %GENX_ROOT%
echo   PS2_SRC_DIR   : %PS2_SRC_DIR%
echo   PS2_BUILD_DIR : %PS2_BUILD_DIR%
echo   PGO_PROF_DIR  : %PGO_PROF_DIR%
if defined AX_SDK_DIR (
  echo   AX_SDK_DIR    : %AX_SDK_DIR%
) else (
  echo   AX_SDK_DIR    : (not detected locally; CMake fallback applies)
)
echo   Jobs (-j)     : %NINJA_JOBS%
echo ============================================================================
echo   [0]   Build ^& Configure App Settings (PS2_PGO_stage0.bat)
echo   [0LD] Clean Diagnostics Build        (PS2_PGO_stage0LD.bat)
echo   [0I]  Incremental Delta Rebuild      (PS2_PGO_stage0I.bat)
echo   [1]   Instrumentation Build          (PS2_PGO_stage1.bat)
echo   [2]   Optimisation Build             (PS2_PGO_stage2.bat)
echo   --------------------------------------------------------------------------
echo   [E]   Inspect / Refresh Toolchain Environment (_ps2_toolchain_env.bat)
echo   [L]   Tail Latest Build / Session Log (ps2_tail_log.ps1)
echo   [V]   Validate Shaders ^& VRAM Guardrails (ps2_shader_validate.ps1)
echo   [C]   Clean Stale CMakeCache.txt in PS2_BUILD_DIR
echo   [Q]   Exit
echo ============================================================================
set "CHOICE="
set /p "CHOICE=Select option: "

if /I "%CHOICE%"=="0"   goto :run_stage0
if /I "%CHOICE%"=="0LD" goto :run_stage0ld
if /I "%CHOICE%"=="0I"  goto :run_stage0i
if /I "%CHOICE%"=="1"   goto :run_stage1
if /I "%CHOICE%"=="2"   goto :run_stage2
if /I "%CHOICE%"=="E"   goto :run_env
if /I "%CHOICE%"=="L"   goto :run_log
if /I "%CHOICE%"=="V"   goto :run_shader_val
if /I "%CHOICE%"=="C"   goto :clean_cache
if /I "%CHOICE%"=="Q"   goto :quit_hub

echo [WARN] Unknown selection "%CHOICE%".
pause
goto :menu_loop

:run_stage0
call :dispatch_script "PS2_PGO_stage0.bat"
pause
goto :menu_loop

:run_stage0ld
call :dispatch_script "PS2_PGO_stage0LD.bat"
pause
goto :menu_loop

:run_stage0i
call :dispatch_script "PS2_PGO_stage0I.bat"
pause
goto :menu_loop

:run_stage1
echo [NOTE] [1] Instrumentation: avoid manual CPU affinity overrides during profile collection.
echo [NOTE] Exit cleanly so .profraw files flush before [2] Optimisation merge.
call :dispatch_script "PS2_PGO_stage1.bat"
pause
goto :menu_loop

:run_stage2
call :dispatch_script "PS2_PGO_stage2.bat"
pause
goto :menu_loop

:run_env
if exist "%ENV_SCRIPT%" (
  call "%ENV_SCRIPT%"
) else (
  echo [WARN] Environment helper not found at "%ENV_SCRIPT%".
)
pause
goto :menu_loop

:run_log
if exist "%SCRIPTS_DIR%\ps2_tail_log.ps1" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPTS_DIR%\ps2_tail_log.ps1" -RepoRoot "%GENX_ROOT%" -Lines 60
) else (
  echo [WARN] ps2_tail_log.ps1 not found under "%SCRIPTS_DIR%".
)
pause
goto :menu_loop

:run_shader_val
if exist "%SCRIPTS_DIR%\ps2_shader_validate.ps1" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPTS_DIR%\ps2_shader_validate.ps1" -RepoRoot "%GENX_ROOT%"
) else (
  echo [WARN] ps2_shader_validate.ps1 not found under "%SCRIPTS_DIR%".
)
pause
goto :menu_loop

:clean_cache
if exist "%PS2_BUILD_DIR%\CMakeCache.txt" (
  del /q "%PS2_BUILD_DIR%\CMakeCache.txt"
  echo [INFO] Removed "%PS2_BUILD_DIR%\CMakeCache.txt".
) else (
  echo [INFO] No CMakeCache.txt found in "%PS2_BUILD_DIR%".
)
pause
goto :menu_loop

:dispatch_script
set "TARGET_BAT=%~1"
if exist "%SCRIPTS_DIR%\%TARGET_BAT%" (
  call "%SCRIPTS_DIR%\%TARGET_BAT%"
  exit /b %ERRORLEVEL%
) else if exist "%GENX_ROOT%\%TARGET_BAT%" (
  call "%GENX_ROOT%\%TARGET_BAT%"
  exit /b %ERRORLEVEL%
) else if exist "%PS2_SRC_DIR%\%TARGET_BAT%" (
  call "%PS2_SRC_DIR%\%TARGET_BAT%"
  exit /b %ERRORLEVEL%
) else (
  echo [ERROR] Target script "%TARGET_BAT%" not found in scripts\, repo root, or engines\pcsx2_ax\.
  exit /b 1
)

:quit_hub
endlocal
exit /b 0