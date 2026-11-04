<#
.SYNOPSIS
    Builds MergeFiles.exe (MFC GUI), MergeFilesCli.exe and MergeFilesTests.exe into build\bin\<Config>.

.DESCRIPTION
    Configures and builds with the CMake on PATH, or else the CMake that ships with Visual Studio 2022
    (no separate CMake install needed).

.PARAMETER Config
    Release (default) or Debug.

.PARAMETER Version
    Version stamped into the programs (major.minor.patch). Default: the one already configured
    (1.0.0 on a fresh build folder).

.PARAMETER BuildNumber
    Fourth part of the file version (the CI run number). Default 0.

.PARAMETER WarningsAsErrors
    Fail the build on any compiler warning (/WX). The CI build uses it; the code builds warning-free.

.PARAMETER Test
    Run the engine unit tests and the CLI checks on testdata\ after building (scripts\test.ps1).
#>
param(
    [ValidateSet("Release", "Debug")][string]$Config = "Release",
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [ValidateRange(0, 65535)][int]$BuildNumber = 0,
    [switch]$WarningsAsErrors,
    [switch]$Test
)

$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.ATLMFC -property installationPath | Select-Object -First 1
    if (-not $vs) { throw "Visual Studio 2022 with the 'C++ MFC for latest build tools' component is required." }
    $cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
}

function Invoke-Checked {
    param([string]$Exe, [string[]]$Arguments)
    # CMake and MSBuild write progress to stderr; Windows PowerShell would treat that as a terminating
    # error under "Stop", so judge success by the exit code instead.
    $ErrorActionPreference = "Continue"
    & $Exe @Arguments 2>&1 | ForEach-Object { "$_" }
    if ($LASTEXITCODE -ne 0) { throw "$Exe $($Arguments -join ' ') failed with exit code $LASTEXITCODE" }
}

$configure = @("-S", ".", "-B", "build", "-G", "Visual Studio 17 2022", "-A", "x64",
    "-DMERGEFILES_BUILD_NUMBER=$BuildNumber", "-DMERGEFILES_WARNINGS_AS_ERRORS=$(if ($WarningsAsErrors) { 'ON' } else { 'OFF' })")
if ($Version) { $configure += "-DMERGEFILES_VERSION=$Version" }
Invoke-Checked $cmake $configure
Invoke-Checked $cmake @("--build", "build", "--config", $Config, "--parallel", "--", "/v:minimal", "/nologo")

Get-ChildItem (Join-Path $Root "build\bin\$Config") -Filter *.exe | ForEach-Object {
    Write-Host ("Built {0} ({1:N1} MB)" -f $_.FullName, ($_.Length / 1MB)) -ForegroundColor Green
}

if ($Test) {
    & (Join-Path $PSScriptRoot "test.ps1") -Config $Config
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
