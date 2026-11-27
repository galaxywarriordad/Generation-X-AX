@echo off
setlocal enabledelayedexpansion

set "THIS_SCRIPT_DIR=%~dp0"
if "%THIS_SCRIPT_DIR:~-1%"=="\" set "THIS_SCRIPT_DIR=%THIS_SCRIPT_DIR:~0,-1%"

set "RESOLVED_ROOT="
for %%C in (
    "%THIS_SCRIPT_DIR%\.."
    "%THIS_SCRIPT_DIR%\..\.."
    "%THIS_SCRIPT_DIR%\..\..\.."
    "C:\Users\nick_\Generation-X-AX"
) do (
    if not defined RESOLVED_ROOT (
        for %%F in ("%%~fC") do (
            if exist "%%~fF\engines\pcsx2_ax\CMakeLists.txt" (
                set "RESOLVED_ROOT=%%~fF"
            )
        )
    )
)

if not defined RESOLVED_ROOT (
    echo [@=FATAL] Could not locate repository root containing engines\pcsx2_ax\CMakeLists.txt.
    exit /b 1
)
if "%RESOLVED_ROOT:~-1%"=="\" set "RESOLVED_ROOT=%RESOLVED_ROOT:~0,-1%"

set "RESOLVED_SRC=%RESOLVED_ROOT%\engines\pcsx2_ax"
set "RESOLVED_LLVM_ROOT=%RESOLVED_ROOT:\=/%"

if not defined VSCMD_VER (
    if exist "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
    )
)

set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

set "NINJA_EXE=C:\Users\nick_\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe\ninja.exe"
if not exist "%NINJA_EXE%" set "NINJA_EXE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

set "CLANG_CL_EXE=C:\Program Files\LLVM\bin\clang-cl.exe"
if not exist "%CLANG_CL_EXE%" set "CLANG_CL_EXE=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe"

set "LLD_LINK_EXE=C:\Program Files\LLVM\bin\lld-link.exe"
if not exist "%LLD_LINK_EXE%" set "LLD_LINK_EXE=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\lld-link.exe"

set "LLVM_PROFDATA_EXE=C:\Program Files\LLVM\bin\llvm-profdata.exe"
if not exist "%LLVM_PROFDATA_EXE%" set "LLVM_PROFDATA_EXE=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\llvm-profdata.exe"

for %%P in ("%CMAKE_EXE%" "%NINJA_EXE%" "%CLANG_CL_EXE%" "%LLD_LINK_EXE%") do (
    if not exist "%%~P" (
        echo [@=FATAL] Missing required tool: %%~P
        exit /b 1
    )
)

endlocal & (
    set "PROJ_DIR=%RESOLVED_ROOT%"
    set "PCSX2_AX_SRC=%RESOLVED_SRC%"
    set "LLVM_PROJ_DIR=%RESOLVED_LLVM_ROOT%"
    set "CMAKE_EXE=%CMAKE_EXE%"
    set "NINJA_EXE=%NINJA_EXE%"
    set "CLANG_CL_EXE=%CLANG_CL_EXE%"
    set "LLD_LINK_EXE=%LLD_LINK_EXE%"
    set "LLVM_PROFDATA_EXE=%LLVM_PROFDATA_EXE%"
    set "PATH=%~dp0;%PATH%"
)
exit /b 0