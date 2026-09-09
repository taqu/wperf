param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [string]$OutputDir = "artifacts"
)

$ErrorActionPreference = "Stop"

$exe = Join-Path $BuildDir "wperf.exe"
$license = Join-Path (Get-Location) "LICENSE"
if (-not (Test-Path -LiteralPath $exe)) { throw "Release executable not found: $exe" }
if (-not (Test-Path -LiteralPath $license)) { throw "LICENSE not found" }
if ($Version -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$') {
    throw "Version must be a semantic version tag such as v0.1.0 or v0.1.0-rc1"
}
$projectFile = Get-Content -LiteralPath (Join-Path (Get-Location) "CMakeLists.txt") -Raw
$sourceVersion = [regex]::Match($projectFile, 'project\(\s*(?:\$\{[^}]+\}|\w+)\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)').Groups[1].Value
if ([string]::IsNullOrEmpty($sourceVersion)) { throw "Canonical project version was not found in CMakeLists.txt" }
if (($Version -replace '^v', '') -notmatch "^$([regex]::Escape($sourceVersion))(?:-|$)") {
    throw "Package version $Version does not match canonical source version $sourceVersion"
}

$output = [IO.Path]::GetFullPath($OutputDir)
$stage = Join-Path $output "package"
$archive = Join-Path $output ("wperf-{0}-windows-x64.zip" -f $Version)
$checksum = "$archive.sha256"

New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $stage "wperf.exe") -Force
Copy-Item -LiteralPath $license -Destination (Join-Path $stage "LICENSE") -Force
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
if (Test-Path -LiteralPath $checksum) { Remove-Item -LiteralPath $checksum -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $archive -CompressionLevel Optimal

$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $(Split-Path $archive -Leaf)" | Set-Content -LiteralPath $checksum -Encoding ascii
Write-Output $archive
Write-Output $checksum
