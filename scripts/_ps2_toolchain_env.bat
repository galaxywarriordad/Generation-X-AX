@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: ============================================================================
:: _ps2_toolchain_env.bat — Shared Environment Resolver for PCSX2_AX Scripts
:: ============================================================================
:: Resolves GENX_ROOT from %~dp0 (supporting C:\Users\nick_\Desktop\Generation-X-AX
:: or any other moved directory), locates ax-sdk and LLVM clang-cl/ninja/cmake,
:: and exports environment variables to the calling batch script.
:: ============================================================================

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "GENX_ROOT=%%~fI"
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
  if "!LLVM_BIN:~-1!"=="\" set "LLVM_BIN=!LLVM_BIN:~0,-1!"
  set "PATH=!LLVM_BIN!;%PATH%"
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
  echo [PS2-ENV] AX_SDK_DIR    = ^(not found locally; CMake fallback active^)
)
if defined LLVM_BIN (
  echo [PS2-ENV] LLVM_BIN      = %LLVM_BIN%
) else (
  echo [PS2-ENV] LLVM_BIN      = ^(using PATH^)
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
  set "VS_DEV_CMD=%VS_DEV_CMD%"
  set "PATH=%PATH%"
)
exit /b 0