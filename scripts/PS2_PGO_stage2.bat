@echo off
setlocal enabledelayedexpansion
title PCSX2_AX Pipeline - Stage 2 (PGO + ThinLTO Optimizer)

chcp 65001 >nul
set "PYTHONIOENCODING=utf-8"

call "%~dp0_ps2_toolchain_env.bat"
if !errorlevel! neq 0 exit /b 1

set "BUILD_DIR=%PROJ_DIR%\build_ps2_stage2"
set "PROFDATA_IN=%PROJ_DIR%\pgo_ps2_data\pcsx2_ax.profdata"
set "VERBOSE_LOG=%PROJ_DIR%\build_ps2_stage2_verbose.log"

if not exist "%PROFDATA_IN%" (
    echo [@=FATAL] Missing merged profile: %PROFDATA_IN% — run PS2_PGO_stage1.bat first.
    exit /b 1
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

set "PROFDATA_LLVM=%PROFDATA_IN:\=/%"
set "OPT_FLAGS=/MD /EHsc /O3 /Ob3 /DNDEBUG /std:c++20 -march=skylake -fprofile-use=%PROFDATA_LLVM% -clang:-Wno-profile-instr-unprofiled -clang:-Wno-profile-instr-out-of-date -Qunused-arguments"
set "C_OPT_FLAGS=/MD /O3 /Ob2 /DNDEBUG -march=skylake -fprofile-use=%PROFDATA_LLVM% -clang:-Wno-profile-instr-unprofiled -clang:-Wno-profile-instr-out-of-date -Qunused-arguments"

echo =======================================================================
echo  [PS2 STAGE 2 - OPTIMISATION] Building PGO + ThinLTO pcsx2_ax_harness
echo  Source: %PCSX2_AX_SRC%
echo  Build : %BUILD_DIR%
echo =======================================================================

"%CMAKE_EXE%" -G Ninja -Wno-unused-cli ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER="%CLANG_CL_EXE:\=/%" ^
    -DCMAKE_CXX_COMPILER="%CLANG_CL_EXE:\=/%" ^
    -DCMAKE_LINKER="%LLD_LINK_EXE:\=/%" ^
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL ^
    -DCMAKE_CXX_FLAGS_RELEASE="%OPT_FLAGS%" ^
    -DCMAKE_C_FLAGS_RELEASE="%C_OPT_FLAGS%" ^
    -DPCSX2_AX_PLANNING_ONLY=ON ^
    -DPCSX2_AX_ENABLE_VULKAN_1_3=ON ^
    -DPCSX2_AX_BUILD_HARNESS=ON ^
    -DPCSX2_AX_ENABLE_LTO=ON ^
    -S "%PCSX2_AX_SRC%" -B "%BUILD_DIR%" > "%VERBOSE_LOG%" 2>&1
if !errorlevel! neq 0 goto :fail

"%NINJA_EXE%" -C "%BUILD_DIR%" -j 4 pcsx2_ax_harness >> "%VERBOSE_LOG%" 2>&1
if !errorlevel! neq 0 goto :fail

echo [PASS] Stage 2 PGO + ThinLTO build succeeded: %BUILD_DIR%
exit /b 0

:fail
echo [@=FATAL] Stage 2 failed. Log: %VERBOSE_LOG%
exit /b 1