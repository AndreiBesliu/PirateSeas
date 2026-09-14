# Counts gunnery over a fixed engagement, for a set of ballistics settings.
#
# THREE seeds per setting, not one. Gun scatter is the only hazard in the
# project and it is exactly what this measures, so a single pair of runs would
# be reading the scatter and calling it the signal.
param([int[]]$Seeds = @(1,2,3), [int]$Seconds = 150)

$ue   = "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$proj = "C:\Users\besli\Documents\Unreal Projects\PirateSeas\PirateSeas.uproject"
$log  = "C:\Users\besli\Documents\Unreal Projects\PirateSeas\Saved\Logs\PirateSeas.log"

$cases = @(
  @{ name = "old      (no inherit, no lead)"; args = @("-ShipInheritVel=0","-ShipLead=0") },
  @{ name = "inherit only"                  ; args = @("-ShipInheritVel=1","-ShipLead=0") },
  @{ name = "lead only"                     ; args = @("-ShipInheritVel=0","-ShipLead=1") },
  @{ name = "both     (default)"            ; args = @("-ShipInheritVel=1","-ShipLead=1") }
)

"{0,-28} {1,10} {2,7} {3,7} {4,7} {5,9} {6,8}" -f "case","broadsides","balls","struck","hull","struck/brd","lead m"
foreach ($c in $cases) {
  $b = 0; $h = 0; $hull = 0; $shots = 0; $leadSum = 0.0; $leadN = 0
  foreach ($s in $Seeds) {
    $a = @($proj,"-game","-NullRHI","-unattended","-nosound","-UseFixedTimeStep","-FPS=60",
           "-ShipSeed=$s","-WindBearing=120","-WindSpeed=11","-EnemyCount=2",
           "-EnemyX=20000","-EnemyY=8000","-ShipQuitAfter=$Seconds") + $c.args
    & $ue $a | Out-Null
    $lines = Get-Content $log
    $b += ($lines | Select-String -Pattern "SHOTLOG broadside").Count
    # Anything that struck a ship, hull or rig. My first version counted only
    # "SHOTLOG hit " - the HULL line - and reported zero hits for a run that
    # was landing shot in the rigging the whole time, because the guns were
    # ordered high. A metric that cannot see the thing being aimed at is not a
    # metric.
    $shots += ($lines | Select-String -Pattern "SHOTLOG splash|SHOTLOG hit |SHOTLOG rig ").Count
    $h += ($lines | Select-String -Pattern "SHOTLOG hit |SHOTLOG rig ").Count
    $hull += ($lines | Select-String -Pattern "SHOTLOG hit ").Count
    foreach ($m in ($lines | Select-String -Pattern "lead=([0-9.]+)m")) {
      $leadSum += [double]$m.Matches[0].Groups[1].Value; $leadN++
    }
  }
  $rate = if ($b -gt 0) { [math]::Round($h / $b, 2) } else { 0 }
  $lead = if ($leadN -gt 0) { [math]::Round($leadSum / $leadN, 1) } else { 0 }
  "{0,-28} {1,10} {2,7} {3,7} {4,7} {5,9} {6,8}" -f $c.name, $b, $shots, $h, $hull, $rate, $lead
}
