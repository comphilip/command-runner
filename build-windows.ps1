[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string] $Configuration = "Release",
    [switch] $SkipTests,
    [string] $CertificateThumbprint,
    [string] $TimestampUrl = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
Set-Location $PSScriptRoot

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string] $FilePath,
        [Parameter(Mandatory = $false)]
        [string[]] $ArgumentList = @()
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($ArgumentList -join ' ')"
    }
}

$preset = "windows-x64-$($Configuration.ToLowerInvariant())"
$buildDirectory = Join-Path $PSScriptRoot "out\build\$preset"
$executablePath = Join-Path $buildDirectory "$Configuration\CommandRunner.exe"

Invoke-Checked -FilePath "cmake" -ArgumentList @("--preset", $preset)
Invoke-Checked -FilePath "cmake" -ArgumentList @(
    "--build", "--preset", $preset, "--config", $Configuration, "--parallel"
)

if (-not $SkipTests) {
    Invoke-Checked -FilePath "ctest" -ArgumentList @(
        "--preset", $preset, "--output-on-failure"
    )
}

if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
    throw "The native build did not produce $executablePath"
}

$verificationScript = Join-Path $PSScriptRoot "scripts\verify-release.ps1"
if ($Configuration -eq "Release") {
    & $verificationScript -ExecutablePath $executablePath
    if ($LASTEXITCODE -ne 0) {
        throw "Release verification failed."
    }
}

if ($CertificateThumbprint -and $Configuration -ne "Release") {
    throw "Code signing is only supported for Release builds."
}
if ($CertificateThumbprint) {
    $signingScript = Join-Path $PSScriptRoot "scripts\sign-release.ps1"
    & $signingScript `
        -ExecutablePath $executablePath `
        -CertificateThumbprint $CertificateThumbprint `
        -TimestampUrl $TimestampUrl
    if ($LASTEXITCODE -ne 0) {
        throw "Code signing failed."
    }
    & $verificationScript `
        -ExecutablePath $executablePath `
        -RequireSignature
    if ($LASTEXITCODE -ne 0) {
        throw "Signed release verification failed."
    }
}

Write-Host ""
Write-Host "Build complete: $executablePath"
