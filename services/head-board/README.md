# Allowgram Head — temporary management board

FastAPI/SQLite + React/Vite control plane for scoped head accounts and signed per-user allowlists. Deploying this service does **not** update an installed Allowgram client. The new native integration is a separate candidate, with native regression, packaging, signing and authenticated acceptance gates still required before release.

## Run locally

```sh
uv sync
(cd web && npm ci && npm run build)
HEAD_DATA_DIR="$HOME/.local/share/allowgram-head" HEAD_PUBLIC_URL="http://127.0.0.1:28444" uv run uvicorn headboard.api:configured_app --factory --host 127.0.0.1 --port 28444 --no-access-log
```

Create a private, single-use owner access code (the output file must not exist):

```sh
uv run python -m headboard.cli --data "$HOME/.local/share/allowgram-head" owner-login --output "$HOME/.local/share/allowgram-head/owner-access.txt"
```

The code expires after **15 minutes**, is written with mode `0600`, and is never printed. Server sessions expire after **12 hours**. Configure `HEAD_OWNER_ID` for ownership; the operator CLI and runtime directory are root-equivalent for this service. The runtime directory is `0700`; code, key and database files are `0600`. Keep these files outside this repository. Issue a fresh code to a new filename if an earlier one expires. There is no public recovery endpoint.

Do not forward an owner code to a head. Create the head, assign its user scope, then issue a separate head sign-in code through the board.

## Temporary HTTPS hosting

Run a temporary HTTPS reverse tunnel to the loopback listener. Set `HEAD_PUBLIC_URL` to its exact HTTPS origin and restart **only this service** before browser login or device enrollment. Cookie security and exact Origin checks derive from that value. Do not bind `0.0.0.0` as a shortcut.

A quick-tunnel hostname is ephemeral and depends on the Mac and tunnel staying running. Native enrollment pins both the origin and signing key. **Use the temporary origin for isolated QA, not real fleet enrollment:** changing the hostname is not transparent to enrolled clients, and an origin/key migration or local unpairing UI is not implemented. A stable hostname, protected runtime backup/restore, recovery plan and explicit operational approval are prerequisites for real rollout. This setup creates no permanent autostart.

## Authentication and ownership

Authentication uses owner-issued random, one-time access codes. Numeric Telegram IDs are owner-assigned account labels, **not cryptographically verified Telegram identity**. Telegram SSO is not implemented. Adding it requires separately registered credentials/URLs and a tested integration; no bot or personal account should be repurposed implicitly.

There is no public registration, default password or first-user-is-owner shortcut. Cookies are HttpOnly, SameSite=Strict and Secure on HTTPS. Browser writes require the configured Origin and a session CSRF token. HTTP is limited to explicit loopback development. Heads cannot create heads/users or assign themselves scope. Disabled heads lose access immediately; the owner principal cannot be disabled.

## Device contract

See [PROTOCOL-v1.md](PROTOCOL-v1.md). Enrollment invitations are opaque `AGH1.` capability strings: per-user, single-use, with a 15-minute lifetime and an Ed25519 public-key pin.

In an updated client, complete the original local chat-picker onboarding, then explicitly connect through **Settings → Head-managed allowlist**. The invitation must match that session's account ID. Initial selected peer IDs seed the policy only when it is uninitialized; a later device cannot overwrite an existing head policy. Ordinary clients cannot edit a managed policy locally.

Policies sign exact JSON bytes using Ed25519 and the domain `ALLOWGRAM_HEAD_POLICY_V1\n`. Payloads bind the subject and device IDs, increasing revision, persistent issue timestamp and typed bare peer IDs. An empty managed list means **deny-all**, not a return to local onboarding. Software-update signing is separate and untouched. Server-side device credentials are stored as hashes; revoked credentials cannot fetch or acknowledge policies.

**Saved is not applied.** The desired revision is distinct from each device's acknowledged revision. An ACK includes the SHA-256 of an actually issued signed payload; fabricated, future or backward ACKs fail. An ACK is a client report, not independent filesystem attestation. The native contract requires verified persistence and application before ACK.

Offline clients retain their last verified restrictions. Device revocation stops future sync; it does not log out, unlock or erase cached restrictions. Mac/tunnel downtime prevents new changes from reaching devices.

## Privacy and safety

The service stores managed IDs, permitted-peer IDs/labels, device metadata and audit/status data. It does not collect Telegram messages, auth keys, contact books or Google credentials. There is no Sheets/roster import. The real temporary service starts empty except for its owner. QA uses separate databases and synthetic peers.

Native fixture results are explicitly **synthetic and offline**. They do not establish a real Telegram login, live transport interoperability or preservation of an installed account. Tests and candidate builds must use fresh private profiles, never an installed user's `tdata`. No automatic client installation, update, public release or fleet rollout is implied.

## Verification

```sh
uv run pytest -q
uvx ruff check headboard tests
uvx ruff format --check headboard tests
cd web
npm run build
npm run test:e2e
```

The backend and browser tests cover access scopes, CSRF/origin protections, enrollment/replay/subject binding, signatures, revisions, acknowledgements and bounded request bodies. Browser tests must use isolated QA storage, never production.

Export public synthetic vectors for the optional native managed-session fixture:

```sh
PYTHONPATH=. uv run python tests/export_native_vectors.py /private/qa/managed-vectors.json --subject 99 --gui-peers
```

The native regression builder's `--managed` option requires standalone `--hardening` mode. It substitutes HTTP transport only in a disposable fixture executable; production source and binaries do not gain a test enrollment/authentication path. Full native compilation and every named integration assertion must pass before crediting that gate. Authenticated private-profile acceptance, release versioning/signing and release approval remain separate.
