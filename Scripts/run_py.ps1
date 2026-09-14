# Runs an Unreal python script and REFUSES to be quiet about failure.
#
# Written after four wasted runs: lighting.py was raising AttributeError on its
# first line of real work, the exception was piped to Out-Null, and I read the
# GAME log afterwards and concluded "the level did not change". It had not
# changed because the script had not run. A runner that hides stderr is not a
# runner, it is a way of agreeing with yourself.
#
# The verdict is read from the LOG FILE, not from the pipeline: `2>&1` on a
# native exe under Windows PowerShell 5.1 wraps every line in an ErrorRecord,
# which broke the first version of this file - it printed "ok" and matched
# nothing. And the editor exits 1 in this project regardless (an unrelated
# water-collision-profile complaint), so the exit code proves nothing either.
param(
  [Parameter(Mandatory=$true)][string]$Script,
  [string]$Tag = ""
)
# Where things are is DERIVED, never typed. This file used to carry the author's
# own absolute paths, which worked perfectly on the machine it was written on and
# would have driven the wrong copy of the project on any other: measure.yml calls
# it from a self-hosted runner that checks out to C:\actions-runner\_work\..., so
# the CI job would have measured whatever happened to be in the author's home
# directory - and reported it as a measurement of the commit under test.
$root = Split-Path -Parent $PSScriptRoot
$engine = if ($env:UE) { $env:UE } else { "C:\Program Files\Epic Games\UE_5.7" }
$ue   = Join-Path $engine "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$proj = Join-Path $root "PirateSeas.uproject"
$log  = Join-Path $root "Saved\Logs\PirateSeas.log"
$path = (Join-Path $PSScriptRoot $Script) -replace '\\', '/'

# A missing prerequisite is a failure, said out loud. Silence here would send the
# editor a path it cannot open and leave the verdict to be read off a stale log
# from a previous run - which is the same lie this file was written to stop.
foreach ($p in @($ue, $proj, $path)) {
  if (-not (Test-Path $p)) {
    Write-Host "=== $Script CANNOT RUN: nothing at $p ===" -ForegroundColor Red
    exit 2
  }
}

& $ue $proj -run=pythonscript -script="$path" -unattended -nosound | Out-Null

$lines = Get-Content $log
if ($Tag -ne "") {
  $lines | Select-String -Pattern $Tag | ForEach-Object { ($_.Line -split 'LogPython: ')[-1] }
}
$bad = $lines | Select-String -Pattern "Python script executed with errors|LogPython: Error:"
if ($bad) {
  Write-Host "=== $Script FAILED ===" -ForegroundColor Red
  $lines | Select-String -Pattern "LogPython: Error:" | Select-Object -Last 10 |
    ForEach-Object { ($_.Line -split 'LogPython: ')[-1] }
  exit 1
}
Write-Host "--> $Script ok"
exit 0
