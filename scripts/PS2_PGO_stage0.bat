@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: ============================================================================
:: PS2_PGO_stage0.bat — Stage 0 Control Baseline Build (PCSX2_AX)
:: ============================================================================
:: Standalone Stage 0 control build script for PCSX2_AX (does NOT invoke
:: PS2_PGO_config.bat). Resolves paths dynamically from %~dp0 so relocating
:: Generation-X-AX (e.g. C:\Users\nick_\Desktop\Generation-X-AX) works cleanly.
:: ============================================================================

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
for %%I in ("%SCRIPT_DIR%\..") do set "GENX_ROOT=%%~fI"
for %%I in ("%GENX_ROOT%\..") do set "PARENT_DIR=%%~fI"

if not defined PS2_SRC_DIR set "PS2_SRC_DIR=%GENX_ROOT%\engines\pcsx2_ax"
if not defined PS2_BUILD_DIR set "PS2_BUILD_DIR=%GENX_ROOT%\build_ps2_stage0"
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

if not defined LLVM_BIN (
  if exist "%ProgramFiles%\LLVM\bin\clang-cl.exe" (
    set "LLVM_BIN=%ProgramFiles%\LLVM\bin"
  ) else if exist "C:\Program Files\LLVM\bin\clang-cl.exe" (
    set "LLVM_BIN=C:\Program Files\LLVM\bin"
  ) else (
    for /f "delims=" %%C in ('where clang-cl.exe 2^>nul') do (
      set "LLVM_BIN=%%~dpC"
      goto :found_s0_llvm
    )
  )
)
:found_s0_llvm
if defined LLVM_BIN (
  if "!LLVM_BIN:~-1!"=="\" set "LLVM_BIN=!LLVM_BIN:~0,-1%"
  set "PATH=!LLVM_BIN!;%PATH%"
)

set "LOG_SUMMARY=%GENX_ROOT%\build_ps2_stage0_summary.log"
set "LOG_DETAIL=%GENX_ROOT%\build_ps2_stage0_triage.log"
set "LOG_SESSION=%GENX_ROOT%\build_pcsx2_ax_session.log"

set "CLEAN_FIRST=0"
set "PLANNING_ONLY=ON"
set "EXTRA_TARGETS="

:parse_s0_args
if "%~1"=="" goto :s0_args_done
if /I "%~1"=="--clean" (
  set "CLEAN_FIRST=1"
  shift
  goto :parse_s0_args
)
if /I "%~1"=="--full" (
  set "PLANNING_ONLY=OFF"
  shift
  goto :parse_s0_args
)
if /I "%~1"=="--planning" (
  set "PLANNING_ONLY=ON"
  shift
  goto :parse_s0_args
)
set "EXTRA_TARGETS=!EXTRA_TARGETS! %~1"
shift
goto :parse_s0_args
:s0_args_done

set "STAMP=%DATE% %TIME:~0,8%"

echo ============================================================================
echo   PCSX2_AX — STAGE 0 CONTROL BASELINE BUILD (%STAMP%)
echo ============================================================================
echo   GENX_ROOT     : %GENX_ROOT%
echo   PS2_SRC_DIR   : %PS2_SRC_DIR%
echo   PS2_BUILD_DIR : %PS2_BUILD_DIR%
if defined AX_SDK_DIR (
  echo   AX_SDK_DIR    : %AX_SDK_DIR%
) else (
  echo   AX_SDK_DIR    : ^(not found locally; CMake fallback applies^)
)
echo   PLANNING_ONLY : %PLANNING_ONLY%
echo   NINJA_JOBS    : %NINJA_JOBS%
echo ============================================================================

if not exist "%PS2_SRC_DIR%\CMakeLists.txt" (
  echo [ERROR] CMakeLists.txt not found at "%PS2_SRC_DIR%" 1>&2
  exit /b 1
)

>> "%LOG_SESSION%" echo [%STAMP%] Started PS2 Stage 0 Control Build (PLANNING_ONLY=%PLANNING_ONLY%, CLEAN=%CLEAN_FIRST%)
> "%LOG_SUMMARY%" echo Start Timestamp : %STAMP%
>> "%LOG_SUMMARY%" echo Target          : pcsx2_ax_harness (Stage 0 / PLANNING_ONLY=%PLANNING_ONLY%)
>> "%LOG_SUMMARY%" echo Source          : %PS2_SRC_DIR%
>> "%LOG_SUMMARY%" echo Build           : %PS2_BUILD_DIR%

> "%LOG_DETAIL%" echo Timestamp : %STAMP%
>> "%LOG_DETAIL%" echo Source    : %PS2_SRC_DIR%
>> "%LOG_DETAIL%" echo Build     : %PS2_BUILD_DIR%

if defined AX_SDK_DIR if exist "%AX_SDK_DIR%\tools\check_hardware.ps1" (
  echo [PS2-STAGE0] Running ax-sdk pre-flight hardware check...
  powershell -NoProfile -ExecutionPolicy Bypass -File "%AX_SDK_DIR%\tools\check_hardware.ps1" -TargetApp pcsx2_ax_harness -RepoRoot "%GENX_ROOT%" >> "%LOG_DETAIL%" 2>&1
)

if "%CLEAN_FIRST%"=="1" (
  if exist "%PS2_BUILD_DIR%" (
    echo [PS2-STAGE0] Cleaning "%PS2_BUILD_DIR%"...
    if defined AX_SDK_DIR if exist "%AX_SDK_DIR%\tools\clean_all.ps1" (
      powershell -NoProfile -ExecutionPolicy Bypass -File "%AX_SDK_DIR%\tools\clean_all.ps1" -ProjDir "%PS2_BUILD_DIR%" -Wait >> "%LOG_DETAIL%" 2>&1
    )
    if exist "%PS2_BUILD_DIR%" rmdir /s /q "%PS2_BUILD_DIR%"
  )
)

if exist "%PS2_BUILD_DIR%\CMakeCache.txt" (
  findstr /I /C:"%PS2_SRC_DIR:\=/%" "%PS2_BUILD_DIR%\CMakeCache.txt" >nul 2>&1
  if errorlevel 1 (
    echo [PS2-STAGE0] Relocated source path detected; purging stale CMakeCache.txt...
    del /q "%PS2_BUILD_DIR%\CMakeCache.txt"
  )
)

if not exist "%PS2_BUILD_DIR%" mkdir "%PS2_BUILD_DIR%"

set "CMAKE_ARGS=-S "%PS2_SRC_DIR%" -B "%PS2_BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPCSX2_AX_PLANNING_ONLY=%PLANNING_ONLY% -DPCSX2_AX_ENABLE_VULKAN_1_3=ON -DPCSX2_AX_BUILD_HARNESS=ON"

if defined AX_SDK_DIR if exist "%AX_SDK_DIR%\CMakeLists.txt" (
  set "AX_SDK_CMAKE=%AX_SDK_DIR:\=/%"
  set "CMAKE_ARGS=%CMAKE_ARGS% -DAX_SDK_DIR="!AX_SDK_CMAKE!""
)

if defined LLVM_BIN if exist "%LLVM_BIN%\clang-cl.exe" (
  set "LLVM_CMAKE=%LLVM_BIN:\=/%"
  set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_C_COMPILER="!LLVM_CMAKE!/clang-cl.exe" -DCMAKE_CXX_COMPILER="!LLVM_CMAKE!/clang-cl.exe""
) else (
  where clang-cl.exe >nul 2>&1
  if not errorlevel 1 (
    set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_C_COMPILER=clang-cl.exe -DCMAKE_CXX_COMPILER=clang-cl.exe"
  )
)

where clang-cl.exe >nul 2>&1
if not errorlevel 1 (
  clang-cl.exe --version >> "%LOG_DETAIL%" 2>&1
)

echo [PS2-STAGE0] Configuring CMake...
cmake %CMAKE_ARGS% >> "%LOG_DETAIL%" 2>&1
set "CFG_ERR=%ERRORLEVEL%"
if not "%CFG_ERR%"=="0" (
  echo [FAIL] CMake configure failed (%CFG_ERR%). See "%LOG_DETAIL%". 1>&2
  >> "%LOG_SUMMARY%" echo Result          : CONFIG_FAILED (%CFG_ERR%)
  >> "%LOG_SESSION%" echo [%DATE% %TIME:~0,8%] FAILED PS2 Stage 0 configure (%CFG_ERR%)
  exit /b %CFG_ERR%
)

echo [PS2-STAGE0] Compiling with Ninja (-j %NINJA_JOBS%)...
ninja -C "%PS2_BUILD_DIR%" -j %NINJA_JOBS% %EXTRA_TARGETS% >> "%LOG_DETAIL%" 2>&1
set "BUILD_ERR=%ERRORLEVEL%"

set "END_STAMP=%DATE% %TIME:~0,8%"
if not "%BUILD_ERR%"=="0" (
  echo [FAIL] Ninja build failed (%BUILD_ERR%). Inspect "%LOG_DETAIL%". 1>&2
  >> "%LOG_SUMMARY%" echo End Timestamp   : %END_STAMP%
  >> "%LOG_SUMMARY%" echo Result          : BUILD_FAILED (%BUILD_ERR%)
  >> "%LOG_SESSION%" echo [%END_STAMP%] FAILED PS2 Stage 0 build (%BUILD_ERR%)
  echo.
  echo --- Compiler errors from %LOG_DETAIL% ---
  findstr /I /C:"fatal error:" /C:"error:" "%LOG_DETAIL%"
  exit /b %BUILD_ERR%
)

echo [PASS] PS2 Stage 0 build succeeded!
>> "%LOG_SUMMARY%" echo End Timestamp   : %END_STAMP%
>> "%LOG_SUMMARY%" echo Result          : BUILD_SUCCEEDED (0)
>> "%LOG_SESSION%" echo [%END_STAMP%] SUCCEEDED PS2 Stage 0 build

if exist "%PS2_BUILD_DIR%\pcsx2_ax_harness.exe" (
  echo [PASS] Built harness: %PS2_BUILD_DIR%\pcsx2_ax_harness.exe
)

exit /b 0