param([ValidateSet('x64','x86')][string]$Architecture='x64')
$ErrorActionPreference='Stop'
$repository='bblanchon/pdfium-binaries'
$tag='chromium/8125'
$commit='4bdb7acd45b39ef6a7fc2a5b4c83f0398e45c9a6'
$root=Split-Path $PSScriptRoot -Parent
$lock=Get-Content -LiteralPath (Join-Path $root 'opensrc/pdfium/dependency.json') -Raw | ConvertFrom-Json
$expected=$lock."win-$Architecture"
$assetName="pdfium-win-$Architecture.tgz"
$release=Invoke-RestMethod "https://api.github.com/repos/$repository/releases/tags/chromium%2F8125" -Headers @{'User-Agent'='Reader-dependency-check'}
if ($release.tag_name -ne $tag -or $release.target_commitish -ne $commit -or !$release.immutable) { throw 'Unexpected PDFium release identity' }
$asset=$release.assets | Where-Object name -EQ $assetName
if ($asset.size -ne $expected.archive_bytes -or $asset.digest -ne "sha256:$($expected.archive_sha256)") { throw 'PDFium release metadata differs from dependency lock' }
$destination=Join-Path $root ".deps/pdfium/$Architecture"
New-Item -ItemType Directory -Force $destination | Out-Null
$stage=Join-Path ([IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString())
New-Item -ItemType Directory $stage | Out-Null
try {
    $archive=Join-Path $stage $assetName
    Invoke-WebRequest $asset.browser_download_url -OutFile $archive
    if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected.archive_sha256) { throw 'PDFium checksum mismatch; refusing extraction or execution' }
    $bundle=Join-Path $stage 'pdfium-attestation.json'
    Invoke-WebRequest "https://github.com/$repository/releases/download/$tag/pdfium-attestation.json" -OutFile $bundle
    if ((Get-FileHash $bundle -Algorithm SHA256).Hash.ToLowerInvariant() -ne $lock.attestation_sha256) { throw 'PDFium attestation checksum mismatch' }
    # A current GitHub CLI is required for Sigstore verification, including the
    # signed workflow identity, source revision and transparency-log proof.
    & gh attestation verify $archive --bundle $bundle --repo $repository --signer-workflow "$repository/.github/workflows/build-all.yml" --source-digest $commit --deny-self-hosted-runners --custom-trusted-root (Join-Path $root 'opensrc/pdfium/trusted_root.jsonl')
    if ($LASTEXITCODE -ne 0) { throw 'PDFium provenance verification failed; nothing installed' }
    & tar -xzf $archive -C $stage
    if ($LASTEXITCODE -ne 0) { throw 'PDFium extraction failed' }
    $arguments=Get-Content (Join-Path $stage 'args.gn') -Raw
    if ($arguments -notmatch 'pdf_enable_v8 = false' -or $arguments -notmatch 'pdf_enable_xfa = false') { throw 'PDFium must disable V8 and XFA' }
    $library=Join-Path $stage 'bin/pdfium.dll'
    if ((Get-FileHash $library -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected.library_sha256) { throw 'PDFium DLL checksum mismatch' }
    if (!(Test-Path (Join-Path $stage 'LICENSE')) -or !(Test-Path (Join-Path $stage 'licenses/pdfium.txt'))) { throw 'Missing PDFium redistribution notices' }
    Copy-Item $library (Join-Path $destination 'pdfium.dll') -Force
    Copy-Item (Join-Path $stage 'LICENSE') (Join-Path $destination 'LICENSE') -Force
    Copy-Item (Join-Path $stage 'licenses') $destination -Recurse -Force
    Write-Host "Verified app-local PDFium $tag $Architecture; $($expected.library_bytes) DLL bytes"
} finally { Remove-Item $stage -Recurse -Force }
