# Allowgram

Allowgram modifies Telegram Desktop to require an account-specific messaging
allow-list after sign-in. The upstream phone-number, verification-code, and
two-step-verification login flow is retained. Chats become accessible after the
user selects and saves at least one chat.

## Using the list

Choose existing conversations by name in the searchable setup list, with native
chat photos and type/status lines. Pinned, main and archived chats load before
saving is enabled. Saved Messages is not offered because it is disabled.
Select up to 10,000 distinct chats, then choose **Save and continue**. Search does
not clear selections. **Retry loading chats** starts a fresh snapshot and clears
old choices, so removed or inaccessible chats cannot be carried into a new list.
There is no manual-ID override or Sheets access file. To include a missing chat,
open or join it through Telegram, then reload the picker before saving.

The list is saved with the account's encrypted local settings and restored on
restart. There is no in-session editor. Logging out clears Telegram's local
account data and requires setup again on the next sign-in.

## Enforcement and scope

Only allowed conversations are visible. Incoming messages from excluded
conversations are discarded before entering the message model; their chat
rows, archive entries, search results, unread counts, sounds, flashes, call
alerts and notification previews are suppressed. Cached navigation and media
playback also require an allowed conversation. Allowed groups still show
messages from their participants, even when those people are not individually
allowed for direct messages.

The message composer and outgoing request layer both enforce the list. The
request filter checks the destination for text,
attachments, albums, forwards, edits, scheduled-message sends, reactions, poll
votes, and supported bot interactions. Forwarded messages and stories also
require their source conversation to be allowed. Unknown request types are rejected by
default. A bot queried through inline search must also be allowed. Saved
Messages is disabled, including when your own ID is listed. Channel comments require the ID of the
linked discussion group.

Version 7.2.8.5 also blocks other-user profiles, additional-account creation and
outgoing emoji/stickers/GIF animations. Plain URL text has no outgoing preview.
Opaque outgoing content is conservatively restricted; see the exact
[hardening scope and limits](docs/allowgram-7.2.8.5.md). Initial login and restored
legacy account data are preserved. A participant's metadata is not permission
to display their profile, even when their DM is allowed.

Mini Apps can open only for server-resolved bots explicitly included under
Users. The launching conversation, reply sources and send-as identities must
also be allowed. Bot message/keyboard buttons, bot menu/main apps and supported
Telegram app links use the same account-specific checks. App links resolve
their own target bot; an allowed group or app does not authorize another bot.
Opaque app IDs and unresolved or mismatched owners are rejected. Existing
windows revalidate their context and close when the active account changes.

Telegram authentication URLs, initData, origin checks and consent remain in
use. Opening an app or accepting its Terms does not grant write access.
Permission requests still require the relevant Telegram confirmation.
Third-party web content has its own backend permissions; Allowgram does not
assign dashboard roles or override server authorization. Payments, arbitrary
custom bridge methods (including cloud storage), prepared-message sharing,
chat/contact chooser bridges, managed-bot creation and emoji-status changes
remain unavailable. Device storage and bot-bound send-data remain supported.

Calls, aggregate stories/global discovery views, broad
story publishing, automated business messages, and other communication
features without a supported destination check are disabled. Raw account/chat
exports, takeout sessions and arbitrary URL previews/instant views are also
disabled because they can bypass conversation filtering. Public groups/channels can be joined when their channel ID is allowed;
invite links whose destination cannot be verified are blocked. Telegram
permissions still apply inside allowed chats. A basic group
that migrates into a supergroup gets a new peer ID and requires a new list.

Telegram servers can still receive messages for this account. Allowgram
suppresses excluded conversations locally. This application-level restriction
does not stop the account from using another Telegram client, control messages already scheduled on Telegram's
servers, or prevent someone with access to the computer from replacing the
application or deleting its local data. It is not a device-management or
administrator-enforced account policy.

## Windows installation and builds

The client uses the name Allowgram, a separate Windows application identity,
its own `%APPDATA%\Allowgram` account directory, and its own installer/shortcuts.
The portable zip instead keeps account data in `AllowgramForcePortable` beside
the executable.
It does not register Telegram URL protocols. Official Telegram automatic
updates are disabled so they cannot replace this custom build.

See [Windows build instructions](docs/building-allowgram-win.md). Production
builds require this application's own Telegram API ID and API hash. They must
not use the upstream demonstration credentials. Installers are unsigned unless
a signing certificate is added to the packaging process.

## Verification status

The C++ ID parser passed 80 checks under MSVC 14.44 with warnings treated as
errors. The request, incoming-message and Mini App guards passed 451 native checks
using the generated Telegram schema and real serialization, plus 6 schema
audits. Incoming cases include matching numeric IDs across different peer
types, normal/service messages, denied authors inside allowed groups,
allowed authors inside denied groups, and an unconfigured allow-list.
Mini App cases cover two allowed 64-bit bot IDs, excluded/unknown/human
identities, group/forwarded contexts, reply/send-as peers, opaque/mismatched
apps, policy/account changes and unsafe bridge links. The native policy
checks used Qt 6.8.3.

The Windows x64 Release build passed with MSVC 14.44 and upstream patched
Qt 6.11.2. The resulting client passed a startup check with fresh, isolated
account data and no fatal startup log errors.

The owner subsequently reported that the delivered 7.2.8.3 installer worked.
This is owner-reported live success; no individual dashboard or excluded-bot
case is claimed as independently tested. See [current testing details](docs/allowgram/testing.md).

Telegram Desktop and its dependencies retain their upstream licenses. The
packaging workflow produces a corresponding source archive with the installer.
