[CmdletBinding()]
param(
    [string]$Repository = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path,
    [string]$OutputDirectory,
    [switch]$TestBuild
)

$ErrorActionPreference = 'Stop'
$Repository = (Resolve-Path -LiteralPath $Repository).Path
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $Repository 'out/allowgram-package'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$payload = Join-Path $OutputDirectory ('payload-' + [guid]::NewGuid().ToString('N'))
$executable = Join-Path $Repository 'out/Release/Telegram.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Release executable missing: $executable"
}
$cachePath = Join-Path $Repository 'out/CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    throw 'CMakeCache.txt is required to verify the built client API configuration.'
}
$cache = [IO.File]::ReadAllText($cachePath)
$builtForTest = $cache -match '(?m)^TDESKTOP_API_TEST:BOOL=(ON|TRUE|YES|1)\s*$'
if ($builtForTest -ne [bool]$TestBuild) {
    throw 'Packaging mode does not match the configured client API mode.'
}
if (-not $TestBuild) {
    $apiId = if ($cache -match '(?m)^TDESKTOP_API_ID:STRING=([1-9][0-9]+)\s*$') { $matches[1] } else { '' }
    if (-not $apiId -or $apiId -in '17349', '2040', '611335' -or $cache -notmatch '(?m)^TDESKTOP_API_HASH:STRING=[a-fA-F0-9]{32}\s*$') {
        throw 'Production packaging requires this application''s own Telegram API credentials.'
    }
}
$versionLine = Get-Content -LiteralPath (Join-Path $Repository 'Telegram/build/version') |
    Where-Object { $_ -match '^AppVersionStrSmall\s+' }
$version = ($versionLine -split '\s+')[1]
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw 'Cannot determine an installer version.'
}
$manifestPath = Join-Path $Repository 'SOURCE-MANIFEST.json'
if (-not (Test-Path -LiteralPath (Join-Path $Repository '.git')) -and (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    $commit = (Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json).sourceCommit
} else {
    $commit = (git -C $Repository rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read the source revision.' }
}
if ($commit -notmatch '^[a-fA-F0-9]{40}$') { throw 'The source revision is missing or invalid.' }
New-Item -ItemType Directory -Path $payload | Out-Null
Copy-Item -LiteralPath $executable -Destination (Join-Path $payload 'Allowgram.exe')
foreach ($name in @('LICENSE', 'LEGAL')) {
    Copy-Item -LiteralPath (Join-Path $Repository $name) -Destination $payload
}
$modules = Join-Path $Repository 'out/Release/modules'
if (Test-Path -LiteralPath $modules -PathType Container) {
    Copy-Item -LiteralPath $modules -Destination $payload -Recurse
}
$readme = @'
ALLOWGRAM

Requires Windows 10 version 1809 or later, or Windows 11, on an x64 PC.

Allowgram is an independent modification of Telegram Desktop. Sign in with
your phone number, complete Telegram verification, and configure the required
allowlist with Telegram user and group identifiers before using the client.

Other chats stay readable, but sending is blocked. Calls, mini apps, payments,
and business automation are disabled. Saved Messages requires your own user
ID in the list. Channel comments need the linked discussion group ID.
The list stays fixed until you log out; logging out requires setup again.

The restriction applies to this client. Other Telegram clients and existing
sessions are outside its control. Automatic upstream updates are disabled.

This unsigned installer installs for the current Windows user. Windows may
show an unknown-publisher prompt. Uninstalling preserves account data.

The accompanying Allowgram source archive includes this client's source and
build scripts. Telegram Desktop and its dependencies retain their original
licenses; see LICENSE and LEGAL. Build details are in build-info.json.
'@
if ($TestBuild) {
    $readme = "TEST BUILD - NOT FOR DISTRIBUTION. Telegram test API credentials have login limits.`r`n`r`n" + $readme
}
[IO.File]::WriteAllText((Join-Path $payload 'README.txt'), $readme, [Text.UTF8Encoding]::new($false))
$buildInfo = [ordered]@{
    product = 'Allowgram'
    version = $version
    architecture = 'x64'
    configuration = 'Release'
    sourceCommit = $commit
    apiMode = $(if ($TestBuild) { 'test' } else { 'production' })
    upstream = 'https://github.com/telegramdesktop/tdesktop'
    automaticUpdates = $false
    builtAtUtc = [DateTime]::UtcNow.ToString('o')
}
[IO.File]::WriteAllText((Join-Path $payload 'build-info.json'), ($buildInfo | ConvertTo-Json), [Text.UTF8Encoding]::new($false))

$archiveName = $(if ($TestBuild) { 'Allowgram-Test' } else { 'Allowgram' })
Compress-Archive -Path (Join-Path $payload '*') -DestinationPath (Join-Path $OutputDirectory "$archiveName-$version-x64.zip") -Force
if (-not $TestBuild) {
    $compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    $compilerPath = if ($compiler) { $compiler.Source } else { Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe' }
    if (-not (Test-Path -LiteralPath $compilerPath -PathType Leaf)) {
        throw 'Inno Setup 6 is required to create the Windows installer.'
    }
    & $compilerPath "/DPayloadPath=$payload" "/DOutputPath=$OutputDirectory" "/DAppVersion=$version" (Join-Path $PSScriptRoot 'setup.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
}
$hashes = Get-ChildItem -LiteralPath $OutputDirectory -File |
    Where-Object { $_.Extension -in '.exe', '.zip' } |
    ForEach-Object { "$( (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() )  $($_.Name)" }
[IO.File]::WriteAllLines((Join-Path $OutputDirectory 'SHA256SUMS.txt'), $hashes, [Text.UTF8Encoding]::new($false))
Write-Output "Packaged Allowgram $version ($($buildInfo.apiMode)) in $OutputDirectory"
