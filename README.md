<p align="center"><img src="Telegram/Resources/art/allowgram/logo_256.png" width="112" alt="Allowgram logo"></p>

# Allowgram

**A Telegram Desktop-derived Windows client that shows only the conversations you allow.**

Sign in through Telegram's normal phone-number and verification flow, then set up an explicit list of user, bot, group and channel IDs. Allowgram hides excluded conversations and their notifications and blocks client requests to excluded destinations. Mini Apps can open only for bots explicitly included in the list.

Allowgram is an unofficial, independent modification maintained by [molotovgit](https://github.com/molotovgit). It is not affiliated with or endorsed by Telegram. Telegram Desktop and its contributors remain the authors of the upstream client.

## Start here

- [Installation and package verification](docs/allowgram/installation.md)
- [Set up your allow-list](docs/allowgram/allow-list.md)
- [Use allowed-bot Mini Apps](docs/allowgram/mini-apps.md)
- [Build from source](docs/allowgram/build.md)
- [Frequently asked questions](docs/allowgram/faq.md)

This repository publishes source. **No public installer or GitHub Release is provided by this publication.** Version 7.2.8.3 was built and delivered privately to the owner; a public binary release requires a separate distribution step. Do not substitute an upstream Telegram installer: it does not contain Allowgram's restrictions.

## What the client enforces

| Capability | Behavior in 7.2.8.3 |
| --- | --- |
| Repeatable setup rows | **+ Add user** and **+ Add group/channel** add rows; **Remove** deletes extra rows. Up to 10,000 distinct IDs total. |
| Conversation visibility | Excluded chat rows, search results, archive entries, message previews, unread badges and notifications are suppressed. |
| Outgoing operations | Destination checks cover supported text, media, forwarding, edits, reactions and other supported requests. Unknown request types fail closed. |
| Allowed groups | Messages from participants are visible inside an allowed group. This does not allow those participants' DMs or authorize their bots' Mini Apps. |
| Bot Mini Apps | The server-resolved bot must be explicitly allowed. Conversation, reply and send-as contexts are also checked. Links resolve their own target; account switches close app windows. |
| Persistent account policy | The list is stored with the account's encrypted local settings. There is no in-session editor in this version. |

The list is **a client-side restriction**, not a Telegram server rule or device-management policy. Telegram can still receive excluded messages for the account. Other Telegram clients, existing sessions, scheduled server-side actions and someone replacing this application remain outside its control. Read the [security boundaries and disabled features](docs/allowgram/security.md) before relying on it.

## Verification

The 7.2.8.3 release passed **451 native request/message/Mini App checks**, **80 ID-parser checks** and **six schema audits**, plus a Windows x64 Release build, fresh-profile startup, isolated installation/uninstallation and package integrity checks. The owner subsequently reported that the delivered installer worked. That is owner-reported live success, not a claim that every dashboard, excluded-bot case or permission flow was individually exercised. See [testing and evidence](docs/allowgram/testing.md).

## Source and provenance

- [Architecture and boundaries](docs/allowgram/security.md)
- [Changelog](CHANGELOG.md)
- [Original history and atomic commit map](docs/allowgram/history.md)
- [Contribution guidance](CONTRIBUTING.md)
- [Upstream README and third-party notices](README.telegram.md)

The original release history is retained on `archive/release-7.2.8.3`. `main` presents the owner's changes as smaller commits and reproduces the original application tree before the documentation additions. Upstream ancestry and attribution remain intact.

## License

GPL-3.0-or-later with the existing OpenSSL linking exception: see [LICENSE](LICENSE) and [LEGAL](LEGAL). Third-party components retain their own licenses. This repository preserves the source layout, copyright notices and pinned submodule references.
