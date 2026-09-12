# Allowgram 7.2.8.5 for Windows x64

This revision adds client-side restrictions beyond the 7.2.8.4 form fix:

- Other-user profiles and profile photos are unavailable, including allowlisted group members. Allowed DM navigation and necessary message-author/bot metadata remain separate.
- Add Account is unavailable. A fresh installation can sign in; stored legacy accounts are not erased or logged out by the upgrade.
- Saved Messages is unavailable even when the account's own ID is in the allow-list. No remote history is deleted.
- Outgoing text/captions/edits reject Unicode and custom emoji. The composer picker, sticker/GIF suggestions and reaction additions are disabled.

The Unicode classifier is derived from the pinned Telegram emoji sequence data.
Emoji-capable symbols in that data are rejected even with text presentation.
Bare digits, `#`, `*`, ordinary punctuation and non-Latin text remain permitted.
Rejected composer/caption submissions retain their text rather than stripping it.

Ordinary allowed text, photos, documents and nonanimated videos remain supported.
URL text is sent without a preview. Message effects, external media URLs, opaque
inline results, rich messages, story forwarding, quick-reply replay and Send Now
for previously scheduled items are not accepted without inspectable content.
Normal plain-text/attachment scheduling at creation remains supported.
Known cached documents and forwards require production content evidence; unknown
objects fail closed. Gzip/TGS, WebM and animated WebP upload prefixes are rejected
conservatively, so ordinary gzip/WebM attachments are also unavailable.

These restrictions apply to this client, not Telegram servers or other clients.
They do not cancel content already scheduled on the server, inspect pixels in
ordinary photos/videos, rewrite received history, or control arbitrary third-party
Mini App/backend content. Existing bot identity, context and consent gates remain.

The Windows installer is per-user and unsigned. Use the accompanying private
validation report and SHA256SUMS.txt for actual source/build/package evidence;
this source note does not assert live signed-in testing or public publication.
