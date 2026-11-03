param(
    [ValidateRange(1, 65535)][int]$MasterPort = 45450,
    [ValidateRange(1, 65535)][int]$RoomPort = 45451,
    [switch]$EnableMicrophone
)
$ErrorActionPreference = 'Stop'
$sdkPath = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
$adbPath = Join-Path $sdkPath 'platform-tools\adb.exe'
if (-not (Test-Path -LiteralPath $adbPath)) { throw 'Set ANDROID_HOME to your Android SDK directory.' }
& $adbPath -e reverse "tcp:$MasterPort" "tcp:$MasterPort"
if ($LASTEXITCODE -ne 0) { throw 'Start exactly one Android emulator, then retry.' }
& $adbPath -e reverse "tcp:$RoomPort" "tcp:$RoomPort"
if ($LASTEXITCODE -ne 0) { throw 'Could not forward the room port.' }
Write-Output "Use 127.0.0.1 and $MasterPort in the emulator. Run this script again after restarting it."
if ($EnableMicrophone) { & (Join-Path $PSScriptRoot 'enable_emulator_microphone.ps1') }
