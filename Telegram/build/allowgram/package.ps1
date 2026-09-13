[CmdletBinding()]
param(
    [string]$Repository,
    [string]$OutputDirectory,
    [switch]$TestBuild
)

$ErrorActionPreference = 'Stop'

function Read-SingleMatch([string]$Text, [string]$Pattern, [string]$Description) {
    $matches = [regex]::Matches($Text, $Pattern, [Text.RegularExpressions.RegexOptions]::Multiline)
    if ($matches.Count -ne 1) {
        throw "Cannot determine $Description."
    }
    return $matches[0].Groups[1].Value
}

function Get-VersionParts([string]$Version) {
    if ($Version -notmatch '^([1-9][0-9]{0,2})\.([0-9]{1,3})\.([0-9]{1,3})\.([1-9][0-9]{0,4})$') {
        throw 'Allowgram package version must be major.minor.patch.sequence.'
    }
    $major = [int]$matches[1]
    $minor = [int]$matches[2]
    $patch = [int]$matches[3]
    $sequence = [int]$matches[4]
    if ($minor -gt 999 -or $patch -gt 999 -or $sequence -gt 65535) {
        throw 'Allowgram package version is outside the supported update-version bounds.'
    }
    return [ordered]@{
        major = $major
        minor = $minor
        patch = $patch
        sequence = $sequence
        base = $major * 1000000 + $minor * 1000 + $patch
    }
}

function Assert-NewOutputFile([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        throw "Refusing to overwrite existing package artifact: $Path"
    }
}

if (-not $Repository) {
    $Repository = Join-Path $PSScriptRoot '../../..'
}
$Repository = (Resolve-Path -LiteralPath $Repository).Path

$executable = Join-Path $Repository 'out/Release/Telegram.exe'
$updater = Join-Path $Repository 'out/Release/Updater.exe'
foreach ($required in @($executable, $updater)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Release executable missing: $required"
    }
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

if (Test-Path -LiteralPath (Join-Path $Repository '.git')) {
    $sourceStatus = git -C $Repository status --porcelain --untracked-files=all
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read the source status.' }
    if ($sourceStatus) { throw 'Commit source changes before packaging Allowgram.' }
}

$versionFile = [IO.File]::ReadAllText((Join-Path $Repository 'Telegram/build/version'))
$appVersion = [int](Read-SingleMatch $versionFile '^AppVersion\s+([0-9]+)\s*$' 'the upstream base app version')
$version = Read-SingleMatch $versionFile '^AppVersionStrSmall\s+([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)\s*$' 'the Allowgram display version'
$versionParts = Get-VersionParts $version
if ($versionParts.base -ne $appVersion) {
    throw 'Allowgram display version does not match AppVersion base.'
}

$coreVersion = [IO.File]::ReadAllText((Join-Path $Repository 'Telegram/SourceFiles/core/version.h'))
$coreAppVersion = [int](Read-SingleMatch $coreVersion 'constexpr auto AppVersion = ([0-9]+);' 'core AppVersion')
$coreDisplay = Read-SingleMatch $coreVersion 'constexpr auto AppVersionStr = "([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)";' 'core display version'
$coreSequence = [int](Read-SingleMatch $coreVersion 'constexpr auto AllowgramUpdateSequence = ([0-9]+);' 'core Allowgram update sequence')
if ($coreAppVersion -ne $appVersion -or $coreDisplay -ne $version -or $coreSequence -ne $versionParts.sequence) {
    throw 'Core Allowgram version constants do not match Telegram/build/version.'
}
$cacheSequence = [int](Read-SingleMatch $cache '^TDESKTOP_ALLOWGRAM_UPDATE_SEQUENCE:STRING=([0-9]+)\s*$' 'configured Allowgram update sequence')
if ($cacheSequence -ne $versionParts.sequence) {
    throw 'Built client update sequence does not match the package version.'
}

$versionInfo = (Get-Item -LiteralPath $executable).VersionInfo
if ($versionInfo.ProductName -ne 'Allowgram' -or $versionInfo.FileVersion -ne $version -or $versionInfo.ProductVersion -ne $version) {
    throw 'Release executable PE metadata does not match the Allowgram package version.'
}

$outputDirectorySpecified = -not [string]::IsNullOrWhiteSpace($OutputDirectory)
if ($outputDirectorySpecified) {
    $OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
} else {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $OutputDirectory = Join-Path $Repository "out/allowgram-package-$version-$stamp"
}
if (Test-Path -LiteralPath $OutputDirectory -PathType Container) {
    if (Get-ChildItem -LiteralPath $OutputDirectory -Force | Select-Object -First 1) {
        throw "Package output directory must be new or empty: $OutputDirectory"
    }
} else {
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
}
$payload = Join-Path $OutputDirectory ('payload-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $payload | Out-Null

$commit = $null
$manifestPath = Join-Path $Repository 'SOURCE-MANIFEST.json'
if (-not (Test-Path -LiteralPath (Join-Path $Repository '.git')) -and (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    $commit = (Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json).sourceCommit
} else {
    $commit = (git -C $Repository rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read the source revision.' }
}
if ($commit -notmatch '^[a-fA-F0-9]{40}$') { throw 'The source revision is missing or invalid.' }

Copy-Item -LiteralPath $executable -Destination (Join-Path $payload 'Allowgram.exe')
Copy-Item -LiteralPath $updater -Destination (Join-Path $payload 'AllowgramUpdater.exe')
foreach ($name in @('LICENSE', 'LEGAL')) {
    Copy-Item -LiteralPath (Join-Path $Repository $name) -Destination $payload
}
$modules = Join-Path $Repository 'out/Release/modules'
if (Test-Path -LiteralPath $modules -PathType Container) {
    Copy-Item -LiteralPath $modules -Destination $payload -Recurse
}

$updateAsset = "allowgram-update-stable-win-x64-$version.tdup"
$readme = @"
ALLOWGRAM

Requires Windows 10 version 1903 or later, or Windows 11, on an x64 PC.

Allowgram is an independent modification of Telegram Desktop. Sign in with
your phone number, complete Telegram verification, and configure the required
allowlist with Telegram user and group identifiers before using the client.

Only allowed conversations appear in the chat list, archive, search and
notifications. Excluded conversations produce no message previews, unread
badges, sounds or incoming-call alerts. Incoming and outgoing private voice/video
calls require an explicitly allowed, known nonbot user. Use the eligible private
chat's call button (right-click for voice/video). Group permission does not permit
calls with its members. Calls history, group/conference calls, aggregate stories,
payments and business automation are disabled. Creating groups/channels,
Saved Messages, all user profile views (including My Profile) and adding
another account are disabled. Contacts, Settings and existing allowed
conversations remain available. Emoji (including typed or
pasted Unicode), stickers, GIFs and message effects cannot be sent. Ordinary
text and nonanimated attachments remain available; opaque media is refused.
Channel comments need the linked discussion group ID.

Mini Apps work only for server-resolved bots explicitly included under Users.
Their conversation, reply and send-as contexts must also be allowed. App links
resolve and check each destination bot independently. Native Telegram consent,
authentication and origin checks remain enabled. Accepting app Terms does not
grant write access; permission requests require separate confirmation.
Opaque app IDs, arbitrary custom bridge methods/cloud storage, prepared-message
sharing, chat/contact chooser bridges, managed-bot creation and emoji-status
changes remain disabled. Third-party dashboards retain their backend permissions.

Each setup section has a + button to add another ID row and a Remove button
to delete an unwanted row. Up to 10,000 distinct IDs are supported.
The list stays fixed until you log out; logging out requires setup again.

The restriction applies to this client. Other Telegram clients and existing
sessions are outside its control. Official Telegram updates are disabled;
signed Allowgram stable updates are enabled and wait for your restart approval.

This unsigned installer installs for the current Windows user. Windows may
show an unknown-publisher prompt. Secure Allowgram update signatures are separate
from paid Windows publisher certificates and do not remove SmartScreen warnings.
Uninstalling preserves account data. Installed account data is saved in
%APPDATA%\Allowgram. The portable zip keeps its account data in
AllowgramForcePortable beside Allowgram.exe.

The accompanying Allowgram source archive includes this client's source and
build scripts. Telegram Desktop and its dependencies retain their original
licenses; see LICENSE and LEGAL. Build details are in build-info.json.
"@
if ($TestBuild) {
    $readme = "TEST BUILD - NOT FOR DISTRIBUTION. Telegram test API credentials have login limits.`r`n`r`n" + $readme
}
[IO.File]::WriteAllText((Join-Path $payload 'README.txt'), $readme, [Text.UTF8Encoding]::new($false))
$buildInfo = [ordered]@{
    product = 'Allowgram'
    version = $version
    baseVersion = $appVersion
    updateSequence = $versionParts.sequence
    architecture = 'x64'
    configuration = 'Release'
    sourceCommit = $commit
    apiMode = $(if ($TestBuild) { 'test' } else { 'production' })
    upstream = 'https://github.com/telegramdesktop/tdesktop'
    automaticUpdates = $true
    updateFeed = 'https://github.com/molotovgit/allowgram/releases/latest/download/allowgram-update-feed.json'
    updateAsset = $updateAsset
    updateTrust = 'Allowgram production root and root-signed stable manifest'
    authenticodeSigned = $false
    builtAtUtc = [DateTime]::UtcNow.ToString('o')
}
[IO.File]::WriteAllText((Join-Path $payload 'build-info.json'), ($buildInfo | ConvertTo-Json), [Text.UTF8Encoding]::new($false))

$archiveName = $(if ($TestBuild) { 'Allowgram-Test' } else { 'Allowgram' })
$portableDirectory = Join-Path $payload 'AllowgramForcePortable'
New-Item -ItemType Directory -Path $portableDirectory | Out-Null
$portableArchive = Join-Path $OutputDirectory "$archiveName-$version-x64.zip"
Assert-NewOutputFile $portableArchive
Compress-Archive -Path (Join-Path $payload '*') -DestinationPath $portableArchive
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::Open($portableArchive, [IO.Compression.ZipArchiveMode]::Update)
try {
    if (-not $zip.GetEntry('AllowgramForcePortable/')) {
        $zip.CreateEntry('AllowgramForcePortable/') | Out-Null
    }
} finally {
    $zip.Dispose()
}
if (-not $TestBuild) {
    $taskLocalInno = Join-Path (Split-Path $Repository) 'allowgram-tools\InnoSetup'
    if (Test-Path -LiteralPath (Join-Path $taskLocalInno 'ISCC.exe') -PathType Leaf) {
        $env:PATH = $taskLocalInno + ';' + $env:PATH
    }
    $compiler = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    $compilerPath = if ($compiler) { $compiler.Source } else { Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe' }
    if (-not (Test-Path -LiteralPath $compilerPath -PathType Leaf)) {
        throw 'Inno Setup 6 is required to create the Windows installer.'
    }
    Assert-NewOutputFile (Join-Path $OutputDirectory "Allowgram-Setup-$version-x64.exe")
    & $compilerPath "/DPayloadPath=$payload" "/DOutputPath=$OutputDirectory" "/DAppVersion=$version" (Join-Path $PSScriptRoot 'setup.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
}

$sourceArchive = Join-Path $OutputDirectory 'Allowgram-source.zip'
Assert-NewOutputFile $sourceArchive
python (Join-Path $PSScriptRoot 'archive_source.py') $sourceArchive
if ($LASTEXITCODE -ne 0) { throw 'Source archiving failed.' }

$hashes = Get-ChildItem -LiteralPath $OutputDirectory -File |
    Where-Object { $_.Extension -in '.exe', '.zip', '.tdup' } |
    Sort-Object Name |
    ForEach-Object { "$( (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() )  $($_.Name)" }
[IO.File]::WriteAllLines((Join-Path $OutputDirectory 'SHA256SUMS.txt'), $hashes, [Text.UTF8Encoding]::new($false))
Write-Output "Packaged Allowgram $version ($($buildInfo.apiMode)) in $OutputDirectory"
