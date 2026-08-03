# A/B regression check for the mforce_ui engine-stamp guard (dsp, 2026-08-03).
#
# The guard used to compare the exe against EVERY file under engine/, which gave
# a false stale: mforce_ui depends on 59 of the 116 engine sources, so a
# comp-lane edit to e.g. composer.h left the exe correctly un-relinked while the
# guard shouted REBUILD (reported by Wolfie, 2026-08-02). It now keys off
# MSBuild's own read logs for the two targets that produce the exe.
#
# This script proves the discrimination in three directions:
#   control        - nothing newer than the exe        -> both modes: not stale
#   composer.h     - newer, NOT a mforce_ui dependency -> old: STALE, new: not
#   partials.h     - newer, IS a mforce_ui dependency  -> new: STALE (sensitivity kept)
#
# The probe exe is a copy of mforce_ui.exe stamped newer than every source, so
# the file under test is the only variable; it lives in build/ (gitignored) and
# is deleted afterwards. Source CONTENT is never touched -- only LastWriteTime,
# restored exactly, verified by SHA256, so no rebuild is triggered.
#
# Run:  powershell -NoProfile -File tools/stamp_test/ab_stale_guard.ps1

$ErrorActionPreference = 'Stop'

$root   = (Resolve-Path "$PSScriptRoot\..\..").Path
$relDir = Join-Path $root 'build\tools\mforce_ui\Release'
$exe    = Join-Path $relDir 'mforce_ui.exe'
$probe  = Join-Path $relDir 'stamp_probe.exe'
$test   = Join-Path $root 'build\tools\stamp_test\Release\stamp_test.exe'
$notDep = Join-Path $root 'engine\include\mforce\music\composer.h'
$isDep  = Join-Path $root 'engine\include\mforce\source\additive\partials.h'

foreach ($f in @($exe, $test, $notDep, $isDep)) {
    if (-not (Test-Path $f)) { throw "missing: $f (build mforce_ui and stamp_test first)" }
}

# NB: the parameter cannot be called $args -- that is a PowerShell automatic
# variable and splatting it silently passes nothing (stamp_test then exits 2).
function Report($label, $argv) {
    Write-Output "--- $label ---"
    & $test @argv | Select-String -Pattern 'dep set|stale|newer file'
    Write-Output "exit=$LASTEXITCODE"
}

$origNotDep = (Get-Item $notDep).LastWriteTime
$origIsDep  = (Get-Item $isDep).LastWriteTime
$hashNotDep = (Get-FileHash $notDep -Algorithm SHA256).Hash
$hashIsDep  = (Get-FileHash $isDep  -Algorithm SHA256).Hash
$now        = Get-Date

Copy-Item $exe $probe -Force
Set-ItemProperty $probe -Name LastWriteTime -Value $now
Write-Output "probe exe stamped : $now  (newer than every source)"

try {
    Write-Output ""
    Write-Output "### CONTROL: nothing newer than the probe -> expect not stale, both ###"
    Report 'A: OLD (whole-engine scan)' @($probe, '--scan')
    Report 'B: NEW (tlog dep set)'      @($probe)

    Set-ItemProperty $notDep -Name LastWriteTime -Value $now.AddHours(1)
    Write-Output ""
    Write-Output "### composer.h newer, NOT a UI dependency -> expect A STALE (the bug), B not ###"
    Report 'A: OLD (whole-engine scan)' @($probe, '--scan')
    Report 'B: NEW (tlog dep set)'      @($probe)
    Set-ItemProperty $notDep -Name LastWriteTime -Value $origNotDep

    Set-ItemProperty $isDep -Name LastWriteTime -Value $now.AddHours(1)
    Write-Output ""
    Write-Output "### partials.h newer, IS a UI dependency -> expect B STALE (sensitivity kept) ###"
    Report 'B: NEW (tlog dep set)'      @($probe)
}
finally {
    Set-ItemProperty $notDep -Name LastWriteTime -Value $origNotDep
    Set-ItemProperty $isDep  -Name LastWriteTime -Value $origIsDep
    Remove-Item $probe -Force -ErrorAction SilentlyContinue
    $okNotDep = $hashNotDep -eq (Get-FileHash $notDep -Algorithm SHA256).Hash
    $okIsDep  = $hashIsDep  -eq (Get-FileHash $isDep  -Algorithm SHA256).Hash
    Write-Output ""
    Write-Output "restored: composer.h mtime $((Get-Item $notDep).LastWriteTime) content-unchanged=$okNotDep"
    Write-Output "restored: partials.h mtime $((Get-Item $isDep).LastWriteTime) content-unchanged=$okIsDep"
}
