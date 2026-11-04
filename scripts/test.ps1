<#
.SYNOPSIS
    Runs the engine unit tests and checks MergeFilesCli against the sample files in testdata\.

.PARAMETER Config
    Release (default) or Debug: which build\bin\<Config> to test (build it first with build.ps1).
#>
param([ValidateSet("Release", "Debug")][string]$Config = "Release")

$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent
$Bin = Join-Path $Root "build\bin\$Config"
$Cli = Join-Path $Bin "MergeFilesCli.exe"
$Data = Join-Path $Root "testdata"
$failed = 0

function Check([string]$Name, [bool]$Ok) {
    if ($Ok) { Write-Host "[ ok ] $Name" }
    else { Write-Host "[FAIL] $Name" -ForegroundColor Red; $script:failed++ }
}

& (Join-Path $Bin "MergeFilesTests.exe")
Check "engine unit tests" ($LASTEXITCODE -eq 0)

# Every sample file decodes and encodes back to the same bytes.
$samples = Get-ChildItem (Join-Path $Data "encodings"), (Join-Path $Data "2way") -File | ForEach-Object FullName
& $Cli roundtrip @samples | Out-Null
Check "byte-exact round trip of $($samples.Count) sample files" ($LASTEXITCODE -eq 0)

# Two-way: different files report differences (exit 1), a file against itself none (exit 0).
$t1 = Join-Path $Data "2way\Test1.cpp"; $t2 = Join-Path $Data "2way\Test2.cpp"
& $Cli diff $t1 $t2 | Out-Null
Check "diff of different files exits 1" ($LASTEXITCODE -eq 1)
& $Cli diff $t1 $t1 | Out-Null
Check "diff of a file with itself exits 0" ($LASTEXITCODE -eq 0)
# The same text in UTF-8 and UTF-16 compares equal.
& $Cli diff (Join-Path $Data "encodings\utf8.txt") (Join-Path $Data "encodings\utf16be_bom.txt") | Out-Null
Check "UTF-8 and UTF-16 BE copies of a file compare equal" ($LASTEXITCODE -eq 0)
# CR LF vs LF: equal by default, different with --strict-eol.
$crlf = Join-Path $Data "encodings\utf8.txt"
$lfCopy = Join-Path $env:TEMP "mergefiles_lf_copy.txt"
[IO.File]::WriteAllBytes($lfCopy, ([IO.File]::ReadAllBytes($crlf) | Where-Object { $_ -ne 13 }))
& $Cli diff $crlf $lfCopy | Out-Null
Check "line endings ignored by default" ($LASTEXITCODE -eq 0)
& $Cli diff $crlf $lfCopy --strict-eol | Out-Null
Check "line endings compared with --strict-eol" ($LASTEXITCODE -eq 1)
Remove-Item $lfCopy

# Three-way: automatic merge matches the expected files, with and without conflict markers.
$left = Join-Path $Data "3way\left.ini"; $base = Join-Path $Data "3way\ancestor.ini"; $right = Join-Path $Data "3way\right.ini"
foreach ($case in @(@{ Args = @(); Expected = "expected_merge.ini" }, @{ Args = @("--markers"); Expected = "expected_markers.ini" })) {
    $out = Join-Path $env:TEMP "mergefiles_merge_out.ini"
    $ErrorActionPreference = "Continue"
    & $Cli merge $left $base $right -o $out @($case.Args) 2>$null
    $code = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    $same = [Linq.Enumerable]::SequenceEqual([IO.File]::ReadAllBytes($out), [IO.File]::ReadAllBytes((Join-Path $Data "3way\$($case.Expected)")))
    Check "merge $($case.Args) matches $($case.Expected) (1 conflict)" ($same -and $code -eq 1)
    Remove-Item $out
}

# Folders: two-way and three-way comparison, then an automatic merge into a copy of the ancestor.
$folders = Join-Path $Data "folders"
$dirLeft = Join-Path $folders "left"; $dirBase = Join-Path $folders "ancestor"; $dirRight = Join-Path $folders "right"
$text = (& $Cli dircmp $dirLeft $dirRight) -join "`n"
Check "dircmp reports 4 different and 2 left-only files" ($LASTEXITCODE -eq 1 -and $text -match "1 identical, 4 different, 2 left only, 0 right only")
$text = (& $Cli dircmp $dirLeft $dirLeft) -join "`n"
Check "dircmp of a folder with itself exits 0" ($LASTEXITCODE -eq 0)
$text = (& $Cli dircmp3 $dirLeft $dirBase $dirRight) -join "`n"
Check "dircmp3 finds 2 conflicts" ($LASTEXITCODE -eq 1 -and $text -match "2 conflict\(s\)")
$mergeTarget = Join-Path $env:TEMP "mergefiles_dirmerge_ancestor"
if (Test-Path $mergeTarget) { Remove-Item $mergeTarget -Recurse -Force }
Copy-Item $dirBase $mergeTarget -Recurse
$text = (& $Cli dirmerge $dirLeft $mergeTarget $dirRight --no-recycle-bin) -join "`n"
$util = Get-Content (Join-Path $mergeTarget "src\util.cpp") -Raw
$ini = Get-Content (Join-Path $mergeTarget "config\app.ini") -Raw
$merged = $LASTEXITCODE -eq 1 -and $text -match "4 item\(s\) taken from one side, 1 file\(s\) merged line by line, 1 conflict" -and
    $util -match "return 10;" -and $util -match "return 20;" -and $ini -match "level = 1" -and
    (Test-Path (Join-Path $mergeTarget "src\new_feature.cpp")) -and -not (Test-Path (Join-Path $mergeTarget "src\old.cpp"))
Check "dirmerge applies one-sided changes, merges util.cpp, leaves app.ini" $merged
Remove-Item $mergeTarget -Recurse -Force

# Binary, image, patch and XML files.
$text = (& $Cli bincmp (Join-Path $Data "binary\data_v1.bin") (Join-Path $Data "binary\data_v3.bin")) -join "`n"
Check "bincmp finds the 8 inserted bytes" ($LASTEXITCODE -eq 1 -and $text -match "0x000003E8 \+0 +0x000003E8 \+8")
$text = (& $Cli imgcmp (Join-Path $Data "images\before.png") (Join-Path $Data "images\after.png") --tolerance 5) -join "`n"
Check "imgcmp with tolerance 5 finds the 900 changed pixels" ($LASTEXITCODE -eq 1 -and $text -match "900 pixel\(s\) differ in 1 area")
$patchTarget = Join-Path $env:TEMP "mergefiles_patch_target"
if (Test-Path $patchTarget) { Remove-Item $patchTarget -Recurse -Force }
Copy-Item (Join-Path $Data "patch\target") $patchTarget -Recurse
& $Cli patch (Join-Path $Data "patch\change.patch") $patchTarget | Out-Null
$ini = Get-Content (Join-Path $patchTarget "src\settings.ini") -Raw
$ok = $LASTEXITCODE -eq 0 -and $ini -match "port = 9090\r\n" -and $ini -match "rotate = daily\r\n" -and (Test-Path (Join-Path $patchTarget "src\readme.txt"))
& $Cli patch (Join-Path $Data "patch\change.patch") $patchTarget --reverse | Out-Null
$restored = [Linq.Enumerable]::SequenceEqual([IO.File]::ReadAllBytes((Join-Path $patchTarget "src\settings.ini")), [IO.File]::ReadAllBytes((Join-Path $Data "patch\target\src\settings.ini")))
Check "patch applies (with an offset), and --reverse restores the file byte for byte" ($ok -and $restored)
Remove-Item $patchTarget -Recurse -Force
$text = (& $Cli xmlfmt (Join-Path $Data "xml\config_b.xml")) -join "`n"
Check "xmlfmt puts one element per line" ($LASTEXITCODE -eq 0 -and $text -match "`n    <user id=`"3`">carol</user>")

# Reports.
$text = (& $Cli report $t1 $t2 --format diff --context 3) -join "`n"
$expected = (& $Cli diff $t1 $t2) -join "`n"
Check "report --format diff matches the diff command" ($LASTEXITCODE -eq 1 -and $text -eq $expected)
$reportFile = Join-Path $env:TEMP "mergefiles_report.html"
& $Cli report $left $base $right --context 2 -o $reportFile | Out-Null
$html = Get-Content $reportFile -Raw -Encoding UTF8
Check "three-way HTML report shows the conflict" ($LASTEXITCODE -eq 0 -and $html -match 'class="t conf"' -and $html -match '/main</span>')
& $Cli dirreport $dirLeft $dirRight --files -o $reportFile | Out-Null
$html = Get-Content $reportFile -Raw -Encoding UTF8
Check "folder report includes file reports" ($LASTEXITCODE -eq 0 -and $html -match 'href="#f' -and $html -match "util.cpp" -and $html -match "new_feature.cpp")
Remove-Item $reportFile

# Archives as folders, and documents as text.
$archives = Join-Path $Data "archives"; $docs = Join-Path $Data "documents"
$text = (& $Cli dircmp (Join-Path $archives "left.zip") (Join-Path $archives "right.tar.gz")) -join "`n"
Check "dircmp of left.zip and right.tar.gz matches the folder comparison" ($LASTEXITCODE -eq 1 -and $text -match "1 identical, 4 different, 2 left only, 0 right only")
$reportFile = Join-Path $env:TEMP "mergefiles_archive_report.html"
& $Cli dirreport (Join-Path $archives "left.zip") (Join-Path $archives "right.tar.gz") --files -o $reportFile | Out-Null
$html = Get-Content $reportFile -Raw -Encoding UTF8
Check "folder report of two archives includes the file reports" ($LASTEXITCODE -eq 0 -and $html -match 'run\(<span class="d">true')
Remove-Item $reportFile
& $Cli dircmp (Join-Path $archives "left.zip") $dirLeft | Out-Null
Check "dircmp of an archive with the folder it was made from exits 0" ($LASTEXITCODE -eq 0)
$text = (& $Cli doctext (Join-Path $docs "report_v2.docx")) -join "`n"
Check "doctext extracts a Word table row" ($LASTEXITCODE -eq 0 -and $text -match "Paper`t45")
$text = (& $Cli diff (Join-Path $docs "budget_v1.xlsx") (Join-Path $docs "budget_v2.xlsx")) -join "`n"
Check "diff compares Excel workbooks by their text" ($LASTEXITCODE -eq 1 -and $text -match "-Paper`t40" -and $text -match "\+Paper`t42")

if ($failed) { Write-Host "$failed check(s) failed" -ForegroundColor Red; exit 1 }
Write-Host "All checks passed" -ForegroundColor Green
exit 0
