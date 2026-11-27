Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot   = "C:\Users\nick_\Desktop\Generation-X-AX"
$CoreDir    = Join-Path $RepoRoot "engines\pcsx2_ax\core"
$VtlbCpp    = Join-Path $CoreDir "vtlb.cpp"
$Slice1Dir  = "C:\Users\nick_\Desktop\build_pcsx2_ax_slice1_asan_ubsan"
$VtlbInc    = Join-Path $Slice1Dir "vtlb_probe_inc"
$PreludeH   = Join-Path $VtlbInc "vtlb_slice1_prelude.h"
$HarnessCpp = Join-Path $Slice1Dir "vtlb_slice1_harness.cpp"
$VtlbObj    = Join-Path $Slice1Dir "vtlb.obj"
$HarnessExe = Join-Path $Slice1Dir "vtlb_slice1_harness.exe"
$AxSdkDir   = "C:\Users\nick_\Desktop\ax-sdk"
$ProtoInc   = "C:\Users\nick_\Desktop\build_pcsx2_ax_verify\proto_inc"
$TmpPcsx2   = "C:\Users\nick_\Desktop\_tmp_pcsx2"
$FmtIncDir  = Join-Path $TmpPcsx2 "3rdparty\fmt\include"
$clangCl    = "C:\Program Files\LLVM\bin\clang-cl.exe"

if (-not (Test-Path -LiteralPath $VtlbCpp)) {
    throw "Missing source file: $VtlbCpp"
}
if (-not (Test-Path -LiteralPath $HarnessCpp)) {
    throw "Missing harness source file: $HarnessCpp"
}
if (-not (Test-Path -LiteralPath $PreludeH)) {
    throw "Missing prelude header: $PreludeH"
}
if (-not (Test-Path -LiteralPath $clangCl)) {
    throw "Missing clang-cl compiler: $clangCl"
}

$env:ASAN_OPTIONS  = "halt_on_error=1:detect_leaks=0"
$env:UBSAN_OPTIONS = "halt_on_error=1:print_stacktrace=1"

$commonArgs = @(
    "/nologo", "/std:c++20", "/O1", "/W4", "/permissive-",
    "-fsanitize=address,undefined",
    "/clang:-march=skylake",
    "/clang:-Wno-mismatched-tags",
    "/clang:-Wno-macro-redefined",
    "/FI`"$PreludeH`"",
    "/D_CRT_SECURE_NO_WARNINGS", "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN",
    "-I`"$VtlbInc`"",
    "-I`"$CoreDir`"",
    "-I`"$(Join-Path $RepoRoot 'engines\pcsx2_ax')`"",
    "-I`"$FmtIncDir`"",
    "-I`"$TmpPcsx2`"",
    "-I`"$ProtoInc`"",
    "-I`"$(Join-Path $AxSdkDir 'include')`""
)

& $clangCl @commonArgs /c $VtlbCpp "/Fo:$VtlbObj"
if ($LASTEXITCODE -ne 0) {
    throw "vtlb.cpp compile failed with exit code $LASTEXITCODE"
}

& $clangCl @commonArgs $HarnessCpp $VtlbObj "/Fe:$HarnessExe"
if ($LASTEXITCODE -ne 0) {
    throw "vtlb_slice1_harness.cpp compile/link failed with exit code $LASTEXITCODE"
}

& $HarnessExe
if ($LASTEXITCODE -ne 0) {
    throw "vtlb_slice1_harness.exe failed with exit code $LASTEXITCODE"
}