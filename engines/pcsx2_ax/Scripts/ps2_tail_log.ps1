# SPDX-FileCopyrightText: Copyright 2026 The Silicon Preservation Society (galaxywarriordad)
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [string]$Mode = "ps2",
    [string]$LogDir = "$env:APPDATA\pcsx2_ax\log",
    [string]$ExePath = "$PSScriptRoot\..\build_ps2_stage0\pcsx2_ax_harness.exe"
)

$ErrorActionPreference = "Continue"

Write-Host "=======================================================================" -ForegroundColor Cyan
Write-Host " PCSX2_AX - SINGLE-WINDOW RUN & LIVE TRIAGE (Mode: $Mode)" -ForegroundColor Cyan
Write-Host " Watching: $LogDir" -ForegroundColor Cyan
Write-Host " Auto-exits when pcsx2_ax_harness.exe finishes." -ForegroundColor Yellow
Write-Host "=======================================================================" -ForegroundColor Cyan

$CandidateLogs = @(
    (Join-Path $LogDir "pcsx2_ax_log.txt"),
    (Join-Path $LogDir "pcsx2_ax_current.log")
)

Stop-Process -Name "pcsx2_ax_harness" -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 250

foreach ($Path in $CandidateLogs) {
    if (Test-Path $Path) {
        try { Remove-Item -Path $Path -Force -ErrorAction Stop }
        catch { Clear-Content -Path $Path -Force -ErrorAction SilentlyContinue }
    }
}

$Ps2Proc = Get-Process -Name "pcsx2_ax_harness" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -eq $Ps2Proc) {
    $ResolvedExe = (Resolve-Path -Path $ExePath -ErrorAction SilentlyContinue).Path
    if ($null -ne $ResolvedExe) {
        Write-Host "[LAUNCH] Starting $ResolvedExe --mode $Mode --dry-run..." -ForegroundColor Green
        Start-Process -FilePath "explorer.exe" -ArgumentList "`"$ResolvedExe`" --mode $Mode --dry-run"
        for ($i = 0; $i -lt 15; $i++) {
            Start-Sleep -Milliseconds 200
            $Ps2Proc = Get-Process -Name "pcsx2_ax_harness" -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($null -ne $Ps2Proc) { break }
        }
    } else {
        Write-Host "[WARN] $ExePath not found yet." -ForegroundColor Yellow
    }
}

$TargetLog = $null
while ($null -eq $TargetLog) {
    foreach ($Path in $CandidateLogs) {
        if (Test-Path $Path) { $TargetLog = $Path; break }
    }
    if ($null -eq $TargetLog) {
        if ($null -ne $Ps2Proc -and $Ps2Proc.HasExited) {
            Write-Host "[EXIT] pcsx2_ax_harness.exe exited (ExitCode: $($Ps2Proc.ExitCode))." -ForegroundColor Green
            exit $Ps2Proc.ExitCode
        }
        Start-Sleep -Milliseconds 150
    }
}

Write-Host "[ACTIVE] Streaming live session from $TargetLog ..." -ForegroundColor Green
$FilterPattern = "epoch|Vulkan|VRAM|5\.00|5\.50|ax-dac|HiBy|Ps2ConsoleAdapter|CohHArcadeAdapter|ERROR|FATAL|underrun"

$FileStream = [System.IO.File]::Open($TargetLog, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
$Reader = New-Object System.IO.StreamReader($FileStream)
$LastSig = ""
$RepeatCount = 0

try {
    while ($true) {
        if ($FileStream.Length -lt $FileStream.Position) {
            $FileStream.Seek(0, [System.IO.SeekOrigin]::Begin) | Out-Null
            $Reader.DiscardBufferedData()
        }

        $Line = $Reader.ReadLine()
        if ($null -ne $Line) {
            if ($Line -match $FilterPattern) {
                $Sig = $Line -replace '^\[\s*\d+\.\d+\]\s*', ''
                if ($Sig -eq $LastSig) {
                    $RepeatCount++
                    continue
                }
                if ($RepeatCount -gt 0) {
                    Write-Host "  ... [suppressed $RepeatCount identical repeats]" -ForegroundColor DarkGray
                    $RepeatCount = 0
                }
                $LastSig = $Sig
                if ($Line -match "ERROR|FATAL|underrun") {
                    Write-Host $Line -ForegroundColor Red
                } elseif ($Line -match "epoch|Vulkan|VRAM|ax-dac|Ps2ConsoleAdapter|CohHArcadeAdapter") {
                    Write-Host $Line -ForegroundColor Green
                } else {
                    Write-Host $Line -ForegroundColor Yellow
                }
            }
        } else {
            $CheckProc = Get-Process -Name "pcsx2_ax_harness" -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($null -eq $CheckProc) {
                Start-Sleep -Milliseconds 200
                while ($null -ne ($FlushLine = $Reader.ReadLine())) {
                    if ($FlushLine -match $FilterPattern) { Write-Host $FlushLine -ForegroundColor Yellow }
                }
                if ($RepeatCount -gt 0) {
                    Write-Host "  ... [suppressed $RepeatCount identical repeats]" -ForegroundColor DarkGray
                }
                Write-Host "[DONE] pcsx2_ax_harness.exe exited." -ForegroundColor Green
                break
            }
            Start-Sleep -Milliseconds 100
        }
    }
} finally {
    $Reader.Close()
    $FileStream.Close()
}
