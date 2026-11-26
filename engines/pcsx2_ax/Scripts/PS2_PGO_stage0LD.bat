@echo off
setlocal enabledelayedexpansion
title PCSX2_AX Pipeline - Stage 0LD (Lean Diagnostics)

chcp 65001 >nul
set "PYTHONIOENCODING=utf-8"

call "%~dp0_ps2_toolchain_env.bat"
if !errorlevel! neq 0 exit /b 1

set "BUILD_DIR=%PROJ_DIR%\build_ps2_stage0"
set "TRIAGE_LOG=%PROJ_DIR%\build_ps2_stage0_triage.log"

echo =======================================================================
echo  [PS2 STAGE 0LD - LEAN DIAGNOSTICS] Clean Cold Build (-j 4)
echo =======================================================================

taskkill /F /IM pcsx2_ax_harness.exe /T >nul 2>&1
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%" >nul 2>&1
mkdir "%BUILD_DIR%"

echo Timestamp : %DATE% %TIME% > "%TRIAGE_LOG%"
"%CLANG_CL_EXE%" --version >> "%TRIAGE_LOG%" 2>&1

set "OPT_FLAGS=/MD /EHsc /O2 /DNDEBUG /std:c++20 -march=skylake -Qunused-arguments"
set "C_OPT_FLAGS=/MD /O2 /DNDEBUG -march=skylake -Qunused-arguments"

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
    -S "%PCSX2_AX_SRC%" -B "%BUILD_DIR%" >> "%TRIAGE_LOG%" 2>&1
if !errorlevel! neq 0 goto :fail

"%NINJA_EXE%" -C "%BUILD_DIR%" -j 4 pcsx2_ax_harness >> "%TRIAGE_LOG%" 2>&1
if !errorlevel! neq 0 goto :fail

echo [PASS] Stage 0LD completed cleanly. Log: %TRIAGE_LOG%
exit /b 0

:fail
echo [@=FATAL] Stage 0LD failed. Diagnostics: %TRIAGE_LOG%
type "%TRIAGE_LOG%"
exit /b 1
