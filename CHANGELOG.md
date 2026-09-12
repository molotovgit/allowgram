# Allowgram changelog

## 7.2.8.5

- Block other-user profile presentation and Saved Messages, independently of the saved allow-list.
- Remove additional-account entry points and guard new logins without deleting restored accounts.
- Reject outgoing Unicode/custom emoji, stickers, GIF animations and reactions; remove composer pickers and suggestions.
- Classify final text, media metadata, uploaded file prefixes and cached forwards before transport. Suppress outgoing URL previews and effects.
- Preserve ordinary allowed text and attachments, allowed-bot Mini App gates and the 7.2.8.4 form layout. See [hardening scope and limits](docs/allowgram-7.2.8.5.md).

## 7.2.8.4

- Restore native input-field height so numeric IDs, prefixed group/channel IDs and the caret fit below floating labels.
- Align each Remove link with its row's editable text region.
- Reserve a native border-width document inset so fractional-scale caret rounding stays inside the viewport.
- Add a regression that measures the actual form and exercises row controls, focus and scrolling in isolated profiles at four application scales.
- Keep the numeric ID policy, fixed saved allow-list and Mini App authorization unchanged.

## 7.2.8.3

- Enable supported Telegram Mini App entry points for explicitly allowed, server-resolved bots.
- Check bot identity and associated conversation/reply/send-as context; reject unknown and mismatched owners.
- Revalidate account and policy on app reuse, links and delayed callbacks.
- Keep dashboard-button URLs distinct and preserve native consent without treating Terms as write permission.
- Retain explicit restrictions on unsupported bridge operations.
- Passed the release checks documented in [testing](docs/allowgram/testing.md). The owner later reported the installer worked; individual live-case outcomes are not asserted.

## 7.2.8.2

- Hide excluded conversations across chat lists, search, archive, notifications and unread counts.
- Filter incoming messages before they enter the client model and guard cached navigation/media paths.
- Add repeatable user and group/channel rows with Remove controls and a combined 10,000-ID limit.
- Keep the portable account-directory marker in the Windows ZIP.

## Initial Allowgram work

- Require an account-specific list after sign-in and verify local policy persistence before unlocking.
- Enforce supported outgoing destinations at the serialized request layer.
- Add Allowgram branding, separate Windows identity/account storage and an installer/source packaging pipeline.
- Retain upstream automation outside the workflow directory and add an opt-in Windows workflow.

## Public source publication

- Preserve complete upstream ancestry and original release commits on an archival branch.
- Present owner changes in smaller commits with exact tree-equivalence checks.
- Add product, setup, Mini App, architecture, build and testing documentation.
- No public binary release or GitHub-hosted build is implied by this publication.

See [history and provenance](docs/allowgram/history.md), [LICENSE](LICENSE) and [LEGAL](LEGAL).
