# ============================================================================
# ps2_telemetry_daemon.ps1 — Out-of-Process Telemetry Sampler for PCSX2_AX
# ============================================================================
# Samples process CPU/working-set/handle metrics and optional ax-sdk telemetry
# without touching real-time audio/emulation threads. Resolves RepoRoot and
# ax-sdk dynamically from %PSScriptRoot%.
# ============================================================================

[CmdletBinding()]
param(
    [string]$RepoRoot = "",
    [string]$ProcessName = "pcsx2_ax_harness",
    [int]$IntervalMs = 500,
    [int]$MaxSamples = 0,
    [string]$OutputJsonPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
} else {
    $RepoRoot = (Resolve-Path $RepoRoot).Path
}

$parentDir = Split-Path -Parent $RepoRoot
$axSdkDir = $env:AX_SDK_DIR
if ([string]::IsNullOrWhiteSpace($axSdkDir)) {
    foreach ($candidate in @(
        (Join-Path $parentDir "ax-sdk"),
        (Join-Path $env:USERPROFILE "Desktop\ax-sdk"),
        (Join-Path $env:USERPROFILE "ax-sdk")
    )) {
        if (Test-Path -Path (Join-Path $candidate "CMakeLists.txt")) {
            $axSdkDir = (Resolve-Path $candidate).Path
            break
        }
    }
}

if ([string]::IsNullOrWhiteSpace($OutputJsonPath)) {
    $OutputJsonPath = Join-Path $RepoRoot "ps2_telemetry_snapshot.json"
}

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  PCSX2_AX — TELEMETRY SAMPLER" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  RepoRoot    : $RepoRoot"
Write-Host "  ProcessName : $ProcessName"
Write-Host "  IntervalMs  : $IntervalMs"
Write-Host "  OutputJson  : $OutputJsonPath"
if (-not [string]::IsNullOrWhiteSpace($axSdkDir)) {
    Write-Host "  AX_SDK_DIR  : $axSdkDir"
}
Write-Host "============================================================" -ForegroundColor Cyan

$samples = New-Object System.Collections.Generic.List[object]
$count = 0

while ($true) {
    if ($MaxSamples -gt 0 -and $count -ge $MaxSamples) {
        break
    }

    $procs = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue
    $now = (Get-Date).ToString("o")

    if ($null -eq $procs -or @($procs).Count -eq 0) {
        $entry = [PSCustomObject]@{
            timestampUtc   = $now
            processName    = $ProcessName
            running        = $false
            pid            = $null
            workingSetMiB  = 0
            privateMemMiB  = 0
            handleCount    = 0
            threads        = 0
        }
    } else {
        $p = @($procs)[0]
        $entry = [PSCustomObject]@{
            timestampUtc   = $now
            processName    = $ProcessName
            running        = $true
            pid            = $p.Id
            workingSetMiB  = [math]::Round($p.WorkingSet64 / 1MB, 2)
            privateMemMiB  = [math]::Round($p.PrivateMemorySize64 / 1MB, 2)
            handleCount    = $p.HandleCount
            threads        = $p.Threads.Count
        }
    }

    $samples.Add($entry) | Out-Null
    $count++

    if ($entry.running) {
        Write-Host ("[{0}] PID={1} WS={2} MiB Priv={3} MiB Handles={4}" -f $entry.timestampUtc, $entry.pid, $entry.workingSetMiB, $entry.privateMemMiB, $entry.handleCount)
    } else {
        Write-Host ("[{0}] {1} not running" -f $entry.timestampUtc, $ProcessName) -ForegroundColor DarkGray
    }

    $samples | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $OutputJsonPath -Encoding UTF8

    if ($MaxSamples -gt 0 -and $count -ge $MaxSamples) {
        break
    }

    Start-Sleep -Milliseconds $IntervalMs
}