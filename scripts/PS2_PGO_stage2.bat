@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: ============================================================================
:: PS2_PGO_stage2.bat - [2] Optimisation Build & Profile Merge (PCSX2_AX)
:: ============================================================================
:: Merges raw .profraw profiles via llvm-profdata into pcsx2_ax.profdata and
:: builds the PGO-optimised PCSX2_AX release binary with -fprofile-use.
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

set "PROF_RAW_DIR=%PGO_PROF_DIR%\raw"
set "MERGED_PROF=%PGO_PROF_DIR%\pcsx2_ax.profdata"
set "OPT_BUILD_DIR=%GENX_ROOT%\build_ps2_pgo_use"
set "LOG_SESSION=%GENX_ROOT%\build_pcsx2_ax_session.log"
set "STAMP=%DATE% %TIME:~0,8%"

echo ============================================================================
echo   PCSX2_AX - [2] OPTIMISATION BUILD (%STAMP%)
echo ============================================================================
echo   GENX_ROOT      : %GENX_ROOT%
echo   PROF_RAW_DIR   : %PROF_RAW_DIR%
echo   MERGED_PROF    : %MERGED_PROF%
echo   OPT_BUILD_DIR  : %OPT_BUILD_DIR%
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

>> "%LOG_SESSION%" echo [%STAMP%] Started PS2 [2] Optimisation Build

set "PROFDATA_EXE=llvm-profdata.exe"
if defined LLVM_BIN if exist "%LLVM_BIN%\llvm-profdata.exe" (
  set "PROFDATA_EXE=%LLVM_BIN%\llvm-profdata.exe"
)

if not exist "%PROF_RAW_DIR%" (
  echo [ERROR] Missing raw profile directory "%PROF_RAW_DIR%". Run [1] PS2_PGO_stage1.bat first. 1>&2
  exit /b 1
)

echo [PS2-PGO2] Merging .profraw profiles with "%PROFDATA_EXE%"...
"%PROFDATA_EXE%" merge -output="%MERGED_PROF%" "%PROF_RAW_DIR%\*.profraw"
set "MERGE_ERR=%ERRORLEVEL%"
if not "%MERGE_ERR%"=="0" (
  echo [FAIL] [2] llvm-profdata merge failed (%MERGE_ERR%). 1>&2
  >> "%LOG_SESSION%" echo [%DATE% %TIME:~0,8%] FAILED PS2 [2] profile merge (%MERGE_ERR%)
  exit /b %MERGE_ERR%
)

if exist "%OPT_BUILD_DIR%" rmdir /s /q "%OPT_BUILD_DIR%"
mkdir "%OPT_BUILD_DIR%"
if errorlevel 1 (
  echo [ERROR] Failed to create "%OPT_BUILD_DIR%" 1>&2
  exit /b 1
)

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
set "CFG_ERR=%ERRORLEVEL%"
if not "%CFG_ERR%"=="0" (
  echo [FAIL] [2] Optimisation CMake configure failed (%CFG_ERR%). 1>&2
  >> "%LOG_SESSION%" echo [%DATE% %TIME:~0,8%] FAILED PS2 [2] configure (%CFG_ERR%)
  exit /b %CFG_ERR%
)

ninja -C "%OPT_BUILD_DIR%" -j %NINJA_JOBS%
set "BUILD_ERR=%ERRORLEVEL%"
if not "%BUILD_ERR%"=="0" (
  echo [FAIL] [2] Optimisation build failed (%BUILD_ERR%). 1>&2
  >> "%LOG_SESSION%" echo [%DATE% %TIME:~0,8%] FAILED PS2 [2] build (%BUILD_ERR%)
  exit /b %BUILD_ERR%
)

echo [PASS] [2] Optimisation build complete in "%OPT_BUILD_DIR%".
>> "%LOG_SESSION%" echo [%DATE% %TIME:~0,8%] SUCCEEDED PS2 [2] optimisation build
exit /b 0