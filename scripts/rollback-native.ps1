#Requires -RunAsAdministrator
param([Parameter(Mandatory = $true)][string]$BackupFolder)
$ErrorActionPreference = 'Stop'
$folder = 'C:\Program Files (x86)\Haltech\Nexus Software\Haltech NSP'
if (Get-Process -Name NSP,haltech-ftdi-arm64 -ErrorAction SilentlyContinue) {
    throw 'Close NSP and its helper before rolling back.'
}
$backupRoot = [IO.Path]::GetFullPath((Join-Path $folder 'bridge-backups')) + '\'
$resolvedBackup = (Resolve-Path -LiteralPath $BackupFolder).ProviderPath
if (-not $resolvedBackup.StartsWith($backupRoot, [StringComparison]::OrdinalIgnoreCase) -or
    -not (Test-Path -LiteralPath (Join-Path $resolvedBackup 'ftd2xx.dll'))) {
    throw 'Choose an install snapshot inside the NSP bridge-backups folder containing ftd2xx.dll.'
}
$recovery = Join-Path $backupRoot ('before-rollback-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $recovery | Out-Null
foreach ($name in @('ftd2xx.dll', 'haltech-ftdi-arm64.exe')) {
    $target = Join-Path $folder $name
    $source = Join-Path $resolvedBackup $name
    if (Test-Path -LiteralPath $target) {
        Copy-Item -LiteralPath $target -Destination (Join-Path $recovery $name)
    }
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination $target -Force
    } elseif (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Force
    }
}
Write-Host "Restored $resolvedBackup. Replaced files remain recoverable in $recovery."
