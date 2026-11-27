@echo off
setlocal enabledelayedexpansion
title PCSX2_AX Pipeline - Stage 0 (Control / High-Velocity)

chcp 65001 >nul
set "PYTHONIOENCODING=utf-8"

call "%~dp0_ps2_toolchain_env.bat"
if !errorlevel! neq 0 exit /b 1

set "BUILD_DIR=%PROJ_DIR%\build_ps2_stage0"
set "SUMMARY_LOG=%PROJ_DIR%\build_ps2_stage0_summary.log"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

set "OPT_FLAGS=/MD /EHsc /O2 /DNDEBUG /std:c++20 -march=skylake -Qunused-arguments"
set "C_OPT_FLAGS=/MD /O2 /DNDEBUG -march=skylake -Qunused-arguments"

echo =======================================================================
echo  [PS2 STAGE 0 - CONTROL / HIGH-VELOCITY] pcsx2_ax_harness (-j 4)
echo  Source: %PCSX2_AX_SRC%
echo  Build : %BUILD_DIR%
echo =======================================================================

echo Start Timestamp : %DATE% %TIME% > "%SUMMARY_LOG%"
echo Source Dir      : %PCSX2_AX_SRC% >> "%SUMMARY_LOG%"
echo Build Dir       : %BUILD_DIR% >> "%SUMMARY_LOG%"

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
    -DPCSX2_AX_ENABLE_LTO=OFF ^
    -S "%PCSX2_AX_SRC%" -B "%BUILD_DIR%"
if !errorlevel! neq 0 goto :fail

"%NINJA_EXE%" -C "%BUILD_DIR%" -j 4 pcsx2_ax_harness
if !errorlevel! neq 0 goto :fail

echo End Timestamp   : %DATE% %TIME% >> "%SUMMARY_LOG%"
echo Build Result    : PASS >> "%SUMMARY_LOG%"
echo [PASS] Stage 0 completed cleanly in %BUILD_DIR%
exit /b 0

:fail
echo End Timestamp   : %DATE% %TIME% >> "%SUMMARY_LOG%"
echo Build Result    : FAIL >> "%SUMMARY_LOG%"
echo [@=FATAL] Stage 0 build failed. See %SUMMARY_LOG%
exit /b 1