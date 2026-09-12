# Frequently asked questions

[Back to Allowgram](../../README.md)

### Can someone outside the list still message my Telegram account?

Yes. Telegram may receive it on the server. Allowgram suppresses that conversation locally: no visible message, chat entry or notification. Other Telegram sessions are not controlled by this app.

### Do I enter usernames or phone numbers?

No. Enter typed numeric Telegram IDs as shown in the [allow-list guide](allow-list.md). A positive unprefixed ID belongs in Allowed users; a raw channel ID needs `channel:` in the group section.

### Can I add many users and groups?

Yes. **+ Add user** and **+ Add group/channel** create more rows. The combined list supports up to 10,000 distinct IDs. Extra blank rows are ignored and duplicate entries are removed.

### Can I edit the saved list in Settings?

There is no in-session editor in 7.2.8.3. The saved list remains fixed until logout; after the next sign-in setup is required again. Plan reconfiguration carefully and ensure you can authenticate again. Do not log out merely to upgrade or to use a bot already in the list.

### Why do messages from unlisted people appear in an allowed group?

The permission is for that conversation. Participants' group messages remain visible. Their personal DMs remain excluded unless their user IDs are separately allowed.

### Does allowing a group allow all bots in it?

No. A Mini App's owning bot must also be explicitly allowed under Allowed users, even when launched from an allowed group, a forwarded button or another app.

### Why are Saved Messages or channel comments missing?

Saved Messages needs your own user ID. Channel comments need the linked discussion group's ID. A group that migrates to a supergroup receives a new peer ID.

### Is there a public installer here?

Not in this source-publication task. See [installation](installation.md) for the actual distribution status and [build instructions](build.md) for local builds. No public CI build success or signed binary is claimed.

### Will it update itself to official Telegram?

Official Telegram automatic updates are disabled so they cannot replace the custom restrictions. A future Allowgram update must be obtained through its own trusted distribution process.
