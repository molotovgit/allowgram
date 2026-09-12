# Mini Apps for allowed bots

[Back to Allowgram](../../README.md)

Mini Apps are available only when their **server-resolved owning bot is explicitly in the current account's Allowed users list**. A displayed sender label, a web domain or an allowed group is not proof that another bot is permitted. Bot IDs greater than 32 bits are supported; Class A Assistant is an example, not a hard-coded exception.

## Open an app

1. Check that the owning bot's numeric ID was included under Allowed users during setup.
2. Open its allowed conversation.
3. Use the bot's message/keyboard web-app button, bot menu **Open** / main-app button, or a supported Telegram bot-app link opened within Allowgram.
4. Review Telegram's normal confirmation prompts. List membership and accepting Terms do not grant write access or permission to share your phone number.

For **@ClassAAssistant_bot** (`8558994389`), the reported entry points are **Open Dashboard**, **Open Super Dashboard**, and the bottom-left **Open** button. Different buttons keep their own launch URLs. The owner reported that the delivered 7.2.8.3 installer worked; there is no independently recorded pass for each individual button or excluded-bot test.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| `CLIENT_CHAT_NOT_ALLOWED` | Verify the owning bot is explicitly allowed, and any launching conversation, reply source and send-as peer are also allowed. A group alone never authorizes its bots. |
| A Telegram app link is rejected | The link's own destination must resolve and pass the list. Unknown owners, opaque app IDs, unsafe link shapes and mismatched mappings fail closed. |
| The app closes on account switch | Expected: an app is bound to the account and conversation context that authorized it. Open it again from the intended allowed account. |
| Windows cannot create the web view | The Windows client uses Microsoft Edge WebView2. Install or repair the [official WebView2 Evergreen Runtime](https://developer.microsoft.com/en-us/microsoft-edge/webview2/), then retry. Do not disable WebView security settings. |
| Dashboard says access denied | The third-party service still controls its own authentication, roles and backend permissions. Allowgram does not grant dashboard administrator access. |
| A bridge feature is unavailable | See the deliberately unsupported features below. |

Payments, arbitrary custom bridge methods (including Telegram cloud storage), prepared-message sharing, chat/contact chooser bridges, managed-bot creation, emoji-status changes and unscoped join/result-message requests remain disabled. Native device storage and supported bot-bound send-data remain available. A third-party app may depend on a disabled feature even when its initial page opens.

Telegram authentication URLs/initData, origin checks and native consent are retained. Internal Telegram links resolve their own destination before opening. Existing windows and delayed callbacks recheck authorization; allowing one app cannot implicitly authorize another bot or recipient.

See [security boundaries](security.md) and [test scope](testing.md).
