# Public source verification

[Back to Allowgram](../../README.md) · [Machine-readable results](verification-results.json)

The preflight revision and exact counts are recorded in the results file. These
are observed local checks; they are not an independent security audit or a
claim that all possible policy bypasses have been ruled out.

## Source and identity

The publication checkout has complete upstream ancestry. Six original release
boundaries matched their reconstructed Git trees exactly. The original
application, submodule references, LICENSE and LEGAL remained unchanged by the
public documentation work. All owner commits examined used the requested owner
identity for both author and committer; no signing or Verified status is claimed.
The archive branch preserves the original six Allowgram commits. Git's full
strict integrity check passed, and 39 recursive submodule pins were checked.

## Secret-scan scope

Gitleaks 8.30.1 was downloaded from its official release and checked against the
release asset's SHA-256 digest. An unfiltered history scan included both intended
publication refs, full history and merge diffs. A second scan examined every
tracked worktree file exported from the publication revision. Ignored build
outputs, Git metadata and private audit files are not source-publication content.

A separate byte-level pass examined every reachable blob, commit and tag from
the intended refs, comparing against the actual private build credentials and
available owner authentication values without logging those values. It also
checked private machine paths, credential-shaped URLs, session/generated paths
and oversized objects. It found no known private credentials or private machine
paths and no objects over 50 MiB.

The unfiltered Gitleaks history scan reported 13 existing upstream items; the
publication scan introduced no additional alerts. The tracked worktree scan
reported five items, each matching a line in the verified upstream base:

| Finding family | Reviewed classification |
| --- | --- |
| Telegram API hashes in upstream configuration/build examples | Published upstream test/open-build identifiers, not the owner's private production API configuration. Production packaging requires the builder's own app credentials. |
| Firebase configuration API identifier | Public, client-distributed Telegram bootstrap configuration material retained from upstream. It is not an owner cloud credential or an administrator grant. |
| PEM private-key detector in historical canary workflow | A shell `printf` formatting template with runtime secret substitution, not an encoded private-key payload. |

Sixteen additional path/URL flags were reviewed: eight upstream Wavefront `.obj`
3D artwork files are text assets, and eight historical workflow URLs contain
environment-variable substitutions rather than literal credentials. Exceptions
are tied to the exact observed upstream objects/findings; no general secret
rule or GitHub push protection is disabled.

The private original backup, detailed scan reports and raw desktop captures
remain outside Git. A clean latest tree alone was not used as evidence that
the archive history was safe.

## Tests and automation

The publication checkout reran the real native guards (451 checks), ID parser
(80 checks) and pinned schema audit (six tests); all exited 0. The source matches
the previously built and packaged 7.2.8.3 application. See [test scope](testing.md)
for the release checks and the distinction between automated verification and
the owner's reported live success.

The active workflow has only `workflow_dispatch` and read-only repository
permissions. Original release/deploy workflows stay outside the active workflow
directory. This source publication does not dispatch a workflow or publish
installers, caches or a GitHub Release.

Documentation paths, images and remote refs must be checked again after the
final screenshot additions. The record above does not claim an unperformed
capture or remote push succeeded.
