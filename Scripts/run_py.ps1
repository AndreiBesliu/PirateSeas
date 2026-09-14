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
$ue   = "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$proj = "C:\Users\besli\Documents\Unreal Projects\PirateSeas\PirateSeas.uproject"
$log  = "C:\Users\besli\Documents\Unreal Projects\PirateSeas\Saved\Logs\PirateSeas.log"
$path = "C:/Users/besli/Documents/Unreal Projects/PirateSeas/Scripts/$Script"

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
