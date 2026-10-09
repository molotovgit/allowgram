"""Synthetic Hub-service contract tests. Never opens a production DB or connection."""
import base64
import hashlib
import json
import secrets
from concurrent.futures import ThreadPoolExecutor

import pytest
from fastapi.testclient import TestClient
from headboard.api import create_app

SERVICE_KEY = "synthetic-hub-service-key-not-for-production-123456"
SERVICE = {"Authorization": "Bearer " + SERVICE_KEY}
SUBJECT = "900001"


def encoded(raw):
    return base64.urlsafe_b64encode(raw).decode().rstrip("=")


def enrollment(**overrides):
    token = secrets.token_urlsafe(32)
    body = {
        "tg_id": SUBJECT,
        "device_public_key": encoded(secrets.token_bytes(32)),
        "credential_hash": hashlib.sha256(token.encode("ascii")).hexdigest(),
        "device_name": "Synthetic Allowgram",
        "client_version": "hub-dev",
        "initial_peers": [{"kind": "user", "id": "900042", "label": "Dispatch"}],
        "hub_enrollment_id": secrets.token_hex(16),
    }
    body.update(overrides)
    return body, token


@pytest.fixture
def app(tmp_path):
    return create_app(tmp_path, public_url="https://head.test", hub_service_token=SERVICE_KEY)


@pytest.fixture
def client(app):
    with TestClient(app, base_url="https://head.test") as c:
        yield c


def enroll(client, body):
    r = client.post("/api/service/v1/enroll", headers=SERVICE, json=body)
    assert r.status_code == 200, r.text
    return r.json()


def detail(client):
    r = client.get(f"/api/service/v1/users/{SUBJECT}", headers=SERVICE)
    assert r.status_code == 200, r.text
    return r.json()


def test_service_disabled_without_explicit_key(tmp_path):
    a = create_app(tmp_path, public_url="https://head.test")
    with TestClient(a, base_url="https://head.test") as c:
        assert c.get(f"/api/service/v1/users/{SUBJECT}", headers=SERVICE).status_code == 503


@pytest.mark.parametrize("headers", [{}, {"Authorization": "Bearer incorrect"}, {"Cookie": "agh_session=forged"}])
def test_untrusted_callers_cannot_enroll_or_read(client, headers):
    body, _ = enrollment()
    assert client.post("/api/service/v1/enroll", json=body, headers=headers).status_code == 401
    assert client.get(f"/api/service/v1/users/{SUBJECT}", headers=headers).status_code == 401


def test_enrollment_seeds_once_and_keeps_only_hashed_credential(client, app):
    body, token = enrollment()
    result = enroll(client, body)
    row = detail(client)
    assert row["revision"] == 1
    assert row["peers"] == body["initial_peers"]
    assert row["devices"][0]["acked_revision"] == 0
    assert row["devices"][0]["device_id"] == result["device_id"]
    payload = json.loads(base64.urlsafe_b64decode(result["policy"]["signed"] + "=="))
    assert payload["peers"] == [{"kind": "user", "id": "900042"}]
    assert payload["telegram_user_id"] == SUBJECT
    with app.state.store.db() as db:
        assert db.execute("SELECT token_hash FROM devices").fetchone()[0] == body["credential_hash"]
    assert token.encode() not in app.state.store.path.read_bytes()
    response = client.get("/api/client/policy", headers={"Authorization": "Bearer " + token})
    assert response.status_code == 200
    assert response.json() == result["policy"]


def test_lost_response_retry_returns_same_device_current_policy(client, app):
    body, _ = enrollment()
    first = enroll(client, body)
    assert enroll(client, body) == first
    changed = {**body, "device_name": "Another name"}
    assert client.post("/api/service/v1/enroll", headers=SERVICE, json=changed).status_code == 409
    with app.state.store.db() as db:
        assert db.execute("SELECT count(*) FROM devices").fetchone()[0] == 1


def test_new_challenge_same_device_does_not_duplicate_it(client, app):
    body, _ = enrollment()
    first = enroll(client, body)
    second = enroll(client, {**body, "hub_enrollment_id": secrets.token_hex(16)})
    assert second == first
    with app.state.store.db() as db:
        assert db.execute("SELECT count(*) FROM devices").fetchone()[0] == 1


def test_second_device_never_overwrites_manager_policy(client):
    body, _ = enrollment()
    enroll(client, body)
    managed = [{"kind": "channel", "id": "900088", "label": "Announcements"}]
    response = client.put(f"/api/service/v1/users/{SUBJECT}/policy", headers=SERVICE,
                          json={"expected_revision": 1, "peers": managed, "actor_label": "Management"})
    assert response.status_code == 200, response.text
    assert response.json()["revision"] == 2
    second, _ = enrollment(initial_peers=[{"kind": "chat", "id": "4001"}])
    enroll(client, second)
    actual = detail(client)
    assert actual["revision"] == 2 and actual["peers"] == managed
    assert len(actual["devices"]) == 2
    assert {"kind": "chat", "id": "4001"} in actual["offered_peers"]


def test_compare_and_swap_and_deny_all_are_real_policy_changes(client):
    body, _ = enrollment()
    enroll(client, body)
    payload = {"expected_revision": 1, "peers": [], "actor_label": "Management"}
    first = client.put(f"/api/service/v1/users/{SUBJECT}/policy", headers=SERVICE, json=payload)
    assert first.status_code == 200, first.text
    stale = client.put(f"/api/service/v1/users/{SUBJECT}/policy", headers=SERVICE, json=payload)
    assert stale.status_code == 409
    assert detail(client)["revision"] == 2 and detail(client)["peers"] == []


def test_revoke_cannot_be_undone_by_replaying_enrollment(client):
    body, token = enrollment()
    result = enroll(client, body)
    response = client.post(f"/api/service/v1/users/{SUBJECT}/devices/{result['device_id']}/revoke",
                           headers=SERVICE, json={})
    assert response.status_code == 200, response.text
    assert client.get("/api/client/policy", headers={"Authorization": "Bearer " + token}).status_code == 401
    assert client.post("/api/service/v1/enroll", headers=SERVICE, json=body).status_code == 409
    assert client.post("/api/service/v1/enroll", headers=SERVICE,
                       json={**body, "hub_enrollment_id": secrets.token_hex(16)}).status_code == 409
    assert detail(client)["devices"][0]["revoked"] is True


def test_user_device_binding_prevents_cross_subject_revocation(client):
    body, _ = enrollment()
    result = enroll(client, body)
    response = client.post(f"/api/service/v1/users/900002/devices/{result['device_id']}/revoke",
                           headers=SERVICE, json={})
    assert response.status_code == 404
    assert detail(client)["devices"][0]["revoked"] is False


def test_batch_read_is_bounded_and_reports_missing(client):
    body, _ = enrollment()
    enroll(client, body)
    r = client.get("/api/service/v1/users?tg_id=900001,900002", headers=SERVICE)
    assert r.status_code == 200, r.text
    assert r.json()["missing"] == ["900002"]
    assert r.json()["users"][0]["tg_id"] == SUBJECT
    assert "peers" not in r.json()["users"][0]
    assert client.get("/api/service/v1/users?tg_id=900001,900001", headers=SERVICE).status_code == 422
    assert client.get("/api/service/v1/users?tg_id=01", headers=SERVICE).status_code == 422


@pytest.mark.parametrize("overrides", [
    {"tg_id": 900001}, {"tg_id": "01"}, {"credential_hash": "bad"},
    {"initial_peers": [{"kind": "chat", "id": "42"}, {"kind": "chat", "id": "42"}]},
    {"initial_peers": [{"kind": "group", "id": "42"}]},
    {"device_public_key": "bad"}, {"hub_enrollment_id": "bad"},
])
def test_malformed_enrollment_never_mutates(client, app, overrides):
    body, _ = enrollment(**overrides)
    assert client.post("/api/service/v1/enroll", headers=SERVICE, json=body).status_code == 422
    with app.state.store.db() as db:
        assert db.execute("SELECT count(*) FROM users").fetchone()[0] == 0


def test_concurrent_same_enrollment_is_one_device(client, app):
    body, _ = enrollment()
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(lambda _: enroll(client, body), range(4)))
    assert all(x == results[0] for x in results)
    with app.state.store.db() as db:
        assert db.execute("SELECT count(*) FROM devices").fetchone()[0] == 1


def test_adopts_existing_matching_device_without_profile_or_token_rotation(client, app):
    body, token = enrollment()
    from headboard.admin import set_policy
    with app.state.store.db() as db:
        db.execute("INSERT INTO users(id,label) VALUES(?,?)", (SUBJECT, "Legacy"))
        set_policy(db, SUBJECT, 1, [{"kind": "chat", "id": "777"}], "synthetic-owner")
        db.execute("INSERT INTO devices(id,token_hash,user_id,device_name,client_version,last_seen) VALUES(?,?,?,?,?,0)",
                   ("a" * 32, body["credential_hash"], SUBJECT, "Legacy device", "legacy"))
    result = enroll(client, body)
    assert result["device_id"] == "a" * 32
    assert detail(client)["peers"] == [{"kind": "chat", "id": "777"}]
    assert client.get("/api/client/policy", headers={"Authorization": "Bearer " + token}).status_code == 200


def test_cancel_minted_enrollment_revokes_only_its_device(client):
    body, token = enrollment()
    enroll(client, body)
    other, other_token = enrollment()
    enroll(client, other)
    url = f"/api/service/v1/enrollments/{body['hub_enrollment_id']}/cancel"
    result = client.post(url, headers=SERVICE, json={})
    assert result.status_code == 200, result.text
    assert result.json() == {"ok": True, "revoked": True}
    assert client.post(url, headers=SERVICE, json={}).json() == result.json()
    assert client.get("/api/client/policy", headers={"Authorization": "Bearer " + token}).status_code == 401
    assert client.get("/api/client/policy", headers={"Authorization": "Bearer " + other_token}).status_code == 200
    assert client.post("/api/service/v1/enroll", headers=SERVICE, json=body).status_code == 409


def test_cancel_before_enroll_blocks_late_inflight_request(client, app):
    body, _ = enrollment()
    url = f"/api/service/v1/enrollments/{body['hub_enrollment_id']}/cancel"
    assert client.post(url, headers=SERVICE, json={}).json() == {"ok": True, "revoked": False}
    assert client.post("/api/service/v1/enroll", headers=SERVICE, json=body).status_code == 409
    with app.state.store.db() as db:
        assert db.execute("SELECT count(*) FROM devices").fetchone()[0] == 0


def test_cancel_adoption_preserves_preexisting_device(client):
    body, token = enrollment()
    enroll(client, body)
    adopted = {**body, "hub_enrollment_id": secrets.token_hex(16)}
    enroll(client, adopted)
    url = f"/api/service/v1/enrollments/{adopted['hub_enrollment_id']}/cancel"
    assert client.post(url, headers=SERVICE, json={}).json() == {"ok": True, "revoked": False}
    assert client.get("/api/client/policy", headers={"Authorization": "Bearer " + token}).status_code == 200
    assert client.post("/api/service/v1/enroll", headers=SERVICE, json=adopted).status_code == 409


@pytest.mark.parametrize("headers", [{}, {"Authorization": "Bearer incorrect"}])
def test_cancel_requires_service_principal(client, headers):
    body, token = enrollment()
    enroll(client, body)
    url = f"/api/service/v1/enrollments/{body['hub_enrollment_id']}/cancel"
    assert client.post(url, headers=headers, json={}).status_code == 401
    assert client.get("/api/client/policy", headers={"Authorization": "Bearer " + token}).status_code == 200


def test_cancel_rejects_extra_body_and_malformed_id(client):
    assert client.post("/api/service/v1/enrollments/invalid/cancel", headers=SERVICE, json={}).status_code == 422
    assert client.post("/api/service/v1/enrollments/" + "c" * 32 + "/cancel", headers=SERVICE, json={"revoke_all": True}).status_code == 422


def test_last_fetch_is_not_reported_as_ack(client):
    body, token = enrollment()
    result = enroll(client, body)
    auth = {"Authorization": "Bearer " + token}
    assert client.get("/api/client/policy", headers=auth).status_code == 200
    assert detail(client)["devices"][0]["acked_revision"] == 0
    raw = base64.urlsafe_b64decode(result["policy"]["signed"] + "==")
    ack = {"revision": 1, "policy_sha256": hashlib.sha256(raw).hexdigest()}
    assert client.post("/api/client/ack", headers=auth, json=ack).status_code == 200
    assert detail(client)["devices"][0]["acked_revision"] == 1
