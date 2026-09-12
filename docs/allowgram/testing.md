# Testing and verification

[Back to Allowgram](../../README.md)

## Recorded 7.2.8.3 results

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

Install Qt Base 6.8.3 for MSVC x64 separately, then pass its directory:

```powershell
& Telegram/build/allowgram/test_request_guard.ps1 -QtDirectory 'C:\Qt\6.8.3\msvc2022_64'
```

This harness generates the pinned TL schema, serializes actual request constructors and calls the production guards. It covers two allowed 64-bit bots, excluded/unknown/non-bot owners, allowed groups with excluded bots, from-message and reply/send-as context, mismatched/opaque apps, account/policy changes and unsafe bridge links. Existing incoming-message and outgoing-chat checks remain in the suite.

## Safe live acceptance

Use a dedicated authorized account/profile with a deliberately chosen test list. Exercise only agreed actions. Do not log out another person, copy their session or change their saved policy. Verify allowed and excluded conversation visibility and notifications, supported sends only to test recipients, allowed-bot app loading, excluded-bot links and account switching. Obtain explicit consent before any live message, permission grant or backend operation.

For the owner's reported bot, the named launch controls are Open Dashboard, Open Super Dashboard and Open. Their mention in this guide is a reproducible checklist, not a fabricated test result.

## Publication checks

The publication process checks local Markdown/image paths, screenshot metadata and content, owner commit identity, exact application-tree equivalence, complete upstream ancestry, all intended reachable history, known-private material, archive refs, remote SHAs and a fresh recursive clone. Publication evidence is added only after the corresponding check has actually run. Raw local logs and desktop captures are not committed because they may contain machine paths or private data.
