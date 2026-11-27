@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: ============================================================================
:: PS2_PGO_stage1.bat - [1] Instrumentation Build & Profile Capture (PCSX2_AX)
:: ============================================================================
:: Builds PCSX2_AX with LLVM clang-cl instrumentation (-fprofile-generate)
:: and runs the harness workload to collect raw .profraw profile data.
:: ============================================================================

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
for %%I in ("%SCRIPT_DIR%\..") do set "GENX_ROOT=%%~fI"

if exist "%SCRIPT_DIR%\_ps2_toolchain_env.bat" (
  call "%SCRIPT_DIR%\_ps2_toolchain_env.bat"
  if errorlevel 1 exit /b %ERRORLEVEL%
) else (
  if not defined PS2_SRC_DIR set "PS2_SRC_DIR=%GENX_ROOT%\engines\pcsx2_ax"
  if not defined PGO_PROF_DIR set "PGO_PROF_DIR=%GENX_ROOT%\build_ps2_pgo_prof"
  if not defined NINJA_JOBS set "NINJA_JOBS=4"
)

set "PGO_BUILD_DIR=%GENX_ROOT%\build_ps2_pgo_gen"
set "PROF_RAW_DIR=%PGO_PROF_DIR%\raw"
set "LOG_SESSION=%GENX_ROOT%\build_pcsx2_ax_session.log"
set "STAMP=%DATE% %TIME:~0,8%"

echo ============================================================================
echo   PCSX2_AX - [1] INSTRUMENTATION BUILD (%STAMP%)
echo ============================================================================
echo   GENX_ROOT      : %GENX_ROOT%
echo   PGO_BUILD_DIR  : %PGO_BUILD_DIR%
echo   PROF_RAW_DIR   : %PROF_RAW_DIR%
echo   NINJA_JOBS     : %NINJA_JOBS%
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

>> "%LOG_SESSION%" echo [%STAMP%] Started PS2 [1] Instrumentation Build

if exist "%PGO_BUILD_DIR%" rmdir /s /q "%PGO_BUILD_DIR%"
if exist "%PROF_RAW_DIR%" rmdir /s /q "%PROF_RAW_DIR%"
mkdir "%PGO_BUILD_DIR%"
if errorlevel 1 (
  echo [ERROR] Failed to create "%PGO_BUILD_DIR%" 1>&2
  exit /b 1
)
mkdir "%PROF_RAW_DIR%"
if errorlevel 1 (
  echo [ERROR] Failed to create "%PROF_RAW_DIR%" 1>&2
  exit /b 1
)

set "LLVM_PROFILE_FILE=%PROF_RAW_DIR%\pcsx2_ax_stage1_%%p.profraw"

set "SDK_FLAG="
if defined AX_SDK_DIR if exist "%AX_SDK_DIR%\CMakeLists.txt" (
  set "AX_SDK_CMAKE=%AX_SDK_DIR:\=/%"
  set "SDK_FLAG=-DAX_SDK_DIR="!AX_SDK_CMAKE!""
)

set "CC_FLAG="
set "CXX_FLAG="
if defined LLVM_BIN if exist "%LLVM_BIN%\clang-cl.exe" (
  set "LLVM_CMAKE=%LLVM_BIN:\=/%"
  set "CC_FLAG=-DCMAKE_C_COMPILER="!LLVM_CMAKE!/clang-cl.exe""
  set "CXX_FLAG=-DCMAKE_CXX_COMPILER="!LLVM_CMAKE!/clang-cl.exe""
)

set "PGO_GEN_FLAGS=/clang:-fprofile-generate=%PROF_RAW_DIR:\=/%"

cmake -S "%PS2_SRC_DIR%" -B "%PGO_BUILD_DIR%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DPCSX2_AX_PLANNING_ONLY=ON ^
  -DPCSX2_AX_ENABLE_VULKAN_1_3=ON ^
  -DPCSX2_AX_BUILD_HARNESS=ON ^
  -DCMAKE_C_FLAGS="%PGO_GEN_FLAGS%" ^
  -DCMAKE_CXX_FLAGS="%PGO_GEN_FLAGS%" ^
  -DCMAKE_EXE_LINKER_FLAGS="%PGO_GEN_FLAGS%" ^
  %SDK_FLAG% %CC_FLAG% %CXX_FLAG%
set "CFG_ERR=%ERRORLEVEL%"
if not "%CFG_ERR%"=="0" (
  echo [FAIL] [1] Instrumentation CMake configure failed (%CFG_ERR%). 1>&2
  >> "%LOG_SESSION%" echo [%DATE% %TIME:~0,8%] FAILED PS2 [1] configure (%CFG_ERR%)
  exit /b %CFG_ERR%
)

ninja -C "%PGO_BUILD_DIR%" -j %NINJA_JOBS%
set "BUILD_ERR=%ERRORLEVEL%"
if not "%BUILD_ERR%"=="0" (
  echo [FAIL] [1] Instrumentation build failed (%BUILD_ERR%). 1>&2
  >> "%LOG_SESSION%" echo [%DATE% %TIME:~0,8%] FAILED PS2 [1] build (%BUILD_ERR%)
  exit /b %BUILD_ERR%
)

if exist "%PGO_BUILD_DIR%\pcsx2_ax_harness.exe" (
  echo [PS2-PGO1] Running instrumented harness workload to capture profile data...
  "%PGO_BUILD_DIR%\pcsx2_ax_harness.exe"
  if errorlevel 1 (
    echo [WARN] Instrumented harness returned non-zero exit code %ERRORLEVEL%. 1>&2
  )
)

echo [PASS] [1] Instrumentation build complete. Raw profiles in "%PROF_RAW_DIR%".
>> "%LOG_SESSION%" echo [%DATE% %TIME:~0,8%] SUCCEEDED PS2 [1] instrumentation build
exit /b 0