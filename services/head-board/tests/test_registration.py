import secrets

from fastapi.testclient import TestClient
from test_board import ORIGIN, headers, login

from headboard.api import create_app


def registration(subject="90001"):
    token = secrets.token_urlsafe(32)
    body = {
        "telegram_user_id": subject,
        "initial_peers": [{"kind": "user", "id": "12345"}],
        "device_name": "Synthetic Windows",
        "client_version": "7.2.8.12-test",
        "consent_version": 1,
    }
    return token, {"Authorization": "Bearer " + token}, body


def test_consent_registers_private_pending_connection_without_authority(tmp_path):
    app = create_app(tmp_path)
    with TestClient(app, base_url=ORIGIN) as client:
        token, auth, body = registration()
        response = client.post("/api/client/register", headers=auth, json=body)
        assert response.status_code == 200, response.text
        pending = response.json()
        assert pending["status"] == "pending"
        assert pending["telegram_user_id"] == "90001"
        assert "policy" not in pending and token not in response.text
        assert client.get("/api/client/policy", headers=auth).status_code == 401
        assert client.get("/api/registrations").status_code == 401
        assert client.get("/api/client/registration", headers=auth).json() == pending
        assert client.post("/api/client/register", headers=auth, json=body).json() == pending
        owner, _ = login(client, tmp_path)
        rows = client.get("/api/registrations").json()
        assert len(rows) == 1 and rows[0]["id"] == pending["id"]
        assert rows[0]["fingerprint"] == pending["fingerprint"]
        assert rows[0]["identity_verified"] is False
        assert "token_hash" not in rows[0] and token not in str(rows)
        assert client.get("/api/users").json() == []
        with app.state.store.db() as db:
            assert db.execute("SELECT count(*) FROM devices").fetchone()[0] == 0
            assert db.execute("SELECT count(*) FROM heads").fetchone()[0] == 1
        assert client.post("/api/client/register", headers=auth, json={**body, "telegram_user_id": "90002"}).status_code == 409


def test_owner_approval_promotes_only_confirmed_device_and_revocation_is_final(tmp_path):
    from test_wire import decode
    import hashlib
    import json

    with TestClient(create_app(tmp_path), base_url=ORIGIN) as client:
        owner, _ = login(client, tmp_path)
        h = headers(owner)
        token, auth, body = registration()
        pending = client.post("/api/client/register", headers=auth, json=body).json()
        route = "/api/registrations/" + pending["id"]
        proof = {"fingerprint": pending["fingerprint"]}
        assert client.post(route + "/approve", headers=h, json={"fingerprint": "0000-0000-0000-0000"}).status_code == 409
        approved = client.post(route + "/approve", headers=h, json=proof)
        assert approved.status_code == 200, approved.text
        assert client.post(route + "/approve", headers=h, json=proof).status_code == 200
        received = client.get("/api/client/registration", headers=auth).json()
        assert received["status"] == "approved"
        payload = json.loads(decode(received["policy"]["signed"]))
        assert payload["telegram_user_id"] == "90001"
        assert payload["device_id"] == received["device_id"]
        assert payload["peers"] == body["initial_peers"]
        assert client.get("/api/client/policy", headers=auth).json() == received["policy"]
        assert client.post("/api/client/ack", headers=auth, json={"revision": 1, "policy_sha256": hashlib.sha256(decode(received["policy"]["signed"])).hexdigest()}).status_code == 200
        assert len(client.get("/api/users/90001").json()["devices"]) == 1
        assert client.post("/api/users/90001/devices/" + received["device_id"] + "/revoke", headers=h, json={}).status_code == 200
        assert client.get("/api/client/policy", headers=auth).status_code == 401
        assert client.get("/api/client/registration", headers=auth).json()["status"] == "rejected"
        assert client.post("/api/client/register", headers=auth, json=body).json()["status"] == "rejected"
        assert client.post(route + "/approve", headers=h, json=proof).status_code == 409
        assert len(client.get("/api/users/90001").json()["devices"]) == 1
