import subprocess
import sys

from fastapi.testclient import TestClient

from headboard.api import create_app

ORIGIN = "http://127.0.0.1:28444"


def login(client, directory):
    output = directory / "owner-code.txt"
    command = subprocess.run(
        [
            sys.executable,
            "-m",
            "headboard.cli",
            "--data",
            str(directory),
            "owner-login",
            "--output",
            str(output),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    assert command.returncode == 0, command.stderr
    code = output.read_text().strip()
    assert code not in command.stdout
    response = client.post(
        "/api/auth/bootstrap", json={"code": code}, headers={"Origin": ORIGIN}
    )
    assert response.status_code == 200, response.text
    return response.json(), code


def test_owner_login_uses_private_single_use_code(tmp_path):
    with TestClient(create_app(tmp_path), base_url=ORIGIN) as client:
        session, code = login(client, tmp_path)
        assert session["role"] == "owner"
        assert session["id"] == "8683512953"
        assert session["csrf"]
        assert client.get("/api/session").json()["id"] == session["id"]
        assert (
            client.post(
                "/api/auth/bootstrap", json={"code": code}, headers={"Origin": ORIGIN}
            ).status_code
            == 401
        )
        assert (tmp_path / "owner-code.txt").stat().st_mode & 0o777 == 0o600


def headers(session):
    return {"Origin": ORIGIN, "X-CSRF-Token": session["csrf"]}


def test_owner_manages_users_scopes_and_revision_conflicts(tmp_path):
    with TestClient(create_app(tmp_path), base_url=ORIGIN) as client:
        session, _ = login(client, tmp_path)
        h = headers(session)
        assert client.get("/api/users").json() == []
        assert (
            client.post(
                "/api/heads", json={"id": "70001", "label": "Head One"}, headers=h
            ).status_code
            == 201
        )
        assert (
            client.post(
                "/api/users", json={"id": "90001", "label": "Driver One"}, headers=h
            ).status_code
            == 201
        )
        assert (
            client.put(
                "/api/users/90001/heads", json={"head_ids": ["70001"]}, headers=h
            ).status_code
            == 200
        )
        peers = [{"kind": "user", "id": "12345", "label": "Dispatcher"}]
        changed = client.put(
            "/api/users/90001/policy",
            json={"expected_revision": 0, "peers": peers},
            headers=h,
        )
        assert changed.status_code == 200, changed.text
        assert changed.json()["revision"] == 1
        assert (
            client.put(
                "/api/users/90001/policy",
                json={"expected_revision": 0, "peers": []},
                headers=h,
            ).status_code
            == 409
        )
        detail = client.get("/api/users/90001").json()
        assert detail["peers"] == peers
        assert detail["head_ids"] == ["70001"]
        assert (
            client.put(
                "/api/users/90001/policy",
                json={"expected_revision": 1, "peers": []},
                headers=h,
            ).json()["revision"]
            == 2
        )
        assert client.get("/api/users/90001").json()["peers"] == []
        assert client.get("/api/audit").json()


def test_health_has_no_private_data(tmp_path):
    with TestClient(create_app(tmp_path), base_url=ORIGIN) as client:
        response = client.get("/api/health")
        assert response.status_code == 200
        assert response.json() == {
            "ok": True,
            "service": "Allowgram Head",
            "version": "0.1.0",
        }
