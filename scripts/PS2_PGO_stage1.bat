@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: ============================================================================
:: PS2_PGO_stage1.bat — Stage 1 Instrumented PGO Profile Build (PCSX2_AX)
:: ============================================================================
:: Builds PCSX2_AX with LLVM clang-cl profile generation (-fprofile-generate)
:: outputting raw profile data into PGO_PROF_DIR under GENX_ROOT.
:: ============================================================================

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "GENX_ROOT=%%~fI"

if exist "%SCRIPT_DIR%_ps2_toolchain_env.bat" (
  call "%SCRIPT_DIR%_ps2_toolchain_env.bat"
  if errorlevel 1 exit /b 1
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
echo   PCSX2_AX — STAGE 1 INSTRUMENTED PGO BUILD (%STAMP%)
echo ============================================================================
echo   GENX_ROOT      : %GENX_ROOT%
echo   PGO_BUILD_DIR  : %PGO_BUILD_DIR%
echo   PROF_RAW_DIR   : %PROF_RAW_DIR%
echo   NINJA_JOBS     : %NINJA_JOBS%
echo ============================================================================

>> "%LOG_SESSION%" echo [%STAMP%] Started PS2 PGO Stage 1 (Instrumented Build)

if exist "%PGO_BUILD_DIR%" rmdir /s /q "%PGO_BUILD_DIR%"
if exist "%PROF_RAW_DIR%" rmdir /s /q "%PROF_RAW_DIR%"
mkdir "%PGO_BUILD_DIR%"
mkdir "%PROF_RAW_DIR%"

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
if errorlevel 1 (
  echo [FAIL] Stage 1 CMake configure failed. 1>&2
  exit /b %ERRORLEVEL%
)

ninja -C "%PGO_BUILD_DIR%" -j %NINJA_JOBS%
if errorlevel 1 (
  echo [FAIL] Stage 1 instrumented build failed. 1>&2
  exit /b %ERRORLEVEL%
)

if exist "%PGO_BUILD_DIR%\pcsx2_ax_harness.exe" (
  echo [PS2-PGO1] Running instrumented harness workload to capture profile data...
  "%PGO_BUILD_DIR%\pcsx2_ax_harness.exe"
)

echo [PASS] Stage 1 instrumented build complete. Raw profiles in "%PROF_RAW_DIR%".
exit /b 0