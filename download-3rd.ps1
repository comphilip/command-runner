[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$repositoryPath = Split-Path -Parent $MyInvocation.MyCommand.Path
$thirdPartyPath = Join-Path $repositoryPath "3rd"
$downloadUri = "https://github.com/DavidNash2024/Win32xx/archive/refs/heads/master.tar.gz"

$temporaryName = [Guid]::NewGuid().ToString("N")
$archivePath = Join-Path ([IO.Path]::GetTempPath()) "win32xx-$temporaryName.tar.gz"
$extractPath = Join-Path ([IO.Path]::GetTempPath()) "win32xx-$temporaryName"

try {
    Write-Host "Downloading Win32xx..."
    Invoke-WebRequest -Uri $downloadUri -OutFile $archivePath

    New-Item -ItemType Directory -Path $extractPath -Force | Out-Null

    if (-not (Get-Command tar.exe -ErrorAction SilentlyContinue)) {
        throw "tar.exe is required to extract the Win32xx archive."
    }

    & tar.exe -xzf $archivePath -C $extractPath
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to extract the Win32xx archive. tar.exe exited with code $LASTEXITCODE."
    }

    $includePath = Get-ChildItem -LiteralPath $extractPath -Directory -Recurse |
        Where-Object { $_.Name -eq "include" } |
        Select-Object -First 1

    if ($null -eq $includePath) {
        throw "The downloaded Win32xx archive does not contain an include directory."
    }

    if (Test-Path -LiteralPath $thirdPartyPath) {
        Remove-Item -LiteralPath $thirdPartyPath -Recurse -Force
    }

    $destinationPath = Join-Path $thirdPartyPath "win32xx"
    New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null
    Copy-Item -LiteralPath $includePath.FullName -Destination $destinationPath -Recurse

    Write-Host "Win32xx headers installed to $destinationPath\include"
}
finally {
    if (Test-Path -LiteralPath $archivePath) {
        Remove-Item -LiteralPath $archivePath -Force
    }

    if (Test-Path -LiteralPath $extractPath) {
        Remove-Item -LiteralPath $extractPath -Recurse -Force
    }
}
