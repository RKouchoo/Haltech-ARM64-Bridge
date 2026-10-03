#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$folder = 'C:\Program Files (x86)\Haltech\Nexus Software\Haltech NSP'
$components = @('ftd2xx.dll')
# Retire only the experimental app-local helper, after preserving it alongside
# the old DLL. Restoring that snapshot must still recover the matched pair.
$retiredComponents = @('haltech-ftdi-arm64.exe')
$snapshotComponents = $components + $retiredComponents

if (-not (Test-Path -LiteralPath (Join-Path $folder 'NSP.exe'))) {
    throw "NSP was not found at $folder"
}
# Refuse to interrupt an ECU operation. Installation never terminates a
# calibration write or discards an open map to replace a loaded DLL.
if (Get-Process -Name NSP,haltech-ftdi-arm64,haltech-ftdi-arm64-v2,haltech-ftdi-arm64-v3 -ErrorAction SilentlyContinue) {
    throw 'Save your work and close NSP and bridge helpers before installing.'
}

function Get-PeMachine([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Not a PE file: $Path" }
        $stream.Position = 0x3c
        $offset = $reader.ReadUInt32()
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550) { throw "Invalid PE header: $Path" }
        return $reader.ReadUInt16()
    } finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

$expectedMachine = @{ 'ftd2xx.dll' = 0x14c }
foreach ($name in $components) {
    $source = Join-Path $repo ("dist\" + $name)
    if (-not (Test-Path -LiteralPath $source)) { throw "Run build.cmd first: missing $source" }
    if ((Get-PeMachine $source) -ne $expectedMachine[$name]) { throw "Wrong CPU architecture: $source" }
}
$nativeDriver = Join-Path $env:WINDIR 'System32\drivers\ftser2k.sys'
if (-not (Test-Path -LiteralPath $nativeDriver) -or (Get-PeMachine $nativeDriver) -ne 0xaa64) {
    throw 'Install the ARM64 FTDI VCP (virtual COM port) driver first.'
}

# Preserve the original uninstall checkpoint, plus a separate full snapshot
# for every upgrade so that the immediately previous working bridge survives.
$targetDll = Join-Path $folder 'ftd2xx.dll'
$originalBackup = Join-Path $folder 'ftd2xx.pre-arm64-bridge.dll'
if ((Test-Path -LiteralPath $targetDll) -and -not (Test-Path -LiteralPath $originalBackup)) {
    Copy-Item -LiteralPath $targetDll -Destination $originalBackup
}
$backupRoot = Join-Path $folder 'bridge-backups'
$backupFolder = Join-Path $backupRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $backupFolder -Force | Out-Null
$existed = @{}
foreach ($name in $snapshotComponents) {
    $target = Join-Path $folder $name
    $existed[$name] = Test-Path -LiteralPath $target
    if ($existed[$name]) { Copy-Item -LiteralPath $target -Destination (Join-Path $backupFolder $name) }
}

$installed = @()
try {
    foreach ($name in $components) {
        $source = Join-Path $repo ("dist\" + $name)
        $target = Join-Path $folder $name
        # Include the target in rollback even if Copy-Item fails partway.
        $installed += $name
        Copy-Item -LiteralPath $source -Destination $target -Force
        if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $target).Hash) {
            throw "Verification failed for $name"
        }
    }
    foreach ($name in $retiredComponents) {
        if ($existed[$name]) {
            $installed += $name
            # Exact backed-up application file only; no driver files are removed.
            Remove-Item -LiteralPath (Join-Path $folder $name) -Force
        }
    }
} catch {
    $installError = $_
    foreach ($name in $installed) {
        $target = Join-Path $folder $name
        if ($existed[$name]) {
            Copy-Item -LiteralPath (Join-Path $backupFolder $name) -Destination $target -Force
        } elseif (Test-Path -LiteralPath $target) {
            # Exact newly-created file only; never a recursive removal.
            Remove-Item -LiteralPath $target -Force
        }
    }
    throw $installError
}
# Keep the existing VCP configuration, including its latency setting. Switching
# the application bridge does not reinstall/reset the driver or change the ECU.
Write-Host "Installed VCP-based ARM64 bridge. Backup: $backupFolder"
if ($existed['haltech-ftdi-arm64.exe']) {
    Write-Host 'Removed the unused native helper; it is recoverable from this backup.'
}
Write-Host 'Reopen NSP to use it. Windows does not need to restart.'

