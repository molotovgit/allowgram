import base64
import json

from fastapi.testclient import TestClient
from test_board import ORIGIN, headers, login

from headboard import diagnostics
from headboard.api import create_app


def decode(text):
    return base64.urlsafe_b64decode(text + "=" * (-len(text) % 4))


def enroll(client, h, user_id="90001"):
    assert client.post("/api/users", json={"id": user_id, "label": "Synthetic Driver"}, headers=h).status_code == 201
    reply = client.post(f"/api/users/{user_id}/invitations", json={}, headers=h)
    invitation = json.loads(decode(reply.json()["invitation"].split(".", 1)[1]))
    enrolled = client.post("/api/client/enroll", json={
        "code": invitation["code"], "telegram_user_id": user_id,
        "initial_peers": [{"kind": "user", "id": "12345", "label": "Dispatcher"}],
        "device_name": "Synthetic Windows", "client_version": "test"})
    assert enrolled.status_code == 200, enrolled.text
    return enrolled.json()


def report(kind="logout", **extra):
    return {"v": 1, "kind": kind, "reason": "Telegram ended the session: AUTH_KEY_UNREGISTERED (401)",
            "at": 1795455297, "client_version": "7.2.8.15", "exe_path": "C:\\Users\\r\\Allowgram\\Allowgram.exe",
            "working_dir": "C:\\Users\\r\\AppData\\Roaming\\Allowgram", "portable": False,
            "previous": {"device_id": "x", "signed_in": True}, "events": "line\nline", "log_tail": "log", **extra}


def test_device_reports_and_owner_reads_them(tmp_path):
    with TestClient(create_app(tmp_path), base_url=ORIGIN) as client:
        owner, _ = login(client, tmp_path)
        h = headers(owner)
        e = enroll(client, h)
        device = {"Authorization": "Bearer " + e["device_token"]}
        assert client.post("/api/client/diagnostics", json=report()).status_code == 401
        assert client.post("/api/client/diagnostics", json=report(),
                           headers={"Authorization": "Bearer " + "x" * 43}).status_code == 401
        sent = client.post("/api/client/diagnostics", json=report(), headers=device)
        assert sent.status_code == 201, sent.text
        listing = client.get("/api/diagnostics?user_id=90001", headers=h).json()
        assert [r["kind"] for r in listing] == ["logout"] and listing[0]["device_id"] == e["device_id"]
        full = client.get(f"/api/diagnostics/{listing[0]['id']}", headers=h).json()
        assert full["report"]["reason"].startswith("Telegram ended the session") and full["report"]["log_tail"] == "log"
        assert any(a["action"] == "diagnostics.received" for a in client.get("/api/audit", headers=h).json())


def test_revoked_device_can_still_report(tmp_path):
    with TestClient(create_app(tmp_path), base_url=ORIGIN) as client:
        owner, _ = login(client, tmp_path)
        h = headers(owner)
        e = enroll(client, h)
        device = {"Authorization": "Bearer " + e["device_token"]}
        assert client.post(f"/api/users/90001/devices/{e['device_id']}/revoke", json={}, headers=h).status_code == 200
        assert client.get("/api/client/policy", headers=device).status_code == 401  # revoked for policy
        assert client.post("/api/client/diagnostics", json=report("login_missing"), headers=device).status_code == 201


def test_report_is_validated_and_rate_limited(tmp_path, monkeypatch):
    monkeypatch.setattr(diagnostics, "PER_DEVICE_PER_DAY", 2)
    with TestClient(create_app(tmp_path), base_url=ORIGIN) as client:
        owner, _ = login(client, tmp_path)
        e = enroll(client, headers(owner))
        device = {"Authorization": "Bearer " + e["device_token"]}
        assert client.post("/api/client/diagnostics", json=report("hacked"), headers=device).status_code == 422
        assert client.post("/api/client/diagnostics", json=report(extra_field=1), headers=device).status_code == 422
        assert client.post("/api/client/diagnostics", json=report(previous={"x": "y" * 30000}),
                           headers=device).status_code == 422
        assert client.post("/api/client/diagnostics", json=report(), headers=device).status_code == 201
        assert client.post("/api/client/diagnostics", json=report(), headers=device).status_code == 201
        assert client.post("/api/client/diagnostics", json=report(), headers=device).status_code == 429


def test_reading_needs_a_signed_in_owner_or_head(tmp_path):
    with TestClient(create_app(tmp_path), base_url=ORIGIN) as client:
        owner, _ = login(client, tmp_path)
        h = headers(owner)
        e = enroll(client, h)
        client.post("/api/client/diagnostics", json=report(), headers={"Authorization": "Bearer " + e["device_token"]})
        report_id = client.get("/api/diagnostics", headers=h).json()[0]["id"]
        assert client.get(f"/api/diagnostics/{report_id + 99}", headers=h).status_code == 404
        # A head sees only users in its scope; a device token is no way in.
        assert client.post("/api/heads", json={"id": "70001", "label": "Head One"}, headers=h).status_code == 201
        code = client.post("/api/heads/70001/access-code", json={}, headers=h).json()["code"]
        cookies = dict(client.cookies)
        client.cookies.clear()
        assert client.get("/api/diagnostics", headers={"Authorization": "Bearer " + e["device_token"]}).status_code == 401
        assert client.post("/api/auth/bootstrap", json={"code": code}, headers={"Origin": ORIGIN}).status_code == 200
        assert client.get("/api/diagnostics").json() == []
        assert client.get(f"/api/diagnostics/{report_id}").status_code == 404
        client.cookies.clear()
        client.cookies.update(cookies)
