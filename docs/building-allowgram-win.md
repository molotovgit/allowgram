# Building Allowgram for Windows

Allowgram requires Windows 10 version 1809 or later, or Windows 11, on an x64
PC. It is a modification of Telegram Desktop. The dedicated
workflow builds the application and its dependencies in Release configuration,
disables upstream automatic updates and crash submission, checks that the
application starts, and packages a per-user installer, a portable zip, the
corresponding repository source, and SHA-256 checksums.

Only this manually dispatched workflow is active. Original upstream workflow
files are retained in `.github/upstream-workflows` so pushing this fork does
not start Telegram's original multi-platform build matrix.

The installer has its own application identifier, installation directory,
shortcuts, and uninstall entry. It does not register the `tg:` URL scheme.
Uninstalling preserves account data. No code-signing certificate is configured;
the resulting executable and installer are unsigned.

## Telegram API credentials

Create an application at <https://my.telegram.org/apps> using Telegram's
[API credential instructions](https://core.telegram.org/api/obtaining_api_id).
Store its `api_id` and `api_hash` as GitHub repository Actions secrets named
`TDESKTOP_API_ID` and `TDESKTOP_API_HASH`. Never commit credentials to source.

The `production` workflow mode requires these secrets and rejects the known
upstream test and public API identifiers. Telegram warns that deploying the
shared test credentials can cause login failures. The `test` mode is available
for compilation and startup checks before credentials are supplied; it produces
a zip explicitly marked as a test build and **does not create an installer**.

## GitHub Actions

1. Put the modified repository and its submodule references in a repository you
   control. Review its visibility before publishing source.
2. Configure the API secrets above for production builds.
3. Run **Build Allowgram for Windows**, selecting `production` or `test`.
4. Download `Allowgram-Windows-x64-production` after a successful run.
5. Install `Allowgram-Setup-<version>-x64.exe` or extract the portable zip.
   Distribute the accompanying `Allowgram-source.zip` and `SHA256SUMS.txt` too.

The workflow uses the standard `windows-2022` runner, MSVC 14.44, Windows SDK
10.0.26100.0, Qt 6, and two concurrent compiler jobs. It has a six-hour job limit
and caches the prepared Release libraries and build tools. It requires at least
35 GiB free on the build volume, with 50 GiB preferred. These are planning
thresholds for this build, not a measured upstream Windows minimum. Upstream
documents approximately 55 GB for its full dual-architecture macOS build and
its Windows workflow removes dependency intermediates before compiling the app.

The standard runner does not guarantee 35 GiB free. Before tests or dependency
preparation, the workflow checks the workspace and C: volumes and may remove
only the four named disposable runner tool directories on C:. If the workspace
is too small but C: has capacity, it copies the complete checkout, including Git
metadata and submodules, into a new `C:\AllowgramBuild\tdesktop` directory.
It preserves the original checkout and verifies the copied commit, submodules,
and remaining disk space. All build, cache, and artifact paths follow the
selected volume; caches are separated by drive. If neither volume provides
35 GiB after source placement, the workflow stops with a capacity error. A first
remote run must still establish this image's available capacity and build peak.

Standard GitHub-hosted runners are free for public repositories. Private
repositories consume the account's included Actions allocation; usage beyond
that allocation can be billed. Confirm the account's quota and spending settings
before running a private build. This workflow neither enables paid runners nor
changes spending limits. Its cache and artifact storage also use account limits.

The dependency wrapper uses the checked-in upstream preparation script and
its pinned revisions. It selects Release commands before the upstream cache
keys are calculated, skips the Debug compilations, and selects Qt's Release
configuration without forced debug symbols. OpenAL retains the upstream
RelWithDebInfo configuration expected by the application build. Build workers
are capped at two across CMake, Ninja, Meson, MSBuild, make, and Cargo; TD's
nested multiprocess compilation is disabled to stay within that cap. Other
upstream dependency flags remain unchanged. It does not download prebuilt
client binaries.

Before the application build, the workflow compiles and runs parser regression
tests, audits the current Telegram schema, and tests the actual transport guard
against serialized Telegram requests. The isolated guard test uses an official
prebuilt Qt Base 6.8.3 runtime downloaded by aqtinstall; it is not included in the
client. The client itself links the upstream patched Qt 6.11.2 source build.

## Local build

Use a Windows x64 Visual Studio developer terminal with compiler 14.44 and SDK
10.0.26100.0. Python, CMake, Ninja, Git, and Inno Setup 6 must be available.
The repository's parent contains `Libraries` and `ThirdParty`; place the whole
build on a drive with sufficient free space.

From the repository root:

```powershell
$env:CMAKE_BUILD_PARALLEL_LEVEL = '2'
$env:NUMBER_OF_PROCESSORS = '2'
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

The API values in the example come from environment variables that you supply
privately. They are compiled into the client, as required by Telegram Desktop;
the build information JSON does not include them.

Source archives include `SOURCE-MANIFEST.json` with the source revision and
complete file list. The packaging scripts can use an extracted source archive
without a `.git` directory. When archiving a Git checkout, commit all source
changes first; the archiver rejects dirty or incomplete source trees.

To inspect dependency commands without downloading or compiling libraries:

```powershell
python Telegram/build/allowgram/prepare_release.py --print-only
```

The startup check uses a fresh temporary account directory, without signing
into a real account or sending messages. Login, allowlist setup, persistence,
and allowed/blocked messaging require an interactive acceptance check with
the built client and a real Telegram account.
