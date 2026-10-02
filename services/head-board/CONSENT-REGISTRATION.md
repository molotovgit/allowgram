# Consent-based desktop registration (client 7.2.8.12)

The default desktop flow is now one informed **Connect** prompt after login and local chat selection. Declining keeps local behavior unchanged and survives restart; Settings offers an explicit connection action later. Existing logged-in profiles, chosen chats and manual AGH1 invitations remain supported. No invitation is required for this new flow.

## Trust and authority

The release client pins `https://allowgram-head-production.up.railway.app` and management public key `emcLYc3W7m8Gv1y9r_cLBug_WZuzjFl1iMaNiV1ZYKw`. These are independent of the unchanged software-update signing root. No plaintext HTTP, redirect, discovery/TOFU, or runtime pin override exists in the default flow. Test-only constructor injection requires an injected transport.

An account ID supplied by a desktop client is **not Telegram identity verification**. The server creates a separate, owner-only **unverified pending connection**, not an established managed user, head, or privileged dashboard session. The owner must compare the device verification code with the intended user's Settings → Head-managed allowlist before approving. Approval grants only device access to that account's existing policy. Heads cannot approve or list pending connections. It never grants dashboard roles or changes scope membership.

## Wire

The desktop creates a cryptographically random 32-byte base64url token only on consent. The token, subject, pinned target/key, and immutable initial request are durably saved in encrypted per-account settings **before transmission**. No management traffic occurs before consent. Existing selected chats remain effective while pending or rejected.

`POST /api/client/register`, `Authorization: Bearer <token>`:

```json
{"telegram_user_id":"90001","initial_peers":[{"kind":"user","id":"23456"}],"device_name":"Allowgram Desktop","client_version":"7.2.8.12","consent_version":1}
```

The same token and body is idempotent, including a lost response/restart. Changed bodies reject with409. Tokens are stored only as SHA256 hashes. `GET /api/client/registration` uses the same bearer. Its base response has exactly five fields:

```json
{"v":1,"id":"<32 lowercase hex>","status":"pending","telegram_user_id":"90001","fingerprint":"ABCD-EF01-2345-6789"}
```

The verification code is the first16 uppercase hex characters of SHA256(`ALLOWGRAM_CONNECTION_V1\n` + token), grouped by4. It is not a login token. Status is `pending`, `approved` or `rejected`. Approved responses additionally contain `device_id` and the existing signed `policy` envelope. A revoked device returns rejected without policy. Server IDs, subject, fingerprint, exact response shape, signature, device binding and policy revision are checked before applying. Existing verified persistence/ACK handling is reused; approval alone is not an applied ACK.

Owner routes, protected by session + exact Origin + CSRF:
- GET `/api/registrations`: whitelist metadata for live pending connections, no credentials/hashes.
- POST `/api/registrations/{id}/approve`: `{ "fingerprint": "<matching code>" }`.
- POST `/api/registrations/{id}/reject`: `{}`.

Approving an existing user never replaces an established policy with the device's snapshot. New users are seeded only at revision0. Rejection, approval and repeat requests are transactionally serialized. Rejected/revoked tokens cannot resurrect devices. Pending requests expire after seven days; public creation is limited to30/minute,250 pending and10000 total retained registrations, plus source-based API limits. Old API rate buckets are reclaimed rather than accumulating permanently.

The existing AGH1 invitation and signed policy/ACK protocol remains available. This document supplements PROTOCOL-v1; earlier non-publication planning constraints are historical, not proof of release approval or acceptance.

## Persistent production service

Run one process/replica with `/data` on a persistent volume. Docker builds only allowlisted source/dependencies and frontend assets. Never put runtime databases, login codes or signing keys in image context, Git, source archives or release assets.

Required variables: `HEAD_PRODUCTION=1`, `HEAD_DATA_DIR=/data`, `HEAD_PUBLIC_URL=<stable HTTPS origin>`, `HEAD_OWNER_ID`, `HEAD_EXPECTED_PUBLIC_KEY`, and optional `HEAD_BUILD_COMMIT` for readback. The runtime refuses a missing, wrong, non-private or mismatched management key rather than rotating it. An explicit one-time `HEAD_BOOTSTRAP_KEY` secret can initialize an empty volume **only if it matches the pinned public key**; remove that Railway secret after verified first boot. Its value is never logged or exposed by the app. Preserve a separate operator-controlled key backup. Health reports only public pin/version/build metadata. Railway's healthcheck hostname is allowed only for GET `/api/health` in production. Access logs are disabled.

The volume must survive restarts/redeployments; verify key identity and database persistence before publishing the desktop update. Services with volumes have a brief redeploy interruption. Login codes remain operator-issued, short-lived and single-use, not passwords or Telegram SSO. Do not publish a release on the strength of synthetic tests alone: full native build, independent review, package signature checks and authorized acceptance are separate gates.
