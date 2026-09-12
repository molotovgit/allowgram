# Build Allowgram for Windows

[Back to Allowgram](../../README.md)

The application source contains the 7.2.8.6 feature removals and earlier hardening changes. Build and package acceptance is recorded separately for each exact source commit. This publication has not run a GitHub-hosted build. The [manual workflow](../../.github/workflows/allowgram-windows.yml) is opt-in and does not publish a GitHub Release automatically.

## Checkout and prerequisites

```powershell
git clone --recurse-submodules https://github.com/molotovgit/allowgram.git
cd allowgram
git submodule update --init --recursive
```

Keep the pinned submodule commits. Do not update them to arbitrary branch tips. Use an x64 Visual Studio developer terminal and the repository's sibling-directory layout: the preparation script places `Libraries` and `ThirdParty` beside the repository.

| Component | Release validation / requirement |
| --- | --- |
| OS / target | Windows x64; installer minimum Windows 10 1903 |
| Compiler | Visual Studio 2022 MSVC 14.44, v143 C++ ATL headers/libraries |
| Windows SDK | 10.0.26100.0 |
| Client Qt | Upstream patched Qt 6.11.2, built from the pinned preparation sources |
| Focused-test Qt | Qt Base MSVC x64; the test helper also supports the production static Qt 6.11.2 |
| Packaging | Inno Setup 6 (`ISCC.exe` on PATH) |
| Build tools | Python 3, CMake, Ninja and Git; dependency versions are pinned in upstream preparation and this workflow |

Allow substantial free disk space. The workflow checks for at least 35 GiB on its build volume and prefers 50 GiB; these are planning thresholds, not a guarantee of a fresh dependency build's peak usage. The six-hour hosted-job limit and runner capacity can still be limiting. This publication does not change billing settings or run that workflow.

## Your own Telegram API application

Create your own app at [my.telegram.org/apps](https://my.telegram.org/apps). Supply its API ID and hash privately as `TDESKTOP_API_ID` and `TDESKTOP_API_HASH` in the build process environment. Never commit the values or paste configure output, CMakeCache, compiler command lines or build logs into public issues. CMake and compiled binaries can contain these values. The owner used private app credentials for the delivered build; they are not part of this repository.

If you intentionally run the manual GitHub workflow later, configure repository Actions secrets with those same names. Do not use upstream demonstration or open-build credentials for your production app. The optional `test` mode creates a marked test ZIP and no production installer.

## Local Release build

From the repository root, with the required toolchain and private environment values already set:

```powershell
$env:CMAKE_BUILD_PARALLEL_LEVEL = '2'
$env:ALLOWGRAM_BUILD_JOBS = '2'
python Telegram/build/allowgram/prepare_release.py
python Telegram/configure.py -G 'Ninja Multi-Config' qt6 `
  -D CMAKE_CONFIGURATION_TYPES=Release `
  -D CMAKE_MSVC_DEBUG_INFORMATION_FORMAT= `
  -D DESKTOP_APP_DISABLE_AUTOUPDATE=ON `
  -D DESKTOP_APP_DISABLE_CRASH_REPORTS=ON `
  -D TDESKTOP_API_TEST=OFF `
  -D "TDESKTOP_API_ID=$env:TDESKTOP_API_ID" `
  -D "TDESKTOP_API_HASH=$env:TDESKTOP_API_HASH"
cmake --build out --config Release --target Telegram --parallel 2
python Telegram/build/allowgram/archive_source.py out/allowgram-package/Allowgram-source.zip
& Telegram/build/allowgram/package.ps1
```

The source archiver requires a clean committed tree and complete submodules. Packaging requires a production-configured Release executable and creates an installer, portable ZIP, corresponding source archive and checksum manifest. The installer remains unsigned unless a separately authorized signing process is added. Do not distribute the entire `out` directory, private API JSON, sessions or build caches.

The [existing detailed build notes](../building-allowgram-win.md) describe the Release dependency wrapper, compiler concurrency, runner disk handling and cache behavior. Upstream [build guidance and notices](../../README.telegram.md) remain available for provenance; only the stated Windows configuration has Allowgram release validation.

Run the [focused regression checks](testing.md) before packaging code changes. Do not use an existing signed-in profile for automated startup tests.
