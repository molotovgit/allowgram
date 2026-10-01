# Allowgram 7.2.8.10 — group call joining

Fixes the blanket policy gates that hid existing group calls and refused their join requests.

- Join existing calls in allowlisted groups as your signed-in user.
- The banner, chat-header action, call history item and group menu use the same current group eligibility check.
- Normal Telegram call negotiation/media code is retained; serialized requests now require the exact server-mapped group call and checked join ownership.
- Leaving, self-mute/unmute, participant reads and presentation/media requests are scoped to that call. Changing the allowlist stops active media and cancels/cleans up an in-flight join.
- Private-call restrictions are unchanged. Call creation, invitation/moderation, conference calls, scheduled-call start and arbitrary call links remain disabled.

## Verification boundaries

The regression suites exercise serialized request parsing, actual session/group mapping, revocation and real menu callbacks. Native offline fixtures use synthetic accounts/transport and are not evidence of a live Telegram group call, microphone audio, camera/video or screen sharing. Record the exact candidate hash and live-call result separately before claiming those passed.

Version: `7.2.8.10`; stable update counter: `10`. Publishing is a separate step from the source change.
