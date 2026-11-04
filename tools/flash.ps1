[CmdletBinding()]
param([switch]$MotorPowerDisconnected,[string]$ProbeSerial)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\env.ps1"
$root=Split-Path -Parent $PSScriptRoot
if(-not $MotorPowerDisconnected){throw 'Disconnect all ESC power before flashing; supply -MotorPowerDisconnected only after checking.'}
if($ProbeSerial -and $ProbeSerial -notmatch '^[0-9A-Za-z]+$'){throw 'Invalid probe serial'}
$image=Join-Path $root 'release\CBOARD_MECANUM_FAST.hex'
if((Get-FileHash -LiteralPath "$root\userCode\runtime\config.h").Hash -ne (Get-FileHash -LiteralPath "$root\release\config-used.h").Hash){throw 'Wheel configuration changed; rebuild before flashing'}
function Invoke-Probe {
    param([string[]]$Arguments,[string]$LogFile)
    # Windows PowerShell 5.1 treats native stderr as ErrorRecord. Keep the
    # process exit code authoritative and retain both streams in the log.
    $previous=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        & openocd @Arguments 2>&1 | ForEach-Object { $_.ToString() } | Tee-Object -FilePath $LogFile
        $probeExit=$LASTEXITCODE
    } finally {$ErrorActionPreference=$previous}
    if($probeExit -ne 0){throw "OpenOCD failed ($probeExit); see $LogFile"}
}
$manifest=Import-Csv -LiteralPath "$root\evidence\SHA256.csv"
foreach($file in @('CBOARD_MECANUM_FAST.hex','layout.json')) {
    $entry=@($manifest | Where-Object {$_.File -eq $file})
    if($entry.Count -ne 1 -or (Get-FileHash -LiteralPath "$root\release\$file" -Algorithm SHA256).Hash -ne $entry[0].Hash){throw 'Release hash mismatch; rebuild first'}
}
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$backupDir=Join-Path $root "backups\$stamp"
New-Item -ItemType Directory -Path $backupDir | Out-Null
$backup=(Join-Path $backupDir 'before-flash.bin').Replace('\','/')
$imageTcl=$image.Replace('\','/')
$common=@('-c','adapter driver hla','-c','hla_layout stlink','-c','hla_vid_pid 0x0483 0x3748','-c','transport select hla_swd','-f','target/stm32f4x.cfg','-c','adapter speed 100')
if($ProbeSerial) { $common+=@('-c',"adapter serial $ProbeSerial") }

# Stop on a different chip family or flash size. Die ID alone cannot prove the PCB model.
$check=@'
init
reset halt
mmw 0xe0042008 0x1000 0
set chip [mrw 0xe0042000]
set size [expr {[mrw 0x1fff7a20] >> 16}]
if {[expr {$chip & 0xfff}] != 0x413 || $size != 1024} { echo "Wrong target; no flash write performed"; shutdown error }
'@
$backupScript=$check+"`ndump_image {$backup} 0x08000000 0x100000`nresume`nshutdown`n"
$backupScriptPath=Join-Path $backupDir 'backup.tcl'
Set-Content -LiteralPath $backupScriptPath -Value $backupScript -Encoding ASCII
Invoke-Probe -Arguments ($common+@('-f',$backupScriptPath)) -LogFile (Join-Path $backupDir 'backup.log')
if(-not (Test-Path -LiteralPath $backup) -or (Get-Item -LiteralPath $backup).Length -ne 1048576) {
    throw 'Backup is incomplete. Nothing was programmed.'
}
Get-FileHash -Algorithm SHA256 -LiteralPath $backup | Format-List | Out-File -LiteralPath (Join-Path $backupDir 'backup-sha256.txt')
# ST-Link firmware may omit voltage reporting. When available, reject an implausible VTref.
$log=Get-Content -LiteralPath (Join-Path $backupDir 'backup.log') -Raw
if($log -match '(?i)Target voltage:\s*([0-9.]+)') {
    $voltage=[double]::Parse($Matches[1],[Globalization.CultureInfo]::InvariantCulture)
    if($voltage -lt 2.7 -or $voltage -gt 3.6) { throw "Unexpected target reference voltage $voltage V. Nothing was programmed." }
}
$programScript=$check+"`nprogram {$imageTcl} verify reset exit`n"
$programScriptPath=Join-Path $backupDir 'program.tcl'
Set-Content -LiteralPath $programScriptPath -Value $programScript -Encoding ASCII
Invoke-Probe -Arguments ($common+@('-f',$programScriptPath)) -LogFile (Join-Path $backupDir 'program.log')
$programLog=Get-Content -LiteralPath (Join-Path $backupDir 'program.log') -Raw
if($programLog -notmatch 'Verified OK') { throw 'Expected verify success is absent. Inspect the log before powering motors.' }
Write-Host "Verified firmware programmed. Backup: $backup"
Write-Host 'Flash verified. Now close this window and run 01_Monitor.cmd. No motor operation has been tested.'
