# Allowgram

Allowgram modifies Telegram Desktop to require an account-specific messaging
allow-list after sign-in. The upstream phone-number, verification-code, and
two-step-verification login flow is retained. Chats become accessible after the
user saves at least one valid user, group, or channel ID.

## Using the list

Enter IDs separated by spaces, commas, semicolons, or newlines:

| Field | Examples | Meaning |
| --- | --- | --- |
| Users | `123456789` or `user:123456789` | A Telegram user or bot |
| Groups and channels | `-123456789` or `chat:123456789` | A basic group |
| Groups and channels | `-1001234567890` or `channel:1234567890` | A supergroup or channel |

The signed channel format is `-(1000000000000 + channel_id)`. Use the `channel:`
prefix if you have a raw channel ID. Putting a user ID into the groups field is
rejected. Duplicate entries are removed, and up to 10,000 distinct peers can be
configured. An ID does not grant access to a private group or override Telegram's
own membership and posting permissions.

The list is saved with the account's encrypted local settings and restored on
restart. There is no in-session editor. Logging out clears Telegram's local
account data and requires setup again on the next sign-in.

## Enforcement and scope

Other chats remain readable. The message composer and outgoing request layer
both enforce the list. The request filter checks the destination for text,
attachments, albums, forwards, edits, scheduled-message sends, reactions, poll
votes, and supported bot interactions. Unknown request types are rejected by
default. A bot queried through inline search must also be allowed. Saved
Messages requires your own user ID. Channel comments require the ID of the
linked discussion group.

Calls, mini apps/webviews, broad story publishing, automated business messages,
and other communication features without a supported destination check are
disabled. Public groups/channels can be joined when their channel ID is allowed;
invite links whose destination cannot be verified are blocked. Telegram
permissions still apply inside allowed chats. A basic group
that migrates into a supergroup gets a new peer ID and requires a new list.

This is an application-level restriction. It does not stop the account from
using another Telegram client, control messages already scheduled on Telegram's
servers, or prevent someone with access to the computer from replacing the
application or deleting its local data. It is not a device-management or
administrator-enforced account policy.

## Windows installation and builds

The client uses the name Allowgram, a separate Windows application identity,
its own `%APPDATA%\Allowgram` account directory, and its own installer/shortcuts.
It does not register Telegram URL protocols. Official Telegram automatic
updates are disabled so they cannot replace this custom build.

See [Windows build instructions](docs/building-allowgram-win.md). Production
builds require this application's own Telegram API ID and API hash. They must
not use the upstream demonstration credentials. Installers are unsigned unless
a signing certificate is added to the packaging process.

## Verification status

The standalone C++ ID parser passed 78 checks under MSVC 14.44 with warnings
treated as errors. The outgoing request filter passed 62 native checks using
the actual generated Telegram schema and request serializer, plus 6 schema
audits. The localization, palette, emoji, and style generators also succeeded.
Twelve modified production translation units covering onboarding, account
settings, storage, UI gating, session management, transport, and sign-in branding also compiled
successfully against the real generated Telegram headers. These local checks
used Qt 6.8.3; the release workflow builds upstream Qt 6.11.2. The original
Allowgram icons passed Windows icon loading and resource compilation checks.

The complete application has not yet been linked or started. Installer
validation and interactive phone-login/allow-list acceptance testing are
pending. This source tree is not yet a verified distributable.

Telegram Desktop and its dependencies retain their upstream licenses. The
packaging workflow produces a corresponding source archive with the installer.
