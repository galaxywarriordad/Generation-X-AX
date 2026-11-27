@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: ============================================================================
:: PS2_PGO_stage2.bat — Stage 2 Merged Profile & Optimized Build (PCSX2_AX)
:: ============================================================================
:: Merges raw .profraw profiles via llvm-profdata into pcsx2_ax.profdata and
:: builds the optimized PCSX2_AX release binary using -fprofile-use.
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

set "PROF_RAW_DIR=%PGO_PROF_DIR%\raw"
set "MERGED_PROF=%PGO_PROF_DIR%\pcsx2_ax.profdata"
set "OPT_BUILD_DIR=%GENX_ROOT%\build_ps2_pgo_use"
set "LOG_SESSION=%GENX_ROOT%\build_pcsx2_ax_session.log"
set "STAMP=%DATE% %TIME:~0,8%"

echo ============================================================================
echo   PCSX2_AX — STAGE 2 MERGED PGO OPTIMIZED BUILD (%STAMP%)
echo ============================================================================
echo   GENX_ROOT      : %GENX_ROOT%
echo   PROF_RAW_DIR   : %PROF_RAW_DIR%
echo   MERGED_PROF    : %MERGED_PROF%
echo   OPT_BUILD_DIR  : %OPT_BUILD_DIR%
echo   NINJA_JOBS     : %NINJA_JOBS%
echo ============================================================================

>> "%LOG_SESSION%" echo [%STAMP%] Started PS2 PGO Stage 2 (Profile Merge + Optimized Build)

set "PROFDATA_EXE=llvm-profdata.exe"
if defined LLVM_BIN if exist "%LLVM_BIN%\llvm-profdata.exe" (
  set "PROFDATA_EXE=%LLVM_BIN%\llvm-profdata.exe"
)

if not exist "%PROF_RAW_DIR%" (
  echo [ERROR] Missing raw profile directory "%PROF_RAW_DIR%". Run PS2_PGO_stage1.bat first. 1>&2
  exit /b 1
)

echo [PS2-PGO2] Merging .profraw profiles with "%PROFDATA_EXE%"...
"%PROFDATA_EXE%" merge -output="%MERGED_PROF%" "%PROF_RAW_DIR%\*.profraw"
if errorlevel 1 (
  echo [FAIL] llvm-profdata merge failed. 1>&2
  exit /b %ERRORLEVEL%
)

if exist "%OPT_BUILD_DIR%" rmdir /s /q "%OPT_BUILD_DIR%"
mkdir "%OPT_BUILD_DIR%"

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

set "PGO_USE_FLAGS=/clang:-fprofile-use=%MERGED_PROF:\=/% /clang:-Wno-profile-instr-unprofiled /clang:-Wno-profile-instr-out-of-date"

cmake -S "%PS2_SRC_DIR%" -B "%OPT_BUILD_DIR%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DPCSX2_AX_PLANNING_ONLY=ON ^
  -DPCSX2_AX_ENABLE_VULKAN_1_3=ON ^
  -DPCSX2_AX_BUILD_HARNESS=ON ^
  -DPCSX2_AX_ENABLE_LTO=ON ^
  -DCMAKE_C_FLAGS="%PGO_USE_FLAGS%" ^
  -DCMAKE_CXX_FLAGS="%PGO_USE_FLAGS%" ^
  -DCMAKE_EXE_LINKER_FLAGS="%PGO_USE_FLAGS%" ^
  %SDK_FLAG% %CC_FLAG% %CXX_FLAG%
if errorlevel 1 (
  echo [FAIL] Stage 2 CMake configure failed. 1>&2
  exit /b %ERRORLEVEL%
)

ninja -C "%OPT_BUILD_DIR%" -j %NINJA_JOBS%
if errorlevel 1 (
  echo [FAIL] Stage 2 optimized build failed. 1>&2
  exit /b %ERRORLEVEL%
)

echo [PASS] Stage 2 optimized PCSX2_AX build complete in "%OPT_BUILD_DIR%".
exit /b 0