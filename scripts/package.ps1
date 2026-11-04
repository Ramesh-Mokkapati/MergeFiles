<#
.SYNOPSIS
    Zips a built release for distribution: dist\MergeFiles-<version>-win-x64.zip and its .sha256.

.DESCRIPTION
    The zip holds MergeFiles.exe, MergeFilesCli.exe, MergeFiles.html (the F1 help) and README.md, flat.
    Build first (scripts\build.ps1). The programs need the Microsoft Visual C++ 2015-2022
    Redistributable (x64), which includes the MFC DLLs.

.PARAMETER Version
    The version in the file name (major.minor.patch).

.PARAMETER Config
    Which build\bin\<Config> to package (default Release).
#>
param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [ValidateSet("Release", "Debug")][string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent
$Bin = Join-Path $Root "build\bin\$Config"
$name = "MergeFiles-$Version-win-x64"

$files = @("MergeFiles.exe", "MergeFilesCli.exe", "MergeFiles.html") | ForEach-Object { Join-Path $Bin $_ }
$files += Join-Path $Root "README.md"
foreach ($file in $files) {
    if (-not (Test-Path $file)) { throw "Missing $file (build first with scripts\build.ps1)" }
}

$stage = Join-Path ([System.IO.Path]::GetTempPath()) $name
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory $stage | Out-Null
Copy-Item $files $stage

$dist = Join-Path $Root "dist"
New-Item -ItemType Directory $dist -Force | Out-Null
$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
Remove-Item $stage -Recurse -Force

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
[System.IO.File]::WriteAllText("$zip.sha256", "$hash  $name.zip", (New-Object System.Text.UTF8Encoding $false))
Get-ChildItem $dist -Filter "$name*" | Format-Table Name, Length
