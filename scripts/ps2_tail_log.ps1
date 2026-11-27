# ============================================================================
# ps2_tail_log.ps1 — Live Log & Error Inspector for PCSX2_AX Builds
# ============================================================================
# Resolves build/session logs under RepoRoot (defaulting to Generation-X-AX)
# and tails or filters compiler/CMake diagnostics.
# ============================================================================

[CmdletBinding()]
param(
    [string]$RepoRoot = "",
    [string]$LogPath = "",
    [int]$Lines = 60,
    [switch]$Follow,
    [switch]$ErrorsOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
} else {
    $RepoRoot = (Resolve-Path $RepoRoot).Path
}

if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $candidates = @(
        (Join-Path $RepoRoot "build_ps2_stage0_incremental.log"),
        (Join-Path $RepoRoot "build_ps2_stage0_triage.log"),
        (Join-Path $RepoRoot "build_pcsx2_ax_session.log"),
        (Join-Path $RepoRoot "build_ps2_stage0_summary.log")
    )
    foreach ($cand in $candidates) {
        if (Test-Path -Path $cand) {
            $LogPath = (Resolve-Path $cand).Path
            break
        }
    }
}

if ([string]::IsNullOrWhiteSpace($LogPath) -or -not (Test-Path -Path $LogPath)) {
    Write-Error "No PCSX2_AX log file found under '$RepoRoot'."
    exit 1
}

Write-Host "Inspecting log: $LogPath" -ForegroundColor Cyan

if ($ErrorsOnly) {
    $matches = Select-String -Path $LogPath -Pattern "fatal error:|error:|FAILED:|\[FAIL\]|\[WARN\]" -CaseSensitive:$false
    if ($null -eq $matches -or @($matches).Count -eq 0) {
        Write-Host "No error/warning lines found in $LogPath." -ForegroundColor Green
    } else {
        foreach ($m in $matches) {
            Write-Host ("{0}:{1}: {2}" -f $m.Path, $m.LineNumber, $m.Line) -ForegroundColor Yellow
        }
    }
    exit 0
}

if ($Follow) {
    Get-Content -Path $LogPath -Tail $Lines -Wait
} else {
    Get-Content -Path $LogPath -Tail $Lines
}