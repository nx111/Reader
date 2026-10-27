param([ValidateSet('x64','x86')][string]$Architecture='x64')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$work=Join-Path $env:RUNNER_TEMP "reader-pdf-native-$Architecture"
New-Item -ItemType Directory -Force $work | Out-Null
$fixture=Join-Path $work 'fixture.pdf'
# The fixture generator uses only the Python standard library. It creates one
# text page and one scanned-image page, with metadata and two local outlines.
& python -c "import sys; from pathlib import Path; sys.path.insert(0,sys.argv[1]); from test_pdf_engine import fixture; fixture(Path(sys.argv[2]))" $PSScriptRoot $fixture
if ($LASTEXITCODE -ne 0) { throw 'PDF fixture creation failed' }
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'MSVC tools are unavailable' }
$developer=Join-Path $installation 'Common7/Tools/VsDevCmd.bat'
$executable=Join-Path $work 'pdf-native-test.exe'
$command="call `"$developer`" -no_logo -arch=$Architecture -host_arch=x64 && cl /nologo /EHsc /std:c++14 /MT /I`"$root/Reader`" /I`"$root/opensrc/pdfium/include`" `"$root/Reader/PdfEngine.cpp`" `"$PSScriptRoot/pdf_engine_test.cpp`" /Fe:`"$executable`""
Push-Location $work
try {
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Native PDF test compilation failed' }
    $library=Join-Path $work 'pdfium.dll'
    Copy-Item -LiteralPath (Join-Path $root ".deps/pdfium/$Architecture/pdfium.dll") -Destination $library
    foreach ($case in @('text','scan','cancel','bounds','repeat')) {
        & $executable $case $fixture $library
        if ($LASTEXITCODE -ne 0) { throw "Native PDF $case failed" }
    }
    $invalid=Join-Path $work 'invalid.pdf'
    [IO.File]::WriteAllText($invalid,'%PDF broken fixture')
    & $executable invalid $invalid $library
    if ($LASTEXITCODE -ne 0) { throw 'Native PDF malformed-file check failed' }
    & $executable missing $fixture (Join-Path $work 'missing.dll')
    if ($LASTEXITCODE -ne 0) { throw 'Native PDF missing-component check failed' }
    Write-Host "Native PDF core: 7 checks passed ($Architecture). Windows GUI was not tested."
} finally { Pop-Location }
