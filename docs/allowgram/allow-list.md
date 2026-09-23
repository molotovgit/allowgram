# Set up your allow-list

[Back to Allowgram](../../README.md)

## Choose existing chats

Complete Telegram's normal phone-number, verification-code and, if enabled,
two-step-verification flow. If the account has no saved list, **Set up your
allow-list** covers the client before conversations become available.

1. Wait for pinned, main and archived chats to finish loading. Native rows show
   chat photos and type/status information. Saved Messages and inaccessible chats
   are excluded; nothing is preselected.
2. Search by name and select the people, bots, groups and channels you want.
   Search hides rows without clearing their selection. User, group and channel
   identifiers stay distinct even when their numeric parts match.
3. Choose **Save and continue**. Save stays visible outside the scrolling list
   and is enabled only after loading is complete and at least one chat is selected.
   Up to 10,000 distinct chats can be selected.

There is no manual-ID override, Sheets connection or installation access file.
If a chat is missing, open or join it through Telegram and reload the picker.
Reload starts a fresh snapshot and clears previous choices; reselect after it
finishes. This avoids carrying inaccessible or removed chats into a new list.

## Errors and saving

- A failed, incomplete or stalled request keeps the client locked. Check the
  connection and use **Retry loading chats**.
- A selected chat that becomes inaccessible must be reloaded before saving.
- A local-storage save error keeps setup locked. Correct the storage issue and
  retry; configuration is accepted only after a verified local write.
- Logging out or changing accounts cancels the old loading generation and its
  rows. Late replies cannot configure a different account.

After saving, only selected conversations become available. Older messages load
when an allowed chat is opened; first setup resets histories that were previously
filtered. Telegram membership and posting permissions still apply.

## Later changes

The list survives restarts in encrypted local account settings. Existing
configured accounts keep their list and skip setup. There is no in-session list
editor: logging out clears local account data and the next sign-in requires setup.
Do not log out merely to view this guide.

An allowed group shows its participants' messages; select a participant's DM
separately if needed. Select the linked discussion group for channel comments,
and the owning bot plus any launching conversation for a Mini App. A basic group
that migrates to a supergroup receives a different peer identity.

Historical numeric-form screenshots describe older builds, not this picker.
See [Mini Apps](mini-apps.md), [FAQ](faq.md) and [client-side boundaries](security.md).
