"""Device-reported chat lists for the Hub's one-click picker. Synthetic only."""
import pytest
from fastapi.testclient import TestClient
from headboard.api import create_app

from test_hub_service import SERVICE, SERVICE_KEY, SUBJECT, detail, enroll, enrollment


@pytest.fixture
def app(tmp_path):
    return create_app(tmp_path, public_url="https://head.test", hub_service_token=SERVICE_KEY)


@pytest.fixture
def client(app):
    with TestClient(app, base_url="https://head.test") as c:
        yield c


def stored(app, device_id):
    with app.state.store.db() as db:
        return db.execute("SELECT COUNT(*) FROM device_chats WHERE device_id=?", (device_id,)).fetchone()[0]


def device(client, **overrides):
    body, token = enrollment(**overrides)
    result = enroll(client, body)
    return result["device_id"], {"Authorization": "Bearer " + token}


def report(client, auth, chats):
    return client.put("/api/client/chats", headers=auth, json={"v": 1, "chats": chats})


CHATS = [
    {"kind": "user", "id": "900042", "label": "Dispatch"},
    {"kind": "chat", "id": "4001", "label": "Drivers group"},
    {"kind": "channel", "id": "1000000123", "label": "Announcements"},
]


def test_no_report_yet_is_empty(client):
    device(client)
    row = detail(client)
    assert row["chats"] == [] and row["chats_updated_at"] is None


def test_reported_chats_reach_the_hub_in_device_order(client):
    device_id, auth = device(client)
    assert report(client, auth, CHATS).status_code == 200
    row = detail(client)
    assert [(c["kind"], c["id"], c["label"]) for c in row["chats"]] == [(c["kind"], c["id"], c["label"]) for c in CHATS]
    assert all(c["device_id"] == device_id for c in row["chats"])
    assert isinstance(row["chats_updated_at"], int)


def test_a_new_report_replaces_the_old_one(client):
    _, auth = device(client)
    report(client, auth, CHATS)
    report(client, auth, CHATS[:1])
    assert [c["id"] for c in detail(client)["chats"]] == ["900042"]


@pytest.mark.parametrize("headers", [{}, {"Authorization": "Bearer " + "x" * 43}, SERVICE])
def test_only_an_enrolled_device_can_report(client, headers):
    device(client)
    assert client.put("/api/client/chats", headers=headers, json={"v": 1, "chats": CHATS}).status_code == 401


@pytest.mark.parametrize("chats", [
    [{"kind": "group", "id": "1", "label": "x"}],
    [{"kind": "user", "id": "01", "label": "x"}],
    [{"kind": "user", "id": "1"}],
    [{"kind": "user", "id": "1", "label": "bad\u0007"}],
    [{"kind": "user", "id": "1", "label": "x" * 129}],
    [{"kind": "user", "id": "1", "label": "a"}, {"kind": "user", "id": "1", "label": "b"}],
    [{"kind": "user", "id": str(i + 1), "label": "x"} for i in range(1001)],
])
def test_malformed_reports_are_refused_and_store_nothing(client, chats):
    _, auth = device(client)
    assert report(client, auth, chats).status_code == 422
    assert detail(client)["chats"] == []


def test_a_revoked_device_list_disappears(client, app):
    device_id, auth = device(client)
    report(client, auth, CHATS)
    assert stored(app, device_id) == 1
    assert client.post(f"/api/service/v1/users/{SUBJECT}/devices/{device_id}/revoke", headers=SERVICE, json={}).status_code == 200
    assert detail(client)["chats"] == []
    assert stored(app, device_id) == 0
    assert report(client, auth, CHATS).status_code == 401


def test_a_cancelled_enrollment_deletes_its_device_list(client, app):
    body, token = enrollment()
    device_id = enroll(client, body)["device_id"]
    report(client, {"Authorization": "Bearer " + token}, CHATS)
    r = client.post(f"/api/service/v1/enrollments/{body['hub_enrollment_id']}/cancel", headers=SERVICE, json={})
    assert r.status_code == 200 and r.json()["revoked"] is True
    assert stored(app, device_id) == 0 and detail(client)["chats"] == []


def test_canonical_ids_pass_through_unchanged(client):
    _, auth = device(client)
    canonical = [
        {"kind": "user", "id": "8558994389", "label": "Class A Assistant"},
        {"kind": "chat", "id": "4001", "label": "Basic group"},
        {"kind": "channel", "id": "2233445566", "label": "Supergroup"},
        {"kind": "channel", "id": "1000000123", "label": "Broadcast channel"},
    ]
    assert report(client, auth, canonical).status_code == 200
    assert [{k: c[k] for k in ("kind", "id", "label")} for c in detail(client)["chats"]] == canonical
    for bad in ("-1002233445566", "-4001", "+4001", "4001.0"):
        assert report(client, auth, [{"kind": "channel", "id": bad, "label": "x"}]).status_code == 422


def test_two_devices_merge_and_the_newest_label_wins(client):
    _, first = device(client)
    _, second = device(client)
    report(client, first, CHATS)
    report(client, second, [{"kind": "user", "id": "900042", "label": "Dispatch (renamed)"}, {"kind": "user", "id": "900077", "label": "New DM"}])
    chats = detail(client)["chats"]
    assert [(c["id"], c["label"]) for c in chats] == [
        ("900042", "Dispatch (renamed)"), ("900077", "New DM"), ("4001", "Drivers group"), ("1000000123", "Announcements")]


def test_the_batch_read_stays_light(client):
    _, auth = device(client)
    report(client, auth, CHATS)
    users = client.get(f"/api/service/v1/users?tg_id={SUBJECT}", headers=SERVICE).json()["users"]
    assert "chats" not in users[0]
