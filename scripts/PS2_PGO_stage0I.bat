@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: ============================================================================
:: PS2_PGO_stage0I.bat — Stage 0I Incremental Delta Rebuild (PCSX2_AX)
:: ============================================================================
:: Standalone incremental delta rebuild script for PCSX2_AX (does NOT invoke
:: PS2_PGO_config.bat). Configures directly if build.ninja is absent or if
:: CMakeCache.txt points to a relocated source path, then runs Ninja (-j 4).
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
      goto :found_0i_llvm
    )
  )
)
:found_0i_llvm
if defined LLVM_BIN (
  if "!LLVM_BIN:~-1!"=="\" set "LLVM_BIN=!LLVM_BIN:~0,-1%"
  set "PATH=!LLVM_BIN!;%PATH%"
)

set "LOG_SUMMARY=%GENX_ROOT%\build_ps2_stage0_summary.log"
set "LOG_DETAIL=%GENX_ROOT%\build_ps2_stage0_incremental.log"
set "LOG_SESSION=%GENX_ROOT%\build_pcsx2_ax_session.log"

set "PLANNING_ONLY=ON"
set "EXTRA_TARGETS="

:parse_0i_args
if "%~1"=="" goto :0i_args_done
if /I "%~1"=="--full" (
  set "PLANNING_ONLY=OFF"
  shift
  goto :parse_0i_args
)
if /I "%~1"=="--planning" (
  set "PLANNING_ONLY=ON"
  shift
  goto :parse_0i_args
)
set "EXTRA_TARGETS=!EXTRA_TARGETS! %~1"
shift
goto :parse_0i_args
:0i_args_done

set "STAMP=%DATE% %TIME:~0,8%"

echo ============================================================================
echo   PCSX2_AX — STAGE 0I INCREMENTAL DELTA REBUILD (%STAMP%)
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

>> "%LOG_SESSION%" echo [%STAMP%] User selected: 0i
>> "%LOG_SESSION%" echo [%STAMP%] Confirmed start: PS2 Stage 0I - Incremental Delta Rebuild

if exist "%PS2_BUILD_DIR%\CMakeCache.txt" (
  findstr /I /C:"%PS2_SRC_DIR:\=/%" "%PS2_BUILD_DIR%\CMakeCache.txt" >nul 2>&1
  if errorlevel 1 (
    echo [PS2-0I] Relocated source path detected; purging stale CMakeCache.txt...
    del /q "%PS2_BUILD_DIR%\CMakeCache.txt"
    if exist "%PS2_BUILD_DIR%\build.ninja" del /q "%PS2_BUILD_DIR%\build.ninja"
  )
)

if not exist "%PS2_BUILD_DIR%" mkdir "%PS2_BUILD_DIR%"

if not exist "%PS2_BUILD_DIR%\build.ninja" (
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
  echo [PS2-0I] Generating build.ninja via CMake...
  cmake %CMAKE_ARGS% >> "%LOG_DETAIL%" 2>&1
  if errorlevel 1 (
    echo [FAIL] Stage 0I CMake configure failed. See "%LOG_DETAIL%". 1>&2
    >> "%LOG_SUMMARY%" echo End Timestamp   : %DATE% %TIME:~0,8%
    >> "%LOG_SUMMARY%" echo Result          : 0I_CONFIG_FAILED (%ERRORLEVEL%)
    >> "%LOG_SESSION%" echo [%DATE% %TIME:~0,8%] FAILED PS2 Stage 0I configure (%ERRORLEVEL%)
    exit /b %ERRORLEVEL%
  )
)

> "%LOG_DETAIL%" echo ninja: Entering directory '%PS2_BUILD_DIR%'
ninja -C "%PS2_BUILD_DIR%" -j %NINJA_JOBS% %EXTRA_TARGETS% >> "%LOG_DETAIL%" 2>&1
set "BUILD_ERR=%ERRORLEVEL%"

set "END_STAMP=%DATE% %TIME:~0,8%"
if not "%BUILD_ERR%"=="0" (
  echo [FAIL] Stage 0I incremental build failed (%BUILD_ERR%). See "%LOG_DETAIL%". 1>&2
  >> "%LOG_SUMMARY%" echo End Timestamp   : %END_STAMP%
  >> "%LOG_SUMMARY%" echo Result          : 0I_BUILD_FAILED (%BUILD_ERR%)
  >> "%LOG_SESSION%" echo [%END_STAMP%] FAILED PS2 Stage 0I build (%BUILD_ERR%)
  echo.
  echo --- Compiler errors from %LOG_DETAIL% ---
  findstr /I /C:"fatal error:" /C:"error:" "%LOG_DETAIL%"
  exit /b %BUILD_ERR%
)

echo [PASS] Stage 0I incremental delta rebuild succeeded!
>> "%LOG_SUMMARY%" echo End Timestamp   : %END_STAMP%
>> "%LOG_SUMMARY%" echo Result          : 0I_BUILD_SUCCEEDED (0)
>> "%LOG_SESSION%" echo [%END_STAMP%] SUCCEEDED PS2 Stage 0I build
exit /b 0