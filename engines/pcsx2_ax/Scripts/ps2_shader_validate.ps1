# SPDX-FileCopyrightText: Copyright 2026 The Silicon Preservation Society (galaxywarriordad)
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [string]$ShaderDir = "$PSScriptRoot\..\engines\pcsx2_ax\shaders",
    [string]$ExternalsDir = "$PSScriptRoot\..\engines\pcsx2_ax\externals"
)

$glslang = "C:\VulkanSDK\1.4.363.0\Bin\glslangValidator.exe"
if (-not (Test-Path -Path $glslang)) {
    $sdkBin = Get-ChildItem -Path "C:\VulkanSDK" -Filter "glslangValidator.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($sdkBin) { $glslang = $sdkBin.FullName }
}

if (-not (Test-Path -Path $glslang)) {
    Write-Host "[ps2_shader_validate] glslangValidator.exe not found under C:\VulkanSDK" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path -Path $ShaderDir)) {
    Write-Host "[ps2_shader_validate] Shader directory not found (Prototype Phase): $ShaderDir" -ForegroundColor Yellow
    exit 0
}

$includeArgs = @()
if (Test-Path -Path $ExternalsDir) {
    $ffxHeader = Get-ChildItem -Path $ExternalsDir -Recurse -Filter "ffx_a.h" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($ffxHeader) {
        $includeArgs += "-I$($ffxHeader.DirectoryName)"
        Write-Host "[ps2_shader_validate] Found FSR headers at: $($ffxHeader.DirectoryName)" -ForegroundColor Cyan
    }
}

$shaderFiles = Get-ChildItem -Path $ShaderDir -Recurse -Filter *.frag -ErrorAction SilentlyContinue | Where-Object {
    ($_.Name -notin @('fidelityfx_fsr.frag', 'opengl_fidelityfx_fsr.frag')) -and
    ($_.Name -notmatch '^opengl_')
}

if (-not $shaderFiles -or $shaderFiles.Count -eq 0) {
    Write-Host "[ps2_shader_validate] No .frag shaders found in $ShaderDir (Prototype Phase)." -ForegroundColor Yellow
    exit 0
}

$errors = 0
$tempSpv = Join-Path $env:TEMP "pcsx2_ax_validate.spv"

foreach ($file in $shaderFiles) {
    $ErrorActionPreference = "Continue"
    $result = & $glslang -V --quiet @includeArgs -DUseUniformBlock=1 --target-env spirv1.3 -o $tempSpv $file.FullName 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ps2_shader_validate] FAILED: $($file.Name)" -ForegroundColor Red
        $result | ForEach-Object { Write-Host "  $_" }
        $errors++
    }
}

if (Test-Path -Path $tempSpv) {
    Remove-Item -Path $tempSpv -Force -ErrorAction SilentlyContinue
}

if ($errors -gt 0) {
    Write-Host "[ps2_shader_validate] $errors PS2 shader(s) failed validation." -ForegroundColor Red
    exit 1
}

Write-Host "[ps2_shader_validate] All $($shaderFiles.Count) PS2 Vulkan fragment shaders validated (SPIR-V 1.3)." -ForegroundColor Green
exit 0
