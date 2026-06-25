param(
    [string]$PackageDir = (Resolve-Path (Join-Path $PSScriptRoot "..\package" )).Path
)

$ErrorActionPreference = 'Stop'

$inf = Join-Path $PackageDir 'Rt5650SpbFilter.inf'
if (-not (Test-Path $inf)) {
    throw "INF not found: $inf"
}

pnputil /add-driver $inf /install
if ($LASTEXITCODE -ne 0) {
    throw 'pnputil failed.'
}

Write-Host 'Driver package staged/installed. Reboot or disable/enable the device if needed.' -ForegroundColor Green
