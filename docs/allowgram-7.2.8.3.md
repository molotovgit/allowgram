# Allowgram 7.2.8.3

Mini Apps can now launch for bots explicitly listed under **Users**. This fixes
the `CLIENT_CHAT_NOT_ALLOWED` launch error for allowed bots, including the
reported Class A Assistant dashboard buttons. Bot IDs remain 64-bit; there is
no bot-specific exception.

Message/keyboard web-app buttons, bot menu/main-app buttons and supported
Telegram app links check the resolved bot and the associated conversation.
An allowed group does not authorize its bots. Telegram links inside apps
resolve and check their own destination. Different dashboard buttons retain
their own URLs instead of reusing the first open dashboard.

The request layer checks the bot, conversation, reply sources and send-as
identity. Named app lookup and launch retain the owning bot identity; opaque
app IDs and missing or mismatched owners fail closed. Existing instances and
bridge callbacks recheck the current account and policy. Apps close on account
switches or when their conversation context expires.

Telegram-provided authentication URLs/initData, origin protections and consent
remain in place, following the [Telegram Mini Apps client flow](https://core.telegram.org/api/bots/webapps).
Allow-list membership and accepting a menu's Terms do not grant write access.
Write access and phone sharing require Telegram's confirmation controls.
Third-party dashboards retain their own backend roles and permissions.

Calls, exports and existing conversation restrictions remain in effect.
Payments, arbitrary custom bridge methods (including Telegram cloud storage),
prepared-message sharing, chat/contact chooser bridges, managed-bot creation,
emoji-status changes and unscoped join/result-message requests remain disabled.
Mini Apps that depend on those features may report them as unavailable.

## Update

Run `Allowgram-Setup-7.2.8.3-x64.exe` to update when ready. This is an unsigned
per-user Windows x64 installer. Windows may show an unknown-publisher prompt.
The existing account and allow-list are preserved; no logout is needed to use
Mini Apps for bots already on that list. The previous 7.2.8.2 packages are kept.

The portable package uses its own `AllowgramForcePortable` account directory.
The source archive and `SHA256SUMS.txt` accompany the binaries.

## Verification

The native request/message/Mini App suite passed 451 checks. The ID parser
passed 80 checks, and all 6 schema audits passed. Cases cover two allowed
64-bit bots, excluded/unknown/non-bot identities, allowed groups with excluded
bots, reply/send-as peers, opaque or mismatched apps, account/policy changes
and forbidden bridge links. The release's `VALIDATION.json` and `validation/`
directory record build, startup, installation and archive checks with their
actual commands, exit codes and logs.

Signed-in dashboard loading and excluded-bot rejection have not been exercised
on a dedicated authorized test profile. No owner session was copied or reset,
no saved allow-list was edited, and no live messages, permission grants or
dashboard mutations were performed during validation.

The remaining owner check is to install the update, open the already-allowed
Class A Assistant bot, and try **Open Dashboard**, **Open Super Dashboard** and
the bottom-left **Open** button. Confirm that the dashboards load, then verify
that a Telegram Mini App link for a bot outside the saved list is rejected.
No logout, session copy or allow-list change is required for this check.
