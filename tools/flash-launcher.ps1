$ErrorActionPreference='Stop'
Write-Host '将通过 ST-Link 烧录 C 型开发板的麦轮快速免SC启动步骤固件。'
Write-Host '先关闭诊断窗口和其他烧录软件，断开所有 C620 的动力电源，只给 C 板供电。'
Write-Host '本固件能驱动电机，必须架稳四轮离地；烧录前会备份板内原有程序。'
$answer=Read-Host '确认上述条件后输入 FLASH，其他输入退出'
if($answer -cne 'FLASH'){Write-Host '已退出，未连接探针。';exit 0}
& "$PSScriptRoot\flash.ps1" -MotorPowerDisconnected
