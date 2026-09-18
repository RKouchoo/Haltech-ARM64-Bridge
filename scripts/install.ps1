#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repo 'dist\ftd2xx.dll'
$folder = 'C:\Program Files (x86)\Haltech\Nexus Software\Haltech NSP'
$target = Join-Path $folder 'ftd2xx.dll'
$backup = Join-Path $folder 'ftd2xx.pre-arm64-bridge.dll'

if (-not (Test-Path -LiteralPath $source)) {
    throw "Build output not found: $source. Run build.cmd first."
}
if (-not (Test-Path -LiteralPath (Join-Path $folder 'NSP.exe'))) {
    throw "NSP was not found at its default location: $folder"
}
Get-Process -Name NSP,haltech-ftdi-arm64,haltech-ftdi-arm64-v2,haltech-ftdi-arm64-v3 -ErrorAction SilentlyContinue | Stop-Process -Force
if ((Test-Path -LiteralPath $target) -and -not (Test-Path -LiteralPath $backup)) {
    Copy-Item -LiteralPath $target -Destination $backup
}
Copy-Item -LiteralPath $source -Destination $target -Force

$ftdiRoot = 'HKLM:\SYSTEM\CurrentControlSet\Enum\FTDIBUS'
if (Test-Path -LiteralPath $ftdiRoot) {
    Get-ChildItem -LiteralPath $ftdiRoot | Where-Object { $_.PSChildName -like 'VID_0403+PID_6014+*' } | ForEach-Object {
        Get-ChildItem -LiteralPath $_.PSPath | ForEach-Object {
            $parameters = Join-Path $_.PSPath 'Device Parameters'
            if (Test-Path -LiteralPath $parameters) {
                Set-ItemProperty -LiteralPath $parameters -Name LatencyTimer -Type DWord -Value 2
                Set-ItemProperty -LiteralPath $parameters -Name MinReadTimeout -Type DWord -Value 0
                Set-ItemProperty -LiteralPath $parameters -Name MinWriteTimeout -Type DWord -Value 0
            }
        }
    }
}
Write-Host 'Installed. Restart Windows before testing NSP.'

