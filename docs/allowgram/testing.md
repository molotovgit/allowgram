# Testing and verification

[Back to Allowgram](../../README.md)

## 7.2.8.5 hardening checks

Run `python Telegram/build/allowgram/check_emoji_data.py` to verify that the
production scalar table matches the pinned Telegram sequence source. The native
request suite decodes real serialized requests and tests every listed emoji
sequence, custom entities, malformed UTF-8, embedded NUL, valid photo captions,
mixed albums, cached documents/forwards, upload prefixes, reactions and effects.
Positive controls include numeric IDs, Uzbek/Cyrillic/CJK, ordinary documents,
photos/videos and allowed-bot Mini App gates. Parser tests also exercise the
production profile-presentation and initial-login permission functions.

These are no-network policy tests, not signed-in navigation or sending E2E.
Consult the private revision-specific report for actual commands, RED/GREEN
receipts, native UI/build/startup/package results and untested routes. Historical
7.2.8.4 counts below are not evidence for the new restrictions.

## Repeat the native form regression

For the synthetic hardening fixture, build with `--hardening` and run
`python Telegram/build/allowgram/test_native_hardening.py --executable <fixture>/Allowgram-Docs.exe --output <fresh-results>`.
This separate executable uses production account, navigation, media-viewer and
composer components with synthetic data. Disposable overlays prevent MTProto
connections and request delivery; they are not included in the shipped client.
The native runners require noninteractive Windows session 0 to avoid affecting
the owner's desktop. They direct no OS keys and take no screenshots. Fixture
timeouts or nonzero teardown exits are failures, regardless of check counts.

After building the Windows client, use the same x64 MSVC environment to build
a separate test executable. The configured Ninja Release objects and generated
styles must match the checked-out application source.

```powershell
python Telegram/build/allowgram/build_ui_regression.py --repository . --output out/ui-regression
python Telegram/build/allowgram/test_ui_layout.py --executable out/ui-regression/Allowgram-Docs.exe --output out/ui-results
```

The result directory must be new. The runner uses a fresh unsigned-in profile
for every case and exits nonzero on failed geometry or interaction checks. It
tests the actual `AllowlistLockWidget` and `Ui::InputField`, including the
`QTextEdit` document, viewport and caret. A working upstream default field is
measured as a control. This is not a source-string or screenshot fixture test.

The disposable overlay only exposes the existing form before sign-in, supplies
example values, invokes the real parser without saving, selects the process-local
style scale and includes the test body. It does not alter field styles, row
geometry, fonts, layout or interaction handlers. It suppresses OS registration
from this test process. The normal release executable is hashed before and
after linking and must remain unchanged.

The matrix uses Telegram style scales 100%, 125%, 150% and 200% with minimum
380x480, compact 800x598 and desktop 1100x800 logical base sizes, multiplied by
the requested scale. It records the actual style scale, widget dimensions and
Qt device pixel ratio. Windows display settings are not changed. This checks
application scaling; it does not claim cross-monitor DPI migration coverage.

Geometry and event checks cover empty, focused, filled and invalid fields;
long, negative and prefixed numeric values; selection; Remove alignment and
containment; plus and Remove callbacks; last-row protection; Enter and Tab;
repeated-row spacing; and scrolling to a new row, wrapped errors and Save.
Pixel-level label, focus and text appearance still needs native visual review.

## Recorded 7.2.8.4 UI results

The unfixed 100% form gave a 19px text document only 12px of viewport height.
Remove was centered 12px above the editable region. The real widget failed
27 geometry checks in the minimum-size baseline.

The corrected form passes **1,284 checks in 12 cases**, covering all four
requested application scales and three window sizes. Every final process
exited normally with code zero. The native request/message/Mini App suite
also passed 451 checks, the parser 80 checks and the schema audit six tests.
See [measured UI results](ui-layout-results.json) for dimensions, text and
caret measurements, the exact baseline and the isolated-test scope.

The early test harness exposed a teardown error from a parented stack control;
the test now uses normal Qt ownership. The 150% caret check also caught a
one-pixel rounding edge, fixed in the production row with a document inset
based on its native border width. Neither failure was waived to get a pass.

The rebuilt Windows x64 executable passed fresh startup. The same payload passed
isolated installation/startup/uninstallation under a distinct validation AppId,
and the portable ZIP passed startup using its own account-directory marker.
The source archive contains 15,748 manifest members; archive integrity, private
API exclusion, payload equality and all three distribution checksums passed.
The installed owner app and delivered 7.2.8.3 files retained their hashes.
See [release verification](verification-results.json).

## Historical 7.2.8.3 results

These are local Windows release results, not a claim that GitHub Actions has run:

| Check | Recorded result |
| --- | --- |
| ID parser (MSVC 14.44, warnings as errors) | 80 checks passed |
| Serialized request, incoming-message and Mini App authorization | 451 checks, 0 failures |
| Pinned Telegram schema audit | 6 tests passed |
| Windows x64 Release, MSVC 14.44 / patched Qt 6.11.2 | Configure and build exited 0 |
| Fresh isolated startup | Process alive after 15 seconds; local storage initialized; no fatal log error |
| Portable startup | Passed using its own portable marker/account directory |
| Isolated install/start/uninstall | Passed with the same payload and a separate validation AppId; existing installation and account data preserved |
| Source/portable/installer integrity | Source manifest and archive reads, private API exclusion, portable/build equality and SHA-256 checks passed |

The owner subsequently reported that the delivered 7.2.8.3 installer worked. Record this as **owner-reported live success**. No independent per-button dashboard, excluded-bot rejection, phone-sharing or write-permission outcome is claimed. Automated validation did not sign in, copy/reset the owner's session, change a saved list, send live messages or mutate a dashboard.

## Repeat focused tests

Use an x64 Visual Studio developer terminal. For the parser:

```powershell
New-Item -ItemType Directory -Force out/allowgram-policy-tests | Out-Null
cl.exe /nologo /std:c++20 /EHsc /utf-8 /W4 /WX /ITelegram/SourceFiles `
  Telegram/SourceFiles/main/allowlist_policy.cpp `
  Telegram/SourceFiles/test/allowlist_policy_test.cpp `
  /Foout/allowgram-policy-tests/ /Fe:out/allowgram-policy-tests/allowlist_policy_test.exe
& ./out/allowgram-policy-tests/allowlist_policy_test.exe
python Telegram/SourceFiles/test/allowlist_request_schema_test.py
```

Pass the installed matching Qt directory. The helper also supports static Qt;
the local Windows build uses the existing Qt 6.11.2 installation, without
rebuilding dependencies. For example, with a Qt 6.8.3 shared installation:

```powershell
& Telegram/build/allowgram/test_request_guard.ps1 -QtDirectory 'C:\Qt\6.8.3\msvc2022_64'
```

This harness generates the pinned TL schema, serializes actual request constructors and calls the production guards. It covers two allowed 64-bit bots, excluded/unknown/non-bot owners, allowed groups with excluded bots, from-message and reply/send-as context, mismatched/opaque apps, account/policy changes and unsafe bridge links. Existing incoming-message and outgoing-chat checks remain in the suite.

## Safe live acceptance

Use a dedicated authorized account/profile with a deliberately chosen test list. Exercise only agreed actions. Do not log out another person, copy their session or change their saved policy. Verify allowed and excluded conversation visibility and notifications, supported sends only to test recipients, allowed-bot app loading, excluded-bot links and account switching. Obtain explicit consent before any live message, permission grant or backend operation.

For the owner's reported bot, the named launch controls are Open Dashboard, Open Super Dashboard and Open. Their mention in this guide is a reproducible checklist, not a fabricated test result.

## Publication checks

The publication process checks local Markdown/image paths, screenshot metadata and content, owner commit identity, archival application-tree equivalence and the reviewed current UI/test/version delta, complete upstream ancestry, all intended reachable history, known-private material, archive refs, remote SHAs and a fresh recursive clone. Publication evidence is added only after the corresponding check has actually run. Raw local logs and desktop captures are not committed because they may contain machine paths or private data.
