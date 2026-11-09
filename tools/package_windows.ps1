# Packages the Windows demo for distribution. Used by both CI (every PR) and
# the release workflow, so the release packaging is tested before tagging.
#
#   pwsh tools/package_windows.ps1 -Version v0.1.0 -OutDir dist
#
# Produces in OutDir:
#   aether-engine-<Version>-windows-x64.zip   exe, LICENSE, NOTICE, licences, README, BUILD_INFO
#   aether-engine-<Version>-dependencies.json dependency manifest (minimal SBOM)
#   SHA256SUMS.txt                            checksums of the files above
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$OutDir = "dist",
    [string]$BuildDir = "build/windows-msvc"
)
$ErrorActionPreference = "Stop"

$name = "aether-engine-$Version-windows-x64"
$stage = Join-Path $OutDir $name
if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
New-Item -ItemType Directory -Force -Path (Join-Path $stage "licenses") | Out-Null

Copy-Item "$BuildDir/samples/spinning_cubes/Release/aether_spinning_cubes.exe" $stage
Copy-Item LICENSE, NOTICE $stage
Copy-Item third_party/licenses/* (Join-Path $stage "licenses")
Copy-Item samples/spinning_cubes/README.md (Join-Path $stage "README.md")
$info = @(
    "version: $Version"
    "commit: $env:GITHUB_SHA"
    "build log: $env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID"
)
$info | Out-File -Encoding utf8 (Join-Path $stage "BUILD_INFO.txt")

Compress-Archive -Path $stage -DestinationPath (Join-Path $OutDir "$name.zip")
Remove-Item -Recurse -Force $stage
Copy-Item third_party/dependencies.json (Join-Path $OutDir "aether-engine-$Version-dependencies.json")

# Collect the file list before writing SHA256SUMS.txt into the same folder;
# a streaming pipeline would try to hash its own (open) output file.
$files = @(Get-ChildItem $OutDir -File | Sort-Object Name)
$lines = foreach ($f in $files) {
    "{0}  {1}" -f (Get-FileHash $f.FullName -Algorithm SHA256).Hash.ToLower(), $f.Name
}
$lines | Out-File -Encoding ascii (Join-Path $OutDir "SHA256SUMS.txt")

Get-ChildItem $OutDir | Format-Table Name, Length
Get-Content (Join-Path $OutDir "SHA256SUMS.txt")
