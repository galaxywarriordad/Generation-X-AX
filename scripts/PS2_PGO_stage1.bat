@echo off
setlocal enabledelayedexpansion
title PCSX2_AX Pipeline - Stage 1 (Instrumented Target Generation)

chcp 65001 >nul
set "PYTHONIOENCODING=utf-8"

call "%~dp0_ps2_toolchain_env.bat"
if !errorlevel! neq 0 exit /b 1

set "BUILD_DIR=%PROJ_DIR%\build_ps2_stage1"
set "PGO_RAW_DIR=%PROJ_DIR%\pgo_ps2_data\raw"
set "PROFDATA_OUT=%PROJ_DIR%\pgo_ps2_data\pcsx2_ax.profdata"
set "VERBOSE_LOG=%PROJ_DIR%\build_ps2_stage1_verbose.log"

if exist "%PGO_RAW_DIR%" rmdir /s /q "%PGO_RAW_DIR%" >nul 2>&1
mkdir "%PGO_RAW_DIR%"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

for /f "tokens=*" %%r in ('"%CLANG_CL_EXE%" -print-resource-dir') do set "CLANG_RES_DIR=%%r"
set "CLANG_PROF_LIB_DIR=%CLANG_RES_DIR%\lib\windows"

set "PGO_GEN_LLVM_DIR=%PGO_RAW_DIR:\=/%"
set "OPT_FLAGS=/MD /EHsc /O3 /Ob2 /DNDEBUG /std:c++20 -march=skylake -fprofile-generate=%PGO_GEN_LLVM_DIR% -clang:-Wno-profile-instr-out-of-date -Qunused-arguments"
set "C_OPT_FLAGS=/MD /O3 /Ob2 /DNDEBUG -march=skylake -fprofile-generate=%PGO_GEN_LLVM_DIR% -Qunused-arguments"
set "LINK_FLAGS=/IGNORE:4006 /IGNORE:4221 /libpath:\"%CLANG_PROF_LIB_DIR%\" /WHOLEARCHIVE:clang_rt.profile-x86_64.lib"

echo =======================================================================
echo  [PS2 STAGE 1 - INSTRUMENTATION] Building Instrumented pcsx2_ax_harness
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
    -DCMAKE_EXE_LINKER_FLAGS_RELEASE="%LINK_FLAGS%" ^
    -DPCSX2_AX_PLANNING_ONLY=ON ^
    -DPCSX2_AX_ENABLE_VULKAN_1_3=ON ^
    -DPCSX2_AX_BUILD_HARNESS=ON ^
    -DPCSX2_AX_ENABLE_LTO=ON ^
    -S "%PCSX2_AX_SRC%" -B "%BUILD_DIR%" > "%VERBOSE_LOG%" 2>&1
if !errorlevel! neq 0 goto :fail

"%NINJA_EXE%" -C "%BUILD_DIR%" -j 4 pcsx2_ax_harness >> "%VERBOSE_LOG%" 2>&1
if !errorlevel! neq 0 goto :fail

if exist "%BUILD_DIR%\pcsx2_ax_harness.exe" (
    set "LLVM_PROFILE_FILE=%PGO_RAW_DIR%\ps2_%%p-%%m.profraw"
    "%BUILD_DIR%\pcsx2_ax_harness.exe" --mode ps2 --dry-run >nul 2>&1
    set "LLVM_PROFILE_FILE=%PGO_RAW_DIR%\cohh_%%p-%%m.profraw"
    "%BUILD_DIR%\pcsx2_ax_harness.exe" --mode coh-h --dry-run >nul 2>&1
)

if exist "%LLVM_PROFDATA_EXE%" (
    dir /b "%PGO_RAW_DIR%\*.profraw" >nul 2>&1
    if !errorlevel! equ 0 (
        "%LLVM_PROFDATA_EXE%" merge -output="%PROFDATA_OUT%" "%PGO_RAW_DIR%"\*.profraw
        if !errorlevel! neq 0 goto :fail
        echo [PASS] Merged PGO profile written to %PROFDATA_OUT%
    ) else (
        echo [WARN] No .profraw files produced yet during prototype dry-run.
    )
)

echo [PASS] Stage 1 completed. Log: %VERBOSE_LOG%
exit /b 0

:fail
echo [@=FATAL] Stage 1 failed. Log: %VERBOSE_LOG%
exit /b 1