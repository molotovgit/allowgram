# Archived upstream automation

These files preserve Telegram Desktop's original workflow sources for
provenance. GitHub only runs workflow files from `.github/workflows`, so this
directory is inert. Do not move a workflow back without reviewing its triggers,
publish destinations, account identity, permissions and required secrets.

Allowgram's active Windows workflow is manual (`workflow_dispatch`), has
read-only repository permissions and uploads build artifacts only when explicitly
run. It does not create a GitHub Release. No workflow is dispatched as part of
this source publication.
