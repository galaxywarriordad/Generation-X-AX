# ============================================================================
# ps2_shader_validate.ps1 — Shader & Pipeline Artifact Validator for PCSX2_AX
# ============================================================================
# Resolves paths relative to the script location (Generation-X-AX) and checks
# staged shader files, Vulkan 1.3 / VRAM budget constants, and build artifacts.
# ============================================================================

[CmdletBinding()]
param(
    [string]$RepoRoot = "",
    [string]$ShaderDir = "",
    [switch]$Strict
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
} else {
    $RepoRoot = (Resolve-Path $RepoRoot).Path
}

if ([string]::IsNullOrWhiteSpace($ShaderDir)) {
    $candidates = @(
        (Join-Path $RepoRoot "engines\pcsx2_ax\shaders"),
        (Join-Path $RepoRoot "engines\pcsx2_ax\core"),
        (Join-Path $RepoRoot "shared")
    )
    foreach ($cand in $candidates) {
        if (Test-Path -Path $cand) {
            $ShaderDir = (Resolve-Path $cand).Path
            break
        }
    }
    if ([string]::IsNullOrWhiteSpace($ShaderDir)) {
        $ShaderDir = Join-Path $RepoRoot "engines\pcsx2_ax"
    }
}

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  PCSX2_AX — SHADER & PIPELINE VALIDATOR" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  RepoRoot  : $RepoRoot"
Write-Host "  ShaderDir : $ShaderDir"
Write-Host "============================================================" -ForegroundColor Cyan

$issues = 0

if (-not (Test-Path -Path $ShaderDir)) {
    Write-Warning "Shader directory '$ShaderDir' does not exist."
    if ($Strict) { exit 1 }
    exit 0
}

$shaderFiles = Get-ChildItem -Path $ShaderDir -Recurse -File -Include *.glsl,*.vert,*.frag,*.comp,*.hlsl,*.spv -ErrorAction SilentlyContinue
Write-Host "Found $($shaderFiles.Count) shader/SPIR-V file(s) under '$ShaderDir'."

foreach ($file in $shaderFiles) {
    $info = Get-Item -LiteralPath $file.FullName
    if ($info.Length -eq 0) {
        Write-Host "[FAIL] Empty shader file: $($info.FullName)" -ForegroundColor Red
        $issues++
        continue
    }

    if ($info.Extension -ieq ".spv") {
        $bytes = [System.IO.File]::ReadAllBytes($info.FullName)
        if ($bytes.Length -lt 20 -or ($bytes.Length % 4) -ne 0) {
            Write-Host "[FAIL] Invalid SPIR-V byte length: $($info.FullName) ($($bytes.Length) bytes)" -ForegroundColor Red
            $issues++
        } else {
            $magic = [System.BitConverter]::ToUInt32($bytes, 0)
            if ($magic -ne 0x07230203) {
                Write-Host "[FAIL] Bad SPIR-V magic 0x$($magic.ToString('X8')) in $($info.FullName)" -ForegroundColor Red
                $issues++
            } else {
                Write-Host "[PASS] Valid SPIR-V header: $($info.Name) ($($bytes.Length) bytes)" -ForegroundColor Green
            }
        }
    } else {
        $text = [System.IO.File]::ReadAllText($info.FullName)
        if ($text -match "TODO|FIXME|placeholder") {
            Write-Host "[WARN] Marker found in $($info.Name)" -ForegroundColor Yellow
            if ($Strict) { $issues++ }
        } else {
            Write-Host "[PASS] Checked text shader: $($info.Name)" -ForegroundColor Green
        }
    }
}

$cmakePath = Join-Path $RepoRoot "engines\pcsx2_ax\CMakeLists.txt"
if (Test-Path -Path $cmakePath) {
    $cmakeText = Get-Content -LiteralPath $cmakePath -Raw
    if ($cmakeText -match "PCSX2_AX_VRAM_CEILING_GIB=5\.50" -and $cmakeText -match "PCSX2_AX_VRAM_PRUNE_GIB=5\.00") {
        Write-Host "[PASS] CMakeLists.txt enforces 5.00 GiB prune / 5.50 GiB VRAM ceiling." -ForegroundColor Green
    } else {
        Write-Host "[WARN] CMakeLists.txt missing expected 5.00/5.50 GiB VRAM guardrails." -ForegroundColor Yellow
        if ($Strict) { $issues++ }
    }
}

if ($issues -gt 0) {
    Write-Host "[FAIL] Validation completed with $issues issue(s)." -ForegroundColor Red
    exit 1
}

Write-Host "[PASS] Shader & pipeline validation completed cleanly." -ForegroundColor Green
exit 0