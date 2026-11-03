param(
    [ValidatePattern('^emulator-\d+$')][string]$Serial,
    [switch]$Disable
)
$ErrorActionPreference = 'Stop'
$sdkPath = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
$adbPath = Join-Path $sdkPath 'platform-tools\adb.exe'
if (-not (Test-Path -LiteralPath $adbPath)) { throw 'Set ANDROID_HOME to your Android SDK directory.' }
$selector = if ($Serial) { @('-s', $Serial) } else { @('-e') }
$command = if ($Disable) { 'hostmicoff' } else { 'hostmicon' }
$response = @(& $adbPath @selector emu avd $command 2>&1)
if ($LASTEXITCODE -ne 0 -or -not ($response | Where-Object { "$_".Trim() -eq 'OK' })) {
    throw "Could not change emulator microphone input. Start one emulator, or pass -Serial emulator-5554. $($response -join ' ')"
}
if ($Disable) { Write-Output 'Emulator host microphone input disabled.' }
else {
    Write-Output 'Emulator host microphone input enabled. Turn Mic off/on in the game, then speak.'
    Write-Output 'Run this again after restarting the emulator; its host microphone switch resets.'
}
