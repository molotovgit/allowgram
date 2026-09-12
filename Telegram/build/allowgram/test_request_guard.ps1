[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$QtDirectory,
    [string]$Repository = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
)

$ErrorActionPreference = 'Stop'
$Repository = (Resolve-Path -LiteralPath $Repository).Path
$QtDirectory = (Resolve-Path -LiteralPath $QtDirectory).Path
$build = Join-Path $Repository 'out/allowgram-guard-tests'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$scheme = Join-Path $build 'scheme'
python (Join-Path $Repository 'Telegram/SourceFiles/codegen/scheme/codegen_scheme.py') "-o$scheme" `
    (Join-Path $Repository 'Telegram/SourceFiles/mtproto/scheme/api.tl') `
    (Join-Path $Repository 'Telegram/SourceFiles/mtproto/scheme/mtproto.tl')
if ($LASTEXITCODE -ne 0) { throw 'Schema generation failed.' }

$includes = @(
    "$QtDirectory/include", "$QtDirectory/include/QtCore", $build,
    "$Repository/Telegram/SourceFiles", "$Repository/Telegram/lib_base",
    "$Repository/Telegram/lib_tl", "$Repository/Telegram/lib_crl", "$Repository/Telegram/lib_rpl",
    "$Repository/Telegram/ThirdParty/GSL/include",
    "$Repository/Telegram/ThirdParty/range-v3/include"
) | ForEach-Object { "/I$_" }
$sources = @(
    "$Repository/Telegram/SourceFiles/test/allowlist_request_guard_test.cpp",
    "$Repository/Telegram/SourceFiles/mtproto/allowlist_request_guard.cpp",
    "$scheme.cpp",
    "$Repository/Telegram/SourceFiles/mtproto/details/mtproto_serialized_request.cpp",
    "$Repository/Telegram/lib_tl/tl/tl_basic_types.cpp",
    "$Repository/Telegram/lib_crl/crl/crl_time.cpp",
    "$Repository/Telegram/lib_crl/crl/winapi/crl_winapi_time.cpp"
)
Push-Location -LiteralPath $build
try {
    foreach ($source in $sources) {
        $forcedIncludes = @()
        if ([IO.Path]::GetFileName($source) -eq 'mtproto_serialized_request.cpp') {
            $forcedIncludes = @('/FIscheme.h')
        }
        cl.exe /nologo /c /std:c++20 /EHsc /MD /O1 /Gy /bigobj /Zc:__cplusplus /permissive- /utf-8 /W3 /DQT_NO_DEBUG @includes @forcedIncludes $source
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $source" }
    }
    $objects = $sources | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_) + '.obj' }
    link.exe /nologo /OUT:allowlist_request_guard_test.exe /OPT:REF @objects "$QtDirectory/lib/Qt6Core.lib"
    if ($LASTEXITCODE -ne 0) { throw 'Request guard regression test linking failed.' }
    $previousPath = $env:PATH
    try {
        $env:PATH = "$QtDirectory/bin;$env:PATH"
        & ./allowlist_request_guard_test.exe
        if ($LASTEXITCODE -ne 0) { throw 'Request guard regression checks failed.' }
    } finally {
        $env:PATH = $previousPath
    }
} finally {
    Pop-Location
}
