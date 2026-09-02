[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $ExecutablePath,
    [Parameter(Mandatory = $true)]
    [string] $CertificateThumbprint,
    [string] $TimestampUrl = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$executable = Get-Item -LiteralPath $ExecutablePath -ErrorAction Stop
$thumbprint = $CertificateThumbprint.Replace(" ", "").ToUpperInvariant()
$certificate = Get-ChildItem -Path "Cert:\CurrentUser\My\$thumbprint" -ErrorAction SilentlyContinue
if ($null -eq $certificate) {
    throw "The signing certificate '$thumbprint' was not found in Cert:\CurrentUser\My."
}
if (-not $certificate.HasPrivateKey) {
    throw "The signing certificate '$thumbprint' does not have an accessible private key."
}

$signTool = Get-Command signtool.exe -ErrorAction SilentlyContinue
if ($null -eq $signTool) {
    throw "signtool.exe is not on PATH. Run this script from a Visual Studio developer shell."
}

& $signTool.Source sign /sha1 $thumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $executable.FullName
if ($LASTEXITCODE -ne 0) {
    throw "signtool.exe failed with exit code $LASTEXITCODE."
}

& $signTool.Source verify /pa /all $executable.FullName
if ($LASTEXITCODE -ne 0) {
    throw "signtool.exe could not verify the signed executable."
}

Write-Host "Signed and verified: $($executable.FullName)"
