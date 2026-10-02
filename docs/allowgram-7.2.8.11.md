# Allowgram 7.2.8.11 — head-managed allowlists

This release adds optional, explicit enrollment with an Allowgram Head management service. Existing unmanaged accounts retain the local chat-picker workflow. Installing the client does not enroll an account or connect it to a management server automatically.

## Enroll deliberately

1. Sign in normally and complete the initial chat picker.
2. Obtain a one-use invitation from the owner or a head authorized for your account.
3. Open **Settings → Head-managed allowlist**. Check the account and management origin before confirming enrollment.

The invitation binds enrollment to the current account and pins the service origin and Ed25519 signing key. It is a capability issued by your manager, not Telegram SSO. Treat invitations and device credentials as secrets.

## What changes

- Authorized heads can grant or revoke conversations while the account remains signed in.
- The client checks signatures, account/device identity and revision ordering, then verifies saved settings before applying and acknowledging a policy.
- Ordinary local picker actions cannot replace a head-managed policy.
- An empty managed list denies all conversations; it does not reopen unmanaged onboarding.
- Revocation clears a disallowed active chat. A newer grant can take effect without replacing the session or logging out.
- Invalid, conflicting or older policy data cannot replace the last verified policy.
- Offline clients retain their last verified restrictions. Saving a desired server policy is not proof that an offline device has applied it.

These remain client-side restrictions. Other Telegram clients and devices are outside their scope. Software-update signing is independent of management-policy signing.

## Operational limits

Use a stable HTTPS management origin with protected signing-key/database backups and a recovery plan before enrolling real devices. Ephemeral development tunnels are for isolated QA only. Origin/key migration and a local unpairing UI are not implemented. Device revocation stops synchronization; it does not erase cached restrictions or remotely sign the account out.

The management service is deployed separately; the application has no hardcoded temporary management hostname. Its current authentication uses separately issued owner/head access codes, not Telegram SSO.

## Release and verification boundaries

Windows display/PE version is `7.2.8.11`; stable update sequence is `11`. The existing signed update feed and voluntary native restart behavior are retained. Update signatures are not Windows Authenticode publisher certificates.

The pre-release implementation passed 228 main native assertions, including 17 managed-session checks, plus 24 composer assertions in an offline synthetic fixture. Those counts describe the implementation candidate, not a claim that a later versioned binary, live Telegram account, macOS build or public update has passed acceptance. Exact final build/package evidence and platform availability belong to the corresponding release assets and notes.
