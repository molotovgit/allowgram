import json

import pytest
from fastapi.testclient import TestClient
from test_board import ORIGIN, headers, login

from headboard.api import create_app


@pytest.fixture
def configured(tmp_path):
    app = create_app(tmp_path)
    with TestClient(app, base_url=ORIGIN) as owner:
        s, _ = login(owner, tmp_path)
        h = headers(s)
        for who in ("70001", "70002"):
            assert (
                owner.post(
                    "/api/heads", json={"id": who, "label": "Head " + who}, headers=h
                ).status_code
                == 201
            )
        for who in ("90001", "90002"):
            assert (
                owner.post(
                    "/api/users", json={"id": who, "label": "User " + who}, headers=h
                ).status_code
                == 201
            )
        assert (
            owner.put(
                "/api/users/90001/heads", json={"head_ids": ["70001"]}, headers=h
            ).status_code
            == 200
        )
        access = owner.post("/api/heads/70001/access-code", json={}, headers=h)
        assert access.status_code == 201, access.text
        with TestClient(app, base_url=ORIGIN) as head:
            signed = head.post(
                "/api/auth/bootstrap",
                json={"code": access.json()["code"]},
                headers={"Origin": ORIGIN},
            )
            assert signed.status_code == 200
            assert signed.json()["role"] == "head"
            yield app, owner, h, head, headers(signed.json())


@pytest.mark.parametrize(
    "method,path,body,expected",
    [
        ("get", "/api/heads", None, 403),
        ("post", "/api/heads", {"id": "55555", "label": "Escalation"}, 403),
        ("post", "/api/heads/70002/access-code", {}, 403),
        ("patch", "/api/heads/70002", {"label": "Overwrite", "active": False}, 403),
        ("post", "/api/users", {"id": "88888", "label": "Unauthorized"}, 403),
        ("get", "/api/users/90002", None, 404),
        ("put", "/api/users/90002/policy", {"expected_revision": 0, "peers": []}, 404),
        ("put", "/api/users/90001/heads", {"head_ids": ["70002"]}, 403),
        ("post", "/api/users/90002/invitations", {}, 404),
        ("get", "/api/users/90002/invitations", None, 404),
        ("post", "/api/users/90002/invitations/unknown/revoke", {}, 404),
        ("post", "/api/users/90002/devices/unknown/revoke", {}, 404),
    ],
)
def test_head_cannot_cross_scope(configured, method, path, body, expected):
    _app, _owner, _h, head, hh = configured
    response = head.request(
        method, path, headers=hh, **({"json": body} if body is not None else {})
    )
    assert response.status_code == expected, response.text
    assert [u["id"] for u in head.get("/api/users").json()] == ["90001"]
    assert all(a["user_id"] == "90001" for a in head.get("/api/audit").json())


def test_scope_and_head_revocation_are_immediate(configured):
    _app, owner, h, head, hh = configured
    invite = head.post("/api/users/90001/invitations", json={}, headers=hh).json()[
        "invitation"
    ]
    import base64

    p = invite.split(".", 1)[1]
    p = json.loads(base64.urlsafe_b64decode(p + "=" * (-len(p) % 4)))
    assert (
        owner.put(
            "/api/users/90001/heads", json={"head_ids": []}, headers=h
        ).status_code
        == 200
    )
    assert head.get("/api/users/90001").status_code == 404
    assert (
        head.post(
            "/api/client/enroll",
            json={
                "code": p["code"],
                "telegram_user_id": "90001",
                "initial_peers": [],
                "device_name": "Synthetic",
                "client_version": "test",
            },
        ).status_code
        == 401
    )
    assert (
        owner.patch(
            "/api/heads/70001", json={"label": "Head", "active": False}, headers=h
        ).status_code
        == 200
    )
    assert head.get("/api/session").status_code == 401


def test_browser_security_and_safe_errors(tmp_path):
    with TestClient(create_app(tmp_path), base_url=ORIGIN) as c:
        assert c.get("/api/users").status_code == 401
        s, _ = login(c, tmp_path)
        h = headers(s)
        assert (
            c.post(
                "/api/users",
                json={"id": "90001", "label": "One"},
                headers={"Origin": ORIGIN},
            ).status_code
            == 403
        )
        assert (
            c.post(
                "/api/users",
                json={"id": "90001", "label": "One"},
                headers={**h, "Origin": "https://evil.example"},
            ).status_code
            == 403
        )
        assert c.post("/api/users", content="{}", headers=h).status_code == 415
        assert (
            c.post(
                "/api/users",
                content='{"id":"90001","id":"90002","label":"Dup"}',
                headers={**h, "Content-Type": "application/json"},
            ).status_code
            == 400
        )
        assert (
            c.post(
                "/api/users",
                json={"id": "90001", "label": "One", "role": "owner"},
                headers=h,
            ).status_code
            == 422
        )
        assert (
            c.patch(
                "/api/heads/8683512953",
                json={"label": "Owner", "active": False},
                headers=h,
            ).status_code
            == 409
        )
        for bad in ["0", "01", "-100123", "1.2", "281474976710656", "١٢٣"]:
            assert (
                c.post(
                    "/api/users", json={"id": bad, "label": "Bad"}, headers=h
                ).status_code
                == 422
            )
        assert c.get("/api/health", headers={"Host": "evil.example"}).status_code == 400
        assert c.get("/api/users").headers["cache-control"] == "no-store"
        assert c.get("/api/users").headers["x-content-type-options"] == "nosniff"
        assert c.post("/api/auth/logout", json={}, headers=h).status_code == 200
        assert c.get("/api/session").status_code == 401


def test_https_cookie_and_local_auth_rate_limit(tmp_path):
    public = "https://board.example"
    app = create_app(tmp_path, public_url=public)
    app.state.store.issue_owner_code(tmp_path / "private-code")
    with TestClient(app, base_url=public) as c:
        response = c.post(
            "/api/auth/bootstrap",
            json={"code": (tmp_path / "private-code").read_text().strip()},
            headers={"Origin": public},
        )
        assert response.status_code == 200
        cookie = response.headers["set-cookie"].lower()
        assert (
            "secure" in cookie and "httponly" in cookie and "samesite=strict" in cookie
        )
        statuses = [
            c.post(
                "/api/auth/bootstrap",
                json={"code": "x" * 43},
                headers={"Origin": public},
            ).status_code
            for _ in range(21)
        ]
        assert 429 in statuses


def test_restart_preserves_lists_auth_and_key(tmp_path):
    first = create_app(tmp_path)
    with TestClient(first, base_url=ORIGIN) as c:
        s, _ = login(c, tmp_path)
        h = headers(s)
        c.post("/api/users", json={"id": "90001", "label": "Persisted"}, headers=h)
        c.put(
            "/api/users/90001/policy",
            json={"expected_revision": 0, "peers": []},
            headers=h,
        )
        cookies = dict(c.cookies)
    second = create_app(tmp_path)
    assert first.state.signer.public == second.state.signer.public
    with TestClient(second, base_url=ORIGIN) as c:
        c.cookies.update(cookies)
        assert c.get("/api/users/90001").json()["revision"] == 1
