@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: ============================================================================
:: _ps2_toolchain_env.bat - Shared Environment Resolver for PCSX2_AX Scripts
:: ============================================================================
:: Resolves GENX_ROOT from %~dp0, locates ax-sdk, LLVM clang-cl, Ninja, and
:: CMake, and exports resolved environment variables to the caller.
:: ============================================================================

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
for %%I in ("%SCRIPT_DIR%\..") do set "GENX_ROOT=%%~fI"
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
  ) else if exist "%GENX_ROOT%\ax-sdk\CMakeLists.txt" (
    set "AX_SDK_DIR=%GENX_ROOT%\ax-sdk"
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
      goto :found_llvm_bin
    )
  )
)
:found_llvm_bin
if defined LLVM_BIN (
  if "!LLVM_BIN:~-1!"=="\" set "LLVM_BIN=!LLVM_BIN:~0,-1%"
  set "PATH=!LLVM_BIN!;%PATH%"
)

if not defined NINJA_BIN (
  if exist "C:\Program Files\ninja-win\ninja.exe" (
    set "NINJA_BIN=C:\Program Files\ninja-win"
  ) else if exist "%ProgramFiles%\ninja-win\ninja.exe" (
    set "NINJA_BIN=%ProgramFiles%\ninja-win"
  ) else (
    for /f "delims=" %%N in ('where ninja.exe 2^>nul') do (
      set "NINJA_BIN=%%~dpN"
      goto :found_ninja_bin
    )
  )
)
:found_ninja_bin
if defined NINJA_BIN (
  if "!NINJA_BIN:~-1!"=="\" set "NINJA_BIN=!NINJA_BIN:~0,-1%"
  set "PATH=!NINJA_BIN!;%PATH%"
)

if not defined CMAKE_BIN (
  if exist "C:\Program Files\CMake\bin\cmake.exe" (
    set "CMAKE_BIN=C:\Program Files\CMake\bin"
  ) else if exist "%ProgramFiles%\CMake\bin\cmake.exe" (
    set "CMAKE_BIN=%ProgramFiles%\CMake\bin"
  ) else (
    for /f "delims=" %%M in ('where cmake.exe 2^>nul') do (
      set "CMAKE_BIN=%%~dpM"
      goto :found_cmake_bin
    )
  )
)
:found_cmake_bin
if defined CMAKE_BIN (
  if "!CMAKE_BIN:~-1!"=="\" set "CMAKE_BIN=!CMAKE_BIN:~0,-1%"
  set "PATH=!CMAKE_BIN!;%PATH%"
)

if not defined VS_DEV_CMD (
  if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
  ) else if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
  ) else if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" (
    set "VS_DEV_CMD=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
  )
)

echo [PS2-ENV] GENX_ROOT     = %GENX_ROOT%
echo [PS2-ENV] PARENT_DIR    = %PARENT_DIR%
echo [PS2-ENV] PS2_SRC_DIR   = %PS2_SRC_DIR%
echo [PS2-ENV] PS2_BUILD_DIR = %PS2_BUILD_DIR%
echo [PS2-ENV] PGO_PROF_DIR  = %PGO_PROF_DIR%
echo [PS2-ENV] NINJA_JOBS    = %NINJA_JOBS%
if defined AX_SDK_DIR (
  echo [PS2-ENV] AX_SDK_DIR    = %AX_SDK_DIR%
) else (
  echo [PS2-ENV] AX_SDK_DIR    = (not found locally; CMake fallback active)
)
if defined LLVM_BIN (
  echo [PS2-ENV] LLVM_BIN      = %LLVM_BIN%
) else (
  echo [PS2-ENV] LLVM_BIN      = (using PATH)
)
if defined NINJA_BIN (
  echo [PS2-ENV] NINJA_BIN     = %NINJA_BIN%
)
if defined CMAKE_BIN (
  echo [PS2-ENV] CMAKE_BIN     = %CMAKE_BIN%
)

endlocal & (
  set "GENX_ROOT=%GENX_ROOT%"
  set "PARENT_DIR=%PARENT_DIR%"
  set "PS2_SRC_DIR=%PS2_SRC_DIR%"
  set "PS2_BUILD_DIR=%PS2_BUILD_DIR%"
  set "PGO_PROF_DIR=%PGO_PROF_DIR%"
  set "NINJA_JOBS=%NINJA_JOBS%"
  set "AX_SDK_DIR=%AX_SDK_DIR%"
  set "LLVM_BIN=%LLVM_BIN%"
  set "NINJA_BIN=%NINJA_BIN%"
  set "CMAKE_BIN=%CMAKE_BIN%"
  set "VS_DEV_CMD=%VS_DEV_CMD%"
  set "PATH=%PATH%"
)
exit /b 0