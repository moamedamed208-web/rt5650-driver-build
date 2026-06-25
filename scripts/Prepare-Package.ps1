$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildDir = Join-Path $root 'x64\Release'
$packageDir = Join-Path $root 'package'

$sys = Join-Path $buildDir 'Rt5650SpbFilter.sys'
$inf = Join-Path $root 'Rt5650SpbFilter.inf'
$pdb = Join-Path $buildDir 'Rt5650SpbFilter.pdb'
$cat = Join-Path $buildDir 'Rt5650SpbFilter.cat'

if (-not (Test-Path $sys)) {
    throw "Build output not found: $sys"
}

New-Item -ItemType Directory -Force -Path $packageDir | Out-Null
Copy-Item $sys $packageDir -Force
Copy-Item $inf $packageDir -Force
if (Test-Path $pdb) { Copy-Item $pdb $packageDir -Force }
if (Test-Path $cat) { Copy-Item $cat $packageDir -Force }

Write-Host "Package prepared in: $packageDir" -ForegroundColor Green
