import base64
import json
import os

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

DOMAIN = b"ALLOWGRAM_HEAD_POLICY_V1\n"


def b64(value):
    return base64.urlsafe_b64encode(value).decode().rstrip("=")


def payload(policy, device_id):
    policy = dict(policy)  # sqlite3.Row membership otherwise searches values.
    peers = [{"kind": p["kind"], "id": p["id"]} for p in json.loads(policy["peers"])]
    return json.dumps(
        {
            "v": 1,
            "telegram_user_id": policy["user_id"]
            if "user_id" in policy
            else policy["id"],
            "device_id": device_id,
            "revision": policy["revision"],
            "issued_at": policy["updated_at"],
            "peers": peers,
        },
        sort_keys=True,
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode()


class Signer:
    def __init__(self, store):
        path = store.directory / "policy-ed25519.key"
        with store.db() as db:
            if not path.exists():
                if db.execute("SELECT COUNT(*) FROM devices").fetchone()[0]:
                    raise RuntimeError(
                        "Management key missing: restore the private backup; refusing key rotation"
                    )
                key = Ed25519PrivateKey.generate()
                fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
                with os.fdopen(fd, "wb") as out:
                    out.write(key.private_bytes_raw())
                    out.flush()
                    os.fsync(out.fileno())
            if path.stat().st_mode & 0o077:
                raise RuntimeError("Management key permissions must be0600")
            self.key = Ed25519PrivateKey.from_private_bytes(path.read_bytes())
        self.public = b64(self.key.public_key().public_bytes_raw())

    def envelope(self, policy, device_id):
        raw = payload(policy, device_id)
        return {"signed": b64(raw), "signature": b64(self.key.sign(DOMAIN + raw))}
