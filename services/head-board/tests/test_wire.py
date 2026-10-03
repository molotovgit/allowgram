import base64
import hashlib
import json

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
from fastapi.testclient import TestClient
from test_board import ORIGIN, headers, login

from headboard.api import create_app


def decode(text):
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def test_live_policy_enrollment_ack_and_revocation(tmp_path):
    with TestClient(create_app(tmp_path), base_url=ORIGIN) as client:
        owner, _ = login(client, tmp_path)
        h = headers(owner)
        assert (
            client.post(
                "/api/users",
                json={"id": "90001", "label": "Synthetic Driver"},
                headers=h,
            ).status_code
            == 201
        )
        reply = client.post("/api/users/90001/invitations", json={}, headers=h)
        assert reply.status_code == 201, reply.text
        invitation = decode(reply.json()["invitation"].split(".", 1)[1])
        invitation = json.loads(invitation)
        init = {
            "code": invitation["code"],
            "telegram_user_id": "90001",
            "initial_peers": [{"kind": "user", "id": "12345", "label": "Dispatcher"}],
            "device_name": "Synthetic Windows",
            "client_version": "test",
        }
        assert (
            client.post(
                "/api/client/enroll", json={**init, "telegram_user_id": "90002"}
            ).status_code
            == 401
        )
        enrolled = client.post("/api/client/enroll", json=init)
        assert enrolled.status_code == 200, enrolled.text
        e = enrolled.json()
        device = {"Authorization": "Bearer " + e["device_token"]}
        public = Ed25519PublicKey.from_public_bytes(decode(invitation["public_key"]))
        raw = decode(e["policy"]["signed"])
        public.verify(
            decode(e["policy"]["signature"]), b"ALLOWGRAM_HEAD_POLICY_V1\n" + raw
        )
        payload = json.loads(raw)
        assert (
            payload["telegram_user_id"] == "90001"
            and payload["device_id"] == e["device_id"]
        )
        assert payload["revision"] == 1 and payload["peers"] == [
            {"kind": "user", "id": "12345"}
        ]
        assert client.post("/api/client/enroll", json=init).status_code == 401
        assert (
            client.get("/api/users/90001").json()["devices"][0]["applied_revision"] == 0
        )
        assert client.get("/api/client/policy", headers=device).json() == e["policy"]
        assert (
            client.post(
                "/api/client/ack",
                headers=device,
                json={"revision": 1, "policy_sha256": "0" * 64},
            ).status_code
            == 409
        )
        ack = {"revision": 1, "policy_sha256": hashlib.sha256(raw).hexdigest()}
        assert (
            client.post("/api/client/ack", headers=device, json=ack).status_code == 200
        )
        assert (
            client.get("/api/users/90001").json()["devices"][0]["applied_revision"] == 1
        )
        assert (
            client.put(
                "/api/users/90001/policy",
                headers=h,
                json={"expected_revision": 1, "peers": []},
            ).status_code
            == 200
        )
        latest = client.get("/api/client/policy", headers=device).json()
        newer = json.loads(decode(latest["signed"]))
        assert newer["revision"] == 2 and newer["peers"] == []
        assert (
            client.get("/api/users/90001").json()["devices"][0]["applied_revision"] == 1
        )
        assert (
            client.post(
                "/api/users/90001/devices/" + e["device_id"] + "/revoke",
                headers=h,
                json={},
            ).status_code
            == 200
        )
        assert client.get("/api/client/policy", headers=device).status_code == 401
        assert (
            client.post("/api/client/ack", headers=device, json=ack).status_code == 401
        )
