param(
    [Parameter(Mandatory=$true)][string]$Archive,
    [ValidateSet('x64','x86')][string]$Architecture='x64'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$archivePath=(Resolve-Path -LiteralPath $Archive).Path
$work=Join-Path $env:RUNNER_TEMP ("reader-package-check-"+[guid]::NewGuid().ToString())
New-Item -ItemType Directory $work | Out-Null
function Get-PeMachine([string]$Path) {
    $bytes=[IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 64 -or $bytes[0] -ne 77 -or $bytes[1] -ne 90) { throw "Invalid PE: $Path" }
    $offset=[BitConverter]::ToInt32($bytes,60)
    if ($offset -lt 0 -or $offset+6 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes,$offset) -ne 0x4550) { throw "Invalid PE header: $Path" }
    return [BitConverter]::ToUInt16($bytes,$offset+4)
}
try {
    & (Join-Path $root 'tool/7z.exe') t $archivePath
    if ($LASTEXITCODE -ne 0) { throw 'Package integrity test failed' }
    & (Join-Path $root 'tool/7z.exe') x -y $archivePath "-o$work"
    if ($LASTEXITCODE -ne 0) { throw 'Package extraction failed' }
    $executable=Join-Path $work 'Reader.exe'
    $library=Join-Path $work 'pdfium.dll'
    $expectedMachine=if ($Architecture -eq 'x64') {0x8664} else {0x14c}
    if ((Get-PeMachine $executable) -ne $expectedMachine -or (Get-PeMachine $library) -ne $expectedMachine) { throw 'EXE/DLL architecture mismatch' }
    $lock=Get-Content -LiteralPath (Join-Path $root 'opensrc/pdfium/dependency.json') -Raw | ConvertFrom-Json
    if ((Get-FileHash $library -Algorithm SHA256).Hash.ToLowerInvariant() -ne $lock."win-$Architecture".library_sha256) { throw 'Packaged PDFium hash mismatch' }
    $legal=Join-Path $work 'licenses/pdfium'
    foreach ($name in @('LICENSE','NOTICE.txt','dependency.json')) {
        $original=Join-Path $root "opensrc/pdfium/$name"
        $packaged=Join-Path $legal $name
        if ((Get-FileHash $original).Hash -ne (Get-FileHash $packaged).Hash) { throw "Missing/changed packaged $name" }
    }
    $notices=Get-ChildItem -LiteralPath (Join-Path $root 'opensrc/pdfium/licenses') -File
    foreach ($notice in $notices) {
        $packaged=Join-Path $legal "licenses/$($notice.Name)"
        if ((Get-FileHash $notice.FullName).Hash -ne (Get-FileHash $packaged).Hash) { throw "Missing/changed third-party notice $($notice.Name)" }
    }
    if (!(Test-Path -LiteralPath (Join-Path $work 'readme.txt'))) { throw 'Missing Reader readme' }
    $files=Get-ChildItem -LiteralPath $work -Recurse -File
    $total=($files | Measure-Object -Property Length -Sum).Sum
    Write-Host "Verified package: arch=$Architecture; archive_bytes=$((Get-Item $archivePath).Length); files=$($files.Count); unpacked_bytes=$total; exe_bytes=$((Get-Item $executable).Length); dll_bytes=$((Get-Item $library).Length); notices=$($notices.Count)"
} finally { Remove-Item -LiteralPath $work -Recurse -Force }
