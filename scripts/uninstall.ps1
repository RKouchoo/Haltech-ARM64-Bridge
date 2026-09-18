#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'
$folder = 'C:\Program Files (x86)\Haltech\Nexus Software\Haltech NSP'
$target = Join-Path $folder 'ftd2xx.dll'
$backup = Join-Path $folder 'ftd2xx.pre-arm64-bridge.dll'

Get-Process -Name NSP,haltech-ftdi-arm64,haltech-ftdi-arm64-v2,haltech-ftdi-arm64-v3 -ErrorAction SilentlyContinue | Stop-Process -Force
if (Test-Path -LiteralPath $target) {
    Remove-Item -LiteralPath $target -Force
}
if (Test-Path -LiteralPath $backup) {
    Move-Item -LiteralPath $backup -Destination $target
    Write-Host 'Removed bridge and restored the previous application-local DLL.'
} else {
    Write-Host 'Removed bridge. No application-local backup existed.'
}

