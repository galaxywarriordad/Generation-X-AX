@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
for %%i in ("%SCRIPT_DIR%..") do set "PROJ_DIR=%%~fi"
if "%PROJ_DIR:~-1%"=="\" set "PROJ_DIR=%PROJ_DIR:~0,-1%"
set "PCSX2_AX_SRC=%PROJ_DIR%\engines\pcsx2_ax"
set "LLVM_PROJ_DIR=%PROJ_DIR:\=/%"

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

for %%D in ("CMAKE_EXE" "NINJA_EXE" "CLANG_CL_EXE" "LLD_LINK_EXE" "LLVM_PROFDATA_EXE" "PROJ_DIR" "PCSX2_AX_SRC" "LLVM_PROJ_DIR") do (
    set "_val=!%%D!"
)

endlocal & (
    set "PROJ_DIR=%PROJ_DIR%"
    set "PCSX2_AX_SRC=%PCSX2_AX_SRC%"
    set "LLVM_PROJ_DIR=%LLVM_PROJ_DIR%"
    set "CMAKE_EXE=%CMAKE_EXE%"
    set "NINJA_EXE=%NINJA_EXE%"
    set "CLANG_CL_EXE=%CLANG_CL_EXE%"
    set "LLD_LINK_EXE=%LLD_LINK_EXE%"
    set "LLVM_PROFDATA_EXE=%LLVM_PROFDATA_EXE%"
    set "PATH=%~dp0;%PATH%"
)
exit /b 0
