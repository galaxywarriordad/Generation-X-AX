# SPDX-FileCopyrightText: Copyright 2026 The Silicon Preservation Society (galaxywarriordad)
# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [string]$ProcessName = "pcsx2_ax_harness",
    [int]$SampleIntervalSeconds = 2,
    [string]$OutputJsonPath = "$PSScriptRoot\..\build_ps2_telemetry_metrics.json",
    [string]$LogDir = "$env:APPDATA\pcsx2_ax\log",
    [string]$StopFilePath = "$PSScriptRoot\..\stop_ps2_telemetry"
)

$ErrorActionPreference = "Continue"

if (!(Test-Path $LogDir)) {
    try { New-Item -ItemType Directory -Force -Path $LogDir | Out-Null } catch {}
}

Write-Host "=======================================================================" -ForegroundColor Cyan
Write-Host " PCSX2_AX ASYNCHRONOUS PIPELINE TELEMETRY DAEMON (PROTOTYPE PHASE)" -ForegroundColor Cyan
Write-Host " Press Ctrl+C or create $StopFilePath to stop." -ForegroundColor Cyan
Write-Host "=======================================================================" -ForegroundColor Cyan

$prevCpuSeconds = $null
$prevSampleTime = $null
$logicalCores = [Environment]::ProcessorCount

while ($true) {
    if (Test-Path $StopFilePath) {
        Write-Host "[STOP] Stop file detected. Exiting PS2 telemetry daemon." -ForegroundColor Yellow
        exit 0
    }

    try {
        $proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -eq $proc) {
            $prevCpuSeconds = $null
            $prevSampleTime = $null
            Write-Host "[WAITING] $ProcessName.exe not currently running..." -ForegroundColor Yellow
        } else {
            $now = Get-Date
            $workingSet = [math]::Round($proc.WorkingSet64 / 1MB, 2)
            $handles = $proc.HandleCount

            $cpuPct = 0.0
            $curCpuSeconds = $proc.TotalProcessorTime.TotalSeconds
            if ($null -ne $prevCpuSeconds -and $null -ne $prevSampleTime) {
                $wallDelta = ($now - $prevSampleTime).TotalSeconds
                if ($wallDelta -gt 0) {
                    $cpuPct = [math]::Round((($curCpuSeconds - $prevCpuSeconds) / ($wallDelta * $logicalCores)) * 100.0, 2)
                }
            }
            $prevCpuSeconds = $curCpuSeconds
            $prevSampleTime = $now

            $prioMap = @{}
            $threadCount = 0
            try {
                foreach ($t in $proc.Threads) {
                    $threadCount++
                    try {
                        $prio = $t.PriorityLevel.ToString()
                        $key = "Prio_$prio"
                        if (-not $prioMap.ContainsKey($key)) { $prioMap[$key] = 0 }
                        $prioMap[$key]++
                    } catch {}
                }
            } catch {}

            $shaderStutters = 0
            $audioUnderruns = 0
            $epochSwitches = 0

            $candidateLogs = @(
                (Join-Path $LogDir "pcsx2_ax_current.log"),
                (Join-Path $LogDir "pcsx2_ax_log.txt")
            ) | Where-Object { Test-Path $_ } | Select-Object -Unique

            foreach ($logFile in $candidateLogs) {
                $stutterMatches = Select-String -Path $logFile -Pattern "Shader compile|Pipeline creation took|MTGS flush" -SimpleMatch -ErrorAction SilentlyContinue
                if ($stutterMatches) { $shaderStutters += $stutterMatches.Count }

                $underrunMatches = Select-String -Path $logFile -Pattern "underrun|EPIPE|buffer empty|ax-dac" -SimpleMatch -ErrorAction SilentlyContinue
                if ($underrunMatches) { $audioUnderruns += $underrunMatches.Count }

                $epochMatches = Select-String -Path $logFile -Pattern "epoch|Ps2ConsoleAdapter|CohHArcadeAdapter" -SimpleMatch -ErrorAction SilentlyContinue
                if ($epochMatches) { $epochSwitches += $epochMatches.Count }
            }

            $timestamp = $now.ToString("yyyy-MM-dd HH:mm:ss.fff")
            $metricObj = [PSCustomObject]@{
                Timestamp        = $timestamp
                ProcessId        = $proc.Id
                WorkingSet_MB    = $workingSet
                ThreadCount      = $threadCount
                HandleCount      = $handles
                ThreadPriorities = $prioMap
                AudioUnderruns   = $audioUnderruns
                ShaderStutters   = $shaderStutters
                EpochEvents      = $epochSwitches
                CpuUsage_Pct     = $cpuPct
                VramPrune_GiB    = 5.00
                VramCeiling_GiB  = 5.50
            }

            $metricJson = $metricObj | ConvertTo-Json -Compress
            [IO.File]::WriteAllText($OutputJsonPath, $metricJson, [Text.Encoding]::UTF8)

            Write-Host "[PS2 TELEMETRY] [$timestamp] PID: $($proc.Id) | CPU: ${cpuPct}% | RAM: ${workingSet}MB | Handles: $handles | Threads: $threadCount | Underruns: $audioUnderruns" -ForegroundColor Green
        }
    } catch {
        Write-Host "[ERROR] PS2 telemetry sampling error: $_" -ForegroundColor Red
    }

    Start-Sleep -Seconds $SampleIntervalSeconds
}
