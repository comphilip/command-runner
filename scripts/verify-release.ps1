[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $ExecutablePath,
    [int64] $TargetBytes = 2MB,
    [int64] $MaximumBytes = 5MB,
    [switch] $RequireSignature
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$executable = Get-Item -LiteralPath $ExecutablePath -ErrorAction Stop
if ($executable.Extension -ine ".exe") {
    throw "Release artifact must be an EXE: $($executable.FullName)"
}
if ($executable.Length -gt $MaximumBytes) {
    throw "Release EXE is $($executable.Length) bytes; the limit is $MaximumBytes bytes."
}
if ($executable.Length -gt $TargetBytes) {
    Write-Warning "Release EXE is above the preferred $TargetBytes-byte target."
}

$version = $executable.VersionInfo
$requiredVersionFields = @{
    FileDescription = "Native Windows command runner"
    ProductName = "Command Runner"
    OriginalFilename = "CommandRunner.exe"
}
foreach ($field in $requiredVersionFields.Keys) {
    $actualValue = $version.$field
    if ([string]::IsNullOrWhiteSpace($actualValue) -or
        $actualValue -ne $requiredVersionFields[$field]) {
        throw "Version metadata field '$field' is missing or incorrect: '$actualValue'"
    }
}

$dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if ($null -eq $dumpbin) {
    Write-Warning "dumpbin.exe is not on PATH; PE architecture/import checks were skipped."
} else {
    $headers = & $dumpbin.Source /headers $executable.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin /headers failed for $($executable.FullName)"
    }
    if ($headers -notmatch "(?i)8664 machine \(x64\)") {
        throw "Release EXE is not an x64 PE image."
    }

    $imports = & $dumpbin.Source /imports $executable.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin /imports failed for $($executable.FullName)"
    }
    $forbiddenImports = "(?i)(python\d*\.dll|python|vcruntime\d*\.dll|msvcp\d*\.dll|clr\.dll|coreclr\.dll|tcl\d*\.dll|tk\d*\.dll)"
    if ($imports -match $forbiddenImports) {
        throw "Release EXE imports a runtime that is not allowed for the native single-EXE build."
    }
}

$signature = $null
try {
    $signature = Get-AuthenticodeSignature -LiteralPath $executable.FullName
} catch {
    if ($RequireSignature) {
        throw "Unable to inspect the Authenticode signature: $($_.Exception.Message)"
    }
    Write-Warning "Authenticode signature inspection is unavailable: $($_.Exception.Message)"
}
if ($null -ne $signature -and $signature.Status -eq "Valid") {
    Write-Host "Signature: valid ($($signature.SignerCertificate.Subject))"
} elseif ($RequireSignature) {
    $status = if ($null -eq $signature) { "Unavailable" } else { $signature.Status }
    throw "A valid Authenticode signature is required, status: $status"
} elseif ($null -ne $signature) {
    Write-Host "Signature: $($signature.Status) (unsigned builds are accepted before release signing)"
} else {
    Write-Host "Signature: unavailable (unsigned builds are accepted before release signing)"
}

$sizeInMiB = [math]::Round($executable.Length / 1MB, 2)
$sha256 = [System.Security.Cryptography.SHA256]::Create()
try {
    $stream = [System.IO.File]::OpenRead($executable.FullName)
    try {
        $hashBytes = $sha256.ComputeHash($stream)
    } finally {
        $stream.Dispose()
    }
} finally {
    $sha256.Dispose()
}
$hash = [System.BitConverter]::ToString($hashBytes).Replace("-", "")
Write-Host "Release artifact: $($executable.FullName)"
Write-Host "Size: $($executable.Length) bytes ($sizeInMiB MiB)"
Write-Host "File version: $($version.FileVersion)"
Write-Host "SHA-256: $hash"
