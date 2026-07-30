param(
    [Parameter(Mandatory = $true)]
    [string] $Tag,

    [Parameter(Mandatory = $false)]
    [string] $ChangelogPath = "CHANGELOG.md",

    [Parameter(Mandatory = $false)]
    [string] $OutputPath = "release-notes.md"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $ChangelogPath -PathType Leaf)) {
    throw "Changelog file not found: $ChangelogPath"
}

$content = Get-Content -LiteralPath $ChangelogPath -Raw -Encoding UTF8
$escapedTag = [Regex]::Escape($Tag)
$pattern = "(?ms)^##[ \t]+$escapedTag[ \t]*\r?\n(?<notes>.*?)(?=^##[ \t]+|\z)"
$match = [Regex]::Match($content, $pattern)

if (-not $match.Success) {
    throw "CHANGELOG.md does not contain a '## $Tag' section."
}

$notes = $match.Groups["notes"].Value.Trim()
if ([string]::IsNullOrWhiteSpace($notes)) {
    throw "The changelog section for $Tag is empty."
}

Set-Content -LiteralPath $OutputPath -Value $notes -Encoding utf8NoBOM
Write-Host "Release notes for $Tag written to $OutputPath"
