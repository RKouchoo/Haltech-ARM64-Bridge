$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$installer = Get-Content -LiteralPath (Join-Path $repo 'scripts\install.ps1') -Raw
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('haltech-install-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$script:checks = 0

function Assert-True([bool]$Condition, [string]$Description) {
    $script:checks++
    if (-not $Condition) { throw "FAIL: $Description" }
}

function New-Fixture([string]$Name, [bool]$Existing = $true) {
    $root = Join-Path $testRoot $Name
    $app = Join-Path $root 'app'
    $package = Join-Path $root 'repo'
    New-Item -ItemType Directory -Path $app, (Join-Path $package 'dist') -Force | Out-Null
    # Generated, inert PE fixtures: the installer reads the headers but never
    # loads them. Tests require neither a driver nor an ECU nor administrator.
    $pe = New-Object byte[] 128
    $pe[0] = 0x4d; $pe[1] = 0x5a; $pe[0x3c] = 0x40
    $pe[0x40] = 0x50; $pe[0x41] = 0x45
    $pe[0x44] = 0x4c; $pe[0x45] = 0x01
    [IO.File]::WriteAllBytes((Join-Path $package 'dist\ftd2xx.dll'), $pe)
    $pe[0x44] = 0x64; $pe[0x45] = 0xaa
    $driver = Join-Path $root 'ftser2k.sys'
    [IO.File]::WriteAllBytes($driver, $pe)
    [IO.File]::WriteAllText((Join-Path $app 'NSP.exe'), 'inert NSP fixture')
    if ($Existing) {
        [IO.File]::WriteAllText((Join-Path $app 'ftd2xx.dll'), 'old DLL')
        [IO.File]::WriteAllText((Join-Path $app 'haltech-ftdi-arm64.exe'), 'old helper')
        [IO.File]::WriteAllText((Join-Path $app 'ftd2xx.pre-arm64-bridge.dll'), 'original checkpoint')
    }
    return @{ App = $app; Repo = $package; Driver = $driver }
}

function Invoke-Fixture($Fixture, [string]$Fault = '', [bool]$Running = $false) {
    # Replace all three external paths before executing the actual installer.
    # Exact matches fail closed if its layout changes. No real NSP/driver path
    # is reachable, and process discovery is mocked in this invocation's scope.
    $replacements = @{
        '$repo = Split-Path -Parent $PSScriptRoot' = $Fixture.Repo
        '$folder = ''C:\Program Files (x86)\Haltech\Nexus Software\Haltech NSP''' = $Fixture.App
        '$nativeDriver = Join-Path $env:WINDIR ''System32\drivers\ftser2k.sys''' = $Fixture.Driver
    }
    $scriptText = $installer
    foreach ($declaration in $replacements.Keys) {
        if (-not $scriptText.Contains($declaration)) { throw "Unrecognised installer layout: $declaration" }
        $variable = $declaration.Substring(0, $declaration.IndexOf(' = '))
        $quotedPath = $replacements[$declaration].Replace("'", "''")
        $scriptText = $scriptText.Replace($declaration, "$variable = '$quotedPath'")
    }
    $scriptText = $scriptText -replace '(?m)^#Requires -RunAsAdministrator\r?\n', ''

    function Get-Process { if ($Running) { [pscustomobject]@{ ProcessName = 'NSP' } } }
    function Copy-Item {
        param([string]$LiteralPath, [string]$Destination, [switch]$Force)
        Microsoft.PowerShell.Management\Copy-Item @PSBoundParameters
        if ($LiteralPath -eq (Join-Path $Fixture.Repo 'dist\ftd2xx.dll')) {
            if ($Fault -eq 'copy') { throw 'Injected copy failure after target was replaced' }
            if ($Fault -eq 'hash') { [IO.File]::WriteAllText($Destination, 'corrupt copy') }
        }
    }
    function Remove-Item {
        param([string]$LiteralPath, [switch]$Force)
        Microsoft.PowerShell.Management\Remove-Item @PSBoundParameters
        if ($Fault -eq 'retire' -and $LiteralPath -eq (Join-Path $Fixture.App 'haltech-ftdi-arm64.exe')) {
            throw 'Injected failure after helper removal'
        }
    }

    $caught = $null
    try { & ([scriptblock]::Create($scriptText)) 6>$null }
    catch { $caught = $_.Exception.Message }
    return $caught
}

function Assert-OldPair($Fixture) {
    Assert-True ((Get-Content -LiteralPath (Join-Path $Fixture.App 'ftd2xx.dll') -Raw) -eq 'old DLL') 'old DLL retained/restored'
    Assert-True ((Get-Content -LiteralPath (Join-Path $Fixture.App 'haltech-ftdi-arm64.exe') -Raw) -eq 'old helper') 'old helper retained/restored'
}

$fixture = New-Fixture 'migration'
Assert-True (-not (Invoke-Fixture $fixture)) 'native-to-VCP migration succeeds'
Assert-True ((Get-FileHash (Join-Path $fixture.App 'ftd2xx.dll')).Hash -eq (Get-FileHash (Join-Path $fixture.Repo 'dist\ftd2xx.dll')).Hash) 'installed DLL matches package'
Assert-True (-not (Test-Path (Join-Path $fixture.App 'haltech-ftdi-arm64.exe'))) 'unused helper removed'
$snapshots = @(Get-ChildItem (Join-Path $fixture.App 'bridge-backups') -Directory)
Assert-True ($snapshots.Count -eq 1) 'one recovery snapshot created'
Assert-True ((Get-Content (Join-Path $snapshots[0].FullName 'ftd2xx.dll') -Raw) -eq 'old DLL') 'old DLL backed up'
Assert-True ((Get-Content (Join-Path $snapshots[0].FullName 'haltech-ftdi-arm64.exe') -Raw) -eq 'old helper') 'old helper backed up'
Assert-True ((Get-Content (Join-Path $fixture.App 'ftd2xx.pre-arm64-bridge.dll') -Raw) -eq 'original checkpoint') 'original uninstall checkpoint preserved'
Assert-True (-not (Invoke-Fixture $fixture)) 'VCP-to-VCP reinstall succeeds without a helper'

$fixture = New-Fixture 'fresh' $false
Assert-True (-not (Invoke-Fixture $fixture)) 'fresh install needs only the VCP DLL and driver'
Assert-True (-not (Test-Path (Join-Path $fixture.App 'haltech-ftdi-arm64.exe'))) 'fresh install does not introduce a helper'

$fixture = New-Fixture 'running'
Assert-True ((Invoke-Fixture $fixture -Running $true) -like '*close NSP*') 'refuse install while NSP runs'
Assert-OldPair $fixture
Assert-True (-not (Test-Path (Join-Path $fixture.App 'bridge-backups'))) 'running-app refusal makes no snapshot or changes'

$fixture = New-Fixture 'no-driver'
Remove-Item -LiteralPath $fixture.Driver
Assert-True ((Invoke-Fixture $fixture) -like '*VCP*driver first*') 'reject missing VCP driver'
Assert-OldPair $fixture

$fixture = New-Fixture 'wrong-architecture'
Copy-Item -LiteralPath $fixture.Driver -Destination (Join-Path $fixture.Repo 'dist\ftd2xx.dll') -Force
Assert-True ((Invoke-Fixture $fixture) -like '*Wrong CPU architecture*') 'reject ARM64 DLL in the x86 application package'
Assert-OldPair $fixture

foreach ($fault in @('copy', 'hash', 'retire')) {
    $fixture = New-Fixture $fault
    $errorMessage = Invoke-Fixture $fixture -Fault $fault
    Assert-True ($errorMessage -like '*Injected*' -or $errorMessage -like '*Verification failed*') "detect $fault failure"
    Assert-OldPair $fixture
}

$fixture = New-Fixture 'fresh-copy-failure' $false
Assert-True ((Invoke-Fixture $fixture -Fault 'copy') -like '*Injected*') 'fresh install detects copy failure'
Assert-True (-not (Test-Path (Join-Path $fixture.App 'ftd2xx.dll'))) 'fresh failure removes only the newly-created DLL'

Write-Host "$script:checks installer checks passed; no real NSP files, processes, registry or devices accessed."
# Leave these small inert fixtures for inspection. No recursive cleanup or
# computed delete operation is needed in a test of installer safety.
Write-Host "Test fixtures: $testRoot"
