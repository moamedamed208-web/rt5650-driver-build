param(
    [string]$PackageDir = (Resolve-Path (Join-Path $PSScriptRoot "..\package" )).Path,
    [string]$Subject = "CN=RT5650 KMDF Test"
)

$ErrorActionPreference = 'Stop'

$sys = Join-Path $PackageDir 'Rt5650SpbFilter.sys'
$inf = Join-Path $PackageDir 'Rt5650SpbFilter.inf'
$cat = Join-Path $PackageDir 'Rt5650SpbFilter.cat'

if (-not (Test-Path $sys)) {
    throw "Missing $sys. Build the driver first and point -PackageDir to the folder that contains the .sys/.inf files."
}

if (-not (Test-Path $inf)) {
    throw "Missing $inf."
}

$wdkBinRoot = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Directory |
    Sort-Object Name -Descending |
    Select-Object -First 1

if (-not $wdkBinRoot) {
    throw 'WDK bin folder not found.'
}

$signtool = Join-Path $wdkBinRoot.FullName 'x64\signtool.exe'
$inf2cat = Join-Path $wdkBinRoot.FullName 'x64\Inf2Cat.exe'

if (-not (Test-Path $signtool)) { throw "signtool.exe not found: $signtool" }
if (-not (Test-Path $inf2cat)) { throw "Inf2Cat.exe not found: $inf2cat" }

$cert = New-SelfSignedCertificate -Type CodeSigningCert `
    -Subject $Subject `
    -CertStoreLocation 'Cert:\LocalMachine\My'

$cerPath = Join-Path $PackageDir 'RT5650Test.cer'
Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null

certutil -addstore -f Root $cerPath | Out-Null
certutil -addstore -f TrustedPublisher $cerPath | Out-Null

if (-not (Test-Path $cat)) {
    & $inf2cat /driver:$PackageDir /os:10_X64,11_X64
    if ($LASTEXITCODE -ne 0) {
        throw 'Inf2Cat failed.'
    }
}

& $signtool sign /v /fd SHA256 /s My /sm /sha1 $cert.Thumbprint $sys $cat
if ($LASTEXITCODE -ne 0) {
    throw 'signtool failed.'
}

Write-Host 'Signed successfully:' -ForegroundColor Green
Write-Host "  $sys"
Write-Host "  $cat"
Write-Host ''
Write-Host 'If Windows still blocks loading, enable test mode and reboot:' -ForegroundColor Yellow
Write-Host '  bcdedit /set testsigning on'
