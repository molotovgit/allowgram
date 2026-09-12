# Contributing to Allowgram

Read [the architecture and boundaries](docs/allowgram/security.md) before changing policy behavior. Preserve upstream licenses, submodule pins, Telegram authentication and WebView security. Do not solve compatibility problems by globally allowing requests or by trusting a launching group instead of a resolved bot.

Make one small, meaningful change per commit. Keep implementation, narrowly relevant tests and necessary declarations coherent; split unrelated UI, build and documentation work. Use clear plain-language subjects and run the checks appropriate to the change. Never add credentials, account/session files, full-desktop screenshots, installers or build caches to Git.

For this owner's publication, Allowgram-owned commits use `molotovgit <275727637+molotovgit@users.noreply.github.com>` as both author and committer, as explicitly requested by that owner. Future contributors should use their own accurate identity. Never copy the owner's attribution for someone else's work. Authentication, email attribution and cryptographic signing are different; do not claim a Verified signature without a real signature.

The historical presentation on `main` contains small dependent commits. It preserves the original release tree at each mapped boundary; not every intermediate historical split was separately compiled. Original release commits remain on `archive/release-7.2.8.3`. See [the commit map](docs/allowgram/history.md).

Use [testing instructions](docs/allowgram/testing.md) and [build instructions](docs/allowgram/build.md). For documentation, check every local link and image, use real isolated UI captures, strip metadata and review images for personal information. Report limitations and unperformed tests plainly.

The upstream contribution guide remains at [docs/upstream-contributing.md](docs/upstream-contributing.md) for provenance. Allowgram-specific changes belong in this repository; do not send custom policy changes to Telegram's repository without separately agreeing that scope.
