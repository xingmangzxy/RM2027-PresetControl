$ErrorActionPreference='Stop'
. "$PSScriptRoot\env.ps1"
$root=Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Path "$root\build","$root\release","$root\evidence" -Force | Out-Null
$inc=@('-I',"$root\userCode\runtime",'-I',"$root\userCode\devices\Inc",'-I',"$root\userCode\tasks\Inc")
& gcc -std=c11 -O2 -Wall -Wextra -Werror @inc -c "$root\userCode\runtime\control.c" -o "$root\build\runtime-host.o"
if($LASTEXITCODE -ne 0){throw 'Runtime host compile failed'}
& g++ -std=c++17 -O2 -Wall -Wextra -Werror @inc "$root\tests\test_framework.cpp" "$root\build\runtime-host.o" "$root\userCode\tasks\Src\ControlTask.cpp" "$root\userCode\tasks\Src\ChassisTask.cpp" "$root\userCode\tasks\Src\UserTask.cpp" "$root\userCode\devices\Src\Motor.cpp" "$root\userCode\devices\Src\RemoteControl.cpp" -o "$root\build\control-tests.exe"
if($LASTEXITCODE -ne 0){throw 'Framework host compile failed'}
& "$root\build\control-tests.exe" | Tee-Object -FilePath "$root\evidence\control-tests.txt"
if($LASTEXITCODE -ne 0){throw 'Framework tests failed'}
& gcc -std=c11 -O2 -Wall -Wextra -Werror -I "$root\userCode\diagnostic" "$root\userCode\diagnostic\RemoteDiagnostic.c" "$root\tests\test_diagnostic.c" -o "$root\build\host-tests.exe"
if($LASTEXITCODE -ne 0){throw 'Host test compile failed'}
& "$root\build\host-tests.exe" | Tee-Object -FilePath "$root\evidence\host-tests.txt"
if($LASTEXITCODE -ne 0){throw 'Host tests failed'}
& cmake -S $root -B "$root\build" -G Ninja 2>&1 | Tee-Object -FilePath "$root\evidence\configure.txt"
if($LASTEXITCODE -ne 0){throw 'Configure failed'}
& cmake --build "$root\build" --parallel 6 2>&1 | Tee-Object -FilePath "$root\evidence\build.txt"
if($LASTEXITCODE -ne 0){throw 'Build failed'}
Get-ChildItem -LiteralPath "$root\build" -File | Where-Object {$_.Extension -in '.hex','.bin','.elf','.map'} | Copy-Item -Destination "$root\release"
& python "$PSScriptRoot\inspect.py"
if($LASTEXITCODE -ne 0){throw 'Image inspection failed'}
Copy-Item -LiteralPath "$root\userCode\runtime\config.h" -Destination "$root\release\config-used.h"
Get-ChildItem -LiteralPath "$root\release" -File | Get-FileHash -Algorithm SHA256 | Select-Object @{n='File';e={Split-Path -Leaf $_.Path}},Hash | Export-Csv -LiteralPath "$root\evidence\SHA256.csv" -NoTypeInformation -Encoding UTF8
Write-Host 'BUILD AND CHECKS PASS. This script does not connect to or flash hardware.'
