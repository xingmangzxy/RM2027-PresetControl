$ErrorActionPreference='Stop'
$known=@('D:\RM27\ArmGnu\bin','D:\RM27\CLion\bin\cmake\win\x64\bin','C:\msys64\ucrt64\bin',(Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312'))
$packages=Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
if(Test-Path -LiteralPath $packages) {
    $ninja=Get-ChildItem -LiteralPath $packages -Directory -Filter 'Ninja-build.Ninja_*' | Select-Object -First 1
    if($ninja){$known+=$ninja.FullName}
    $openocd=Get-ChildItem -LiteralPath $packages -Directory -Filter '*openocd*' | Get-ChildItem -Recurse -File -Filter openocd.exe | Select-Object -First 1
    if($openocd){$known+=$openocd.DirectoryName}
}
$env:PATH=(($known | Where-Object {Test-Path -LiteralPath $_}) -join ';')+';'+$env:PATH
