# Sweeps RangeBias with the physical ballistics on, to find out whether the old
# 1.04 was compensating for the missing lead.
param([int[]]$Seeds = @(1,2,3,4), [int]$Seconds = 150)
$ue   = "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$proj = "C:\Users\besli\Documents\Unreal Projects\PirateSeas\PirateSeas.uproject"
$log  = "C:\Users\besli\Documents\Unreal Projects\PirateSeas\Saved\Logs\PirateSeas.log"
"{0,-12} {1,10} {2,8} {3,11}" -f "rangeBias","broadsides","struck","struck/brd"
foreach ($bias in @("0.98","1.00","1.02","1.04","1.06")) {
  $b = 0; $h = 0
  foreach ($s in $Seeds) {
    & $ue @($proj,"-game","-NullRHI","-unattended","-nosound","-UseFixedTimeStep","-FPS=60","-Ledger=0",
            "-ShipSeed=$s","-WindBearing=120","-WindSpeed=11","-EnemyCount=2",
            "-EnemyX=20000","-EnemyY=8000","-ShipQuitAfter=$Seconds",
            "-ShipInheritVel=1","-ShipLead=1","-ShipRangeBias=$bias") | Out-Null
    $l = Get-Content $log
    $b += ($l | Select-String -Pattern "SHOTLOG broadside").Count
    $h += ($l | Select-String -Pattern "SHOTLOG hit |SHOTLOG rig ").Count
  }
  $r = if ($b -gt 0) { [math]::Round($h / $b, 2) } else { 0 }
  "{0,-12} {1,10} {2,8} {3,11}" -f $bias, $b, $h, $r
}
