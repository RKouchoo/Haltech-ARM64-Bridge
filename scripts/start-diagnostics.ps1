$ErrorActionPreference = 'Stop'
$program = 'C:\Program Files (x86)\Haltech\Nexus Software\Haltech NSP\NSP.exe'
if (Get-Process -Name NSP -ErrorAction SilentlyContinue) {
    throw 'Save your work and close NSP first. This script will not terminate it.'
}
if (-not (Test-Path -LiteralPath $program)) {
    throw "NSP was not found at $program"
}
$logFolder = Join-Path $env:LOCALAPPDATA 'Haltech-ARM64-Bridge\Diagnostics'
New-Item -ItemType Directory -Path $logFolder -Force | Out-Null
$sessionId = [guid]::NewGuid().ToString('N')
$tracePath = Join-Path $logFolder ("bridge-{0}-{1}.csv" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $sessionId)

# Only the new NSP process inherits tracing; no persistent environment change.
# Close NSP normally after reproducing the failure to flush the final records.
# Trace files contain timing/counts/errors only, never ECU payloads.
$previousTrace = $env:HALTECH_BRIDGE_TRACE
try {
    $env:HALTECH_BRIDGE_TRACE = $tracePath
    Start-Process -FilePath $program -WorkingDirectory (Split-Path -Parent $program) -WindowStyle Normal
} finally {
    if ($null -eq $previousTrace) {
        Remove-Item Env:\HALTECH_BRIDGE_TRACE -ErrorAction SilentlyContinue
    } else {
        $env:HALTECH_BRIDGE_TRACE = $previousTrace
    }
}
Write-Host "Timing trace: $tracePath"
Write-Host 'Observe live values, retry one log download, then close NSP normally to flush the trace.'
