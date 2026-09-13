# Allowgram 7.2.8.7 for Windows x64

Incoming and outgoing private voice and video calls require an explicitly
allowlisted, known user in this account. Allowing a group does not permit calls
with its members. Unknown users, bots, self, group calls and conferences remain
blocked.

Eligible private chats have a call button. Click for voice calling; right-click
for the voice/video menu. Calling does not require opening a personal profile.
The sidebar Calls/history entry, New Group, New Channel, View Profile and My
Profile remain unavailable.

Authorization is checked before outgoing permissions and incoming call
construction, then again through acceptance, confirmation, updates and signaling.
Each call ID and access hash is bound to its verified account and peer. Removing
a peer from the saved policy ends pending or active calls and retains only
narrow cleanup permission for that validated call. Reallowing a user does not
revive an old call. There is still no in-session allow-list editor.

Settings, Contacts, initial login, allowed chats/group information and allowed-bot
Mini Apps retain their existing controls. Saved Messages, multiple accounts,
outgoing emoji/stickers/GIFs and the picker remain disabled. The nullable
short-info presentation correction and explicit hide-layer behavior are retained.

Verification uses real production policy, call models, callbacks and serialized
requests with synthetic identities and responses. A disposable native overlay
intercepts networking, device/media creation, ringing and call panels; it also
substitutes key-exchange values. This is not live-account interoperability or a
real audio/video call. See [testing](allowgram/testing.md) and the exact package's
validation receipt for completed runs and source identity.

The Windows x64 package is unsigned. Public publication, signed installation and
live-account calling are separate acceptance work. The preexisting IPC CMD:quit
shutdown crash remains outside this change; startup checks use normal WM_CLOSE.
Existing account data and earlier packages are not modified by verification.
