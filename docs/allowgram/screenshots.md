# Screenshot provenance

[Back to the illustrated guide](allow-list.md)

The three images are fresh 7.2.8.4 Allowgram UI captures supplied through MiniPEKKA's
capture workflow. They are not generated illustrations or reconstructed HTML.
They show the corrected production `AllowlistLockWidget`, styles, labels, add/remove
controls and parser in a separate, unsigned-in documentation fixture.

The disposable fixture displays that existing widget without authenticating a
Telegram account, supplies neutral demonstration entries and calls the real ID
parser for the error example. Its local-only adapter does not save account
settings or simulate a successful authenticated save. The ordinary application
shows this setup screen after sign-in, as described in the guide.

The published overview, multiple-row and invalid-entry images use 100%
application scale. An 800x900 outer window was requested; its lower blank
portion extended below the display. Each lossless crop is the actual visible
800x840 app-content rectangle. All form controls, the complete validation error,
**Save and continue**, and **Log out** are contained in these three captures.
They are not described as captures of the entire requested outer window.

Additional native captures at 150% with a selection and at 100% in an 800x598
compact window were reviewed separately. IDs, selection bounds, labels,
underlines and Remove links are readable. Those views show only part of the
scrollable form; they do not establish that Save is visible at that scroll
position. Caret containment and scrolling behavior are also checked by the
[native regression](testing.md). Windows DPI and monitor migration were not
changed or tested.

The fixture executable, profiles and raw desktop captures stay outside Git.
Only window crops are published. Every final crop was visually reviewed for
personal IDs, conversations, credentials, terminal contents and other windows;
PNG metadata was stripped before committing. No owner session was copied,
reset or logged out to make these images. The installed client and older
7.2.8.3 delivery were preserved. The 7.2.8.4 release executable was rebuilt
separately from the same production layout source.
