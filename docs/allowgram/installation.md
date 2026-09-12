# Installation

[Back to Allowgram](../../README.md)

## Availability

This publication contains source code. It does not upload an installer, portable binary or GitHub Release. The owner received a private Windows x64 7.2.8.4 build. There is no public binary download link to follow here. You can [build from source](build.md); a public binary release would require a separate owner-authorized distribution step.

## If you already have an Allowgram package

The 7.2.8.6 installer targets **Windows 10 version 1903 or later, or Windows 11, on x64**. Run `Allowgram-Setup-7.2.8.6-x64.exe` only after checking its accompanying validation receipt. The installer is per-user and unsigned. Windows SmartScreen or an unknown-publisher prompt may appear; verify the source of the file and its checksum before choosing to proceed. Do not disable Windows security settings.

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath .\Allowgram-Setup-7.2.8.6-x64.exe
```

Compare the result with the `SHA256SUMS.txt` delivered through the same trusted release channel. A matching checksum checks file integrity; it is not a code signature or independent assurance of the publisher's identity.

The installed app uses its own application identity and `%APPDATA%\Allowgram` account directory. The portable ZIP must be fully extracted; it uses `AllowgramForcePortable` beside `Allowgram.exe`. Keep that directory intact if you move a portable copy. Do not copy another Telegram session merely to set up Allowgram.

Upgrading the existing Allowgram installation preserves its account data and allow-list. No logout is required to enable Mini Apps for bots already in that list. Uninstalling preserves account data. Official Telegram automatic updates are disabled in this build, and Allowgram does not register itself as the system Telegram URL-protocol handler.

After first sign-in, follow [Set up your allow-list](allow-list.md).

## Future public binary release checklist

A separate release should publish the exact installer, portable ZIP, corresponding source archive and checksums together, identify the source commit and test results, disclose signing status, and preserve all license notices. This task does not configure signing credentials, upload binaries or dispatch a build workflow.
