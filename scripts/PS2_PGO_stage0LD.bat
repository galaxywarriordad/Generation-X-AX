@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: ============================================================================
:: PS2_PGO_stage0LD.bat - Stage 0LD Clean Diagnostics Build (PCSX2_AX)
:: ============================================================================
:: Standalone clean diagnostics build script for PCSX2_AX (does NOT invoke
:: PS2_PGO_config.bat). Purges prior build directory, configures CMake +
:: Ninja directly, and compiles with -k 0 so all diagnostics are captured.
:: ============================================================================

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
for %%I in ("%SCRIPT_DIR%\..") do set "GENX_ROOT=%%~fI"
for %%I in ("%GENX_ROOT%\..") do set "PARENT_DIR=%%~fI"

if exist "%SCRIPT_DIR%\_ps2_toolchain_env.bat" (
  call "%SCRIPT_DIR%\_ps2_toolchain_env.bat"
  if errorlevel 1 exit /b %ERRORLEVEL%
)

if not defined PS2_SRC_DIR set "PS2_SRC_DIR=%GENX_ROOT%\engines\pcsx2_ax"
if not defined PS2_BUILD_DIR set "PS2_BUILD_DIR=%GENX_ROOT%\build_ps2_stage0"
if not defined NINJA_JOBS set "NINJA_JOBS=4"

set "LOG_SUMMARY=%GENX_ROOT%\build_ps2_stage0_summary.log"
set "LOG_DETAIL=%GENX_ROOT%\build_ps2_stage0_triage.log"
set "LOG_SESSION=%GENX_ROOT%\build_pcsx2_ax_session.log"

set "PLANNING_ONLY=ON"
set "EXTRA_TARGETS="

:parse_0ld_args
if "%~1"=="" goto :0ld_args_done
if /I "%~1"=="--full" (
  set "PLANNING_ONLY=OFF"
  shift
  goto :parse_0ld_args
)
if /I "%~1"=="--planning" (
  set "PLANNING_ONLY=ON"
  shift
  goto :parse_0ld_args
)
set "EXTRA_TARGETS=!EXTRA_TARGETS! %~1"
shift
goto :parse_0ld_args
:0ld_args_done

set "STAMP=%DATE% %TIME:~0,8%"

echo ============================================================================
echo   PCSX2_AX - STAGE 0LD CLEAN DIAGNOSTICS (%STAMP%)
echo ============================================================================
echo   GENX_ROOT     : %GENX_ROOT%
echo   PS2_SRC_DIR   : %PS2_SRC_DIR%
echo   PS2_BUILD_DIR : %PS2_BUILD_DIR%
if defined AX_SDK_DIR (
  echo   AX_SDK_DIR    : %AX_SDK_DIR%
) else (
  echo   AX_SDK_DIR    : (not found locally; CMake fallback applies)
)
echo   PLANNING_ONLY : %PLANNING_ONLY%
echo   NINJA_JOBS    : %NINJA_JOBS%
echo ============================================================================

if not exist "%PS2_SRC_DIR%\CMakeLists.txt" (
  echo [ERROR] CMakeLists.txt not found at "%PS2_SRC_DIR%" 1>&2
  exit /b 1
)

where cmake.exe >nul 2>&1
if errorlevel 1 (
  echo [ERROR] cmake.exe not found in PATH or C:\Program Files\CMake\bin 1>&2
  exit /b 1
)

where ninja.exe >nul 2>&1
if errorlevel 1 (
  echo [ERROR] ninja.exe not found in PATH or C:\Program Files\ninja-win 1>&2
  exit /b 1
)

>> "%LOG_SESSION%" echo [%STAMP%] User selected: 0ld
>> "%LOG_SESSION%" echo [%STAMP%] Confirmed start: PS2 Stage 0LD - Clean Diagnostics

> "%LOG_SUMMARY%" echo Start Timestamp : %STAMP%
>> "%LOG_SUMMARY%" echo Mode            : Stage 0LD (Clean Diagnostics)
>> "%LOG_SUMMARY%" echo Source          : %PS2_SRC_DIR%
>> "%LOG_SUMMARY%" echo Build           : %PS2_BUILD_DIR%

if exist "%PS2_BUILD_DIR%" (
  echo [PS2-0LD] Purging previous build directory "%PS2_BUILD_DIR%"...
  if defined AX_SDK_DIR if exist "%AX_SDK_DIR%\tools\clean_all.ps1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%AX_SDK_DIR%\tools\clean_all.ps1" -ProjDir "%PS2_BUILD_DIR%" -Wait >nul 2>&1
  )
  if exist "%PS2_BUILD_DIR%" rmdir /s /q "%PS2_BUILD_DIR%"
)

mkdir "%PS2_BUILD_DIR%"
if errorlevel 1 (
  echo [ERROR] Failed to create build directory "%PS2_BUILD_DIR%" 1>&2
  exit /b 1
)

> "%LOG_DETAIL%" echo Timestamp : %STAMP%
>> "%LOG_DETAIL%" echo Source    : %PS2_SRC_DIR%
>> "%LOG_DETAIL%" echo Build     : %PS2_BUILD_DIR%

where clang-cl.exe >nul 2>&1
if not errorlevel 1 (
  clang-cl.exe --version >> "%LOG_DETAIL%" 2>&1
)

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

echo [PS2-0LD] Configuring CMake...
cmake %CMAKE_ARGS% >> "%LOG_DETAIL%" 2>&1
set "CFG_ERR=%ERRORLEVEL%"
if not "%CFG_ERR%"=="0" (
  echo [FAIL] Stage 0LD CMake configure failed (%CFG_ERR%). See "%LOG_DETAIL%". 1>&2
  >> "%LOG_SUMMARY%" echo Result          : 0LD_CONFIG_FAILED (%CFG_ERR%)
  >> "%LOG_SESSION%" echo [%DATE% %TIME:~0,8%] FAILED PS2 Stage 0LD configure (%CFG_ERR%)
  exit /b %CFG_ERR%
)

echo [PS2-0LD] Compiling with Ninja (-j %NINJA_JOBS% -k 0)...
ninja -C "%PS2_BUILD_DIR%" -j %NINJA_JOBS% -k 0 %EXTRA_TARGETS% >> "%LOG_DETAIL%" 2>&1
set "BUILD_ERR=%ERRORLEVEL%"

set "END_STAMP=%DATE% %TIME:~0,8%"
>> "%LOG_SUMMARY%" echo End Timestamp   : %END_STAMP%
if not "%BUILD_ERR%"=="0" (
  echo [WARN] Stage 0LD completed with compile errors (%BUILD_ERR%). See "%LOG_DETAIL%".
  >> "%LOG_SUMMARY%" echo Result          : 0LD_ERRORS (%BUILD_ERR%)
  >> "%LOG_SESSION%" echo [%END_STAMP%] COMPLETED PS2 Stage 0LD with errors (%BUILD_ERR%)
  echo.
  echo --- Compiler errors from %LOG_DETAIL% ---
  findstr /I /C:"fatal error:" /C:"error:" "%LOG_DETAIL%"
  exit /b %BUILD_ERR%
)

echo [PASS] Stage 0LD clean diagnostics succeeded with 0 errors!
>> "%LOG_SUMMARY%" echo Result          : 0LD_PASSED (0)
>> "%LOG_SESSION%" echo [%END_STAMP%] PASSED PS2 Stage 0LD clean diagnostics
exit /b 0