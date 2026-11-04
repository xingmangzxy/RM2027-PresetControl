$ErrorActionPreference='Stop'
. "$PSScriptRoot\env.ps1"
& python -X utf8 "$PSScriptRoot\monitor.py"
if($LASTEXITCODE -ne 0){throw 'Diagnostic viewer failed'}
