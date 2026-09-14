# Allowgram 7.2.8.6 for Windows x64

This packaging revision extends the [7.2.8.5 hardening](allowgram-7.2.8.5.md).

- New Group and New Channel are unavailable. Shared creation permission guards navigation and direct creation dialogs/submissions. The serialized request inventory continues to reject chat/channel creation without blocking existing permitted conversations.
- Calls history, voice/video/group/conference call initiation, call banners and redial actions are unavailable. A shared call permission stops local initiation and incoming-call presentation before device permissions or panels. Existing transport denials remain in force.
- My Profile and View profile are unavailable for every user, including the signed-in account and allowlisted members. The existing profile-presentation permission also guards short-info dialogs and profile photos. Group/channel information remains separate.
- Contacts, logged-in identity display, own-account Settings, authentication and account maintenance remain available. Phone links retain copying and adding contacts, using cached names when available.

Saved Messages, Add Account, outgoing Unicode/custom emoji, stickers, GIFs and
the composer picker remain disabled. Ordinary allowed text/nonanimated
attachments, initial login, required allow-list setup and permitted bot Mini Apps
retain their existing checks. Account data and received history are not deleted.
Audio/video device configuration remains available; it does not authorize calls.

The native regression uses real production menu, navigation, dialog and composer
components with synthetic data. Its disposable network-disabled overlay stops
call tests before device or panel side effects. Serialized request tests call the
production guard with complete constructors. These are not live signed-in E2E
tests. See [testing](allowgram/testing.md) and the exact package validation receipt
for commands, source identity, results and remaining acceptance work.

These are restrictions in this client, not a server-wide policy, protection
against replacing the binary, or control of third-party Mini App content.
Existing server-scheduled actions are not cancelled. No code-signing or public
release is implied: the Windows x64 installer is unsigned and per-user.
