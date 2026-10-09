import asyncio
import hashlib
import hmac
import json
import secrets
import time

from fastapi import HTTPException, Query, Request, Response
from pydantic import Field, field_validator

from .admin import audit, set_policy, visible
from .models import Id, Model, Peer, PeerList
from .signing import Signer, b64, payload
from .store import digest


class Empty(Model):
    pass


class Enrollment(Model):
    code: str = Field(min_length=32, max_length=200)
    telegram_user_id: Id
    initial_peers: list[Peer] = Field(max_length=10000)
    device_name: str = Field(min_length=1, max_length=100)
    client_version: str = Field(min_length=1, max_length=40)

    @field_validator("initial_peers")
    @classmethod
    def validate_peers(cls, peers):
        return PeerList(peers=peers).peers


class Ack(Model):
    revision: int = Field(ge=1, le=9007199254740991)
    policy_sha256: str = Field(pattern=r"^[0-9a-f]{64}$")


def devices(db, user_id):
    return [
        dict(r)
        for r in db.execute(
            "SELECT id,device_name,client_version,revoked,last_seen,applied_revision,applied_at FROM devices WHERE user_id=? ORDER BY id",
            (user_id,),
        )
    ]


def register(app):
    store, principal = app.state.store, app.state.principal
    with store.db() as db:
        db.executescript("""
        CREATE TABLE IF NOT EXISTS invitations(id TEXT PRIMARY KEY, code_hash TEXT UNIQUE NOT NULL, user_id TEXT NOT NULL REFERENCES users(id), actor_id TEXT NOT NULL REFERENCES heads(id), expires INTEGER NOT NULL, used INTEGER NOT NULL DEFAULT 0, revoked INTEGER NOT NULL DEFAULT 0);
        CREATE TABLE IF NOT EXISTS devices(id TEXT PRIMARY KEY, token_hash TEXT UNIQUE NOT NULL, user_id TEXT NOT NULL REFERENCES users(id), device_name TEXT NOT NULL, client_version TEXT NOT NULL, revoked INTEGER NOT NULL DEFAULT 0, last_seen INTEGER NOT NULL, applied_revision INTEGER NOT NULL DEFAULT 0, applied_at INTEGER);
        """)
    signer = Signer(store)
    app.state.signer = signer

    def device_auth(db, request):
        auth = request.headers.get("authorization", "")
        if not auth.startswith("Bearer ") or not 32 <= len(auth[7:]) <= 200:
            raise HTTPException(401, "Device authorization required")
        row = db.execute(
            "SELECT * FROM devices WHERE token_hash=? AND revoked=0",
            (digest(auth[7:]),),
        ).fetchone()
        if not row:
            raise HTTPException(401, "Device authorization required")
        return row

    @app.post("/api/users/{user_id}/invitations", status_code=201)
    def invitation(user_id: str, body: Empty, request: Request):
        who = principal(request, write=True)
        with store.db() as db:
            visible(db, who, user_id)
            invitation_id, code = secrets.token_hex(16), secrets.token_urlsafe(32)
            expires = int(time.time()) + 900
            db.execute(
                "INSERT INTO invitations(id,code_hash,user_id,actor_id,expires) VALUES(?,?,?,?,?)",
                (invitation_id, digest(code), user_id, who["id"], expires),
            )
            audit(
                db,
                who["id"],
                user_id,
                "invitation.created",
                {"id": invitation_id, "expires": expires},
            )
        blob = {
            "v": 1,
            "url": app.state.public_url,
            "telegram_user_id": user_id,
            "code": code,
            "public_key": signer.public,
        }
        return {
            "id": invitation_id,
            "expires": expires,
            "invitation": "AGH1."
            + b64(json.dumps(blob, separators=(",", ":")).encode()),
        }

    @app.get("/api/users/{user_id}/invitations")
    def invitations(user_id: str, request: Request):
        who = principal(request)
        with store.db() as db:
            visible(db, who, user_id)
            return [
                dict(r)
                for r in db.execute(
                    "SELECT id,actor_id,expires,used,revoked FROM invitations WHERE user_id=? ORDER BY expires DESC LIMIT 50",
                    (user_id,),
                )
            ]

    @app.post("/api/users/{user_id}/invitations/{invitation_id}/revoke")
    def revoke_invitation(
        user_id: str, invitation_id: str, body: Empty, request: Request
    ):
        who = principal(request, write=True)
        with store.db() as db:
            visible(db, who, user_id)
            if not db.execute(
                "UPDATE invitations SET revoked=1 WHERE id=? AND user_id=?",
                (invitation_id, user_id),
            ).rowcount:
                raise HTTPException(404, "Invitation not found")
            audit(db, who["id"], user_id, "invitation.revoked", {"id": invitation_id})
        return {"ok": True}

    @app.post("/api/users/{user_id}/devices/{device_id}/revoke")
    def revoke_device(user_id: str, device_id: str, body: Empty, request: Request):
        who = principal(request, write=True)
        with store.db() as db:
            visible(db, who, user_id)
            if not db.execute(
                "UPDATE devices SET revoked=1 WHERE id=? AND user_id=?",
                (device_id, user_id),
            ).rowcount:
                raise HTTPException(404, "Device not found")
            audit(db, who["id"], user_id, "device.revoked", {"id": device_id})
        return {"ok": True}

    @app.post("/api/client/enroll")
    def enroll(body: Enrollment):
        with store.db() as db:
            grant = db.execute(
                "SELECT i.*,h.role,h.active FROM invitations i JOIN heads h ON h.id=i.actor_id WHERE i.code_hash=? AND i.user_id=? AND i.used=0 AND i.revoked=0 AND i.expires>? AND h.active=1",
                (digest(body.code), body.telegram_user_id, int(time.time())),
            ).fetchone()
            if not grant or (
                grant["role"] != "owner"
                and not db.execute(
                    "SELECT 1 FROM scopes WHERE head_id=? AND user_id=?",
                    (grant["actor_id"], body.telegram_user_id),
                ).fetchone()
            ):
                raise HTTPException(401, "Invalid or expired invitation")
            user = db.execute(
                "SELECT * FROM users WHERE id=?", (body.telegram_user_id,)
            ).fetchone()
            if user["revision"] == 0:
                set_policy(
                    db,
                    body.telegram_user_id,
                    1,
                    [p.model_dump(exclude_none=True) for p in body.initial_peers],
                    grant["actor_id"],
                )
                user = db.execute(
                    "SELECT * FROM users WHERE id=?", (body.telegram_user_id,)
                ).fetchone()
            device_id, token = secrets.token_hex(16), secrets.token_urlsafe(32)
            db.execute(
                "INSERT INTO devices(id,token_hash,user_id,device_name,client_version,last_seen) VALUES(?,?,?,?,?,?)",
                (
                    device_id,
                    digest(token),
                    body.telegram_user_id,
                    body.device_name,
                    body.client_version,
                    int(time.time()),
                ),
            )
            db.execute("UPDATE invitations SET used=1 WHERE id=?", (grant["id"],))
            audit(
                db,
                grant["actor_id"],
                body.telegram_user_id,
                "device.enrolled",
                {"device_id": device_id},
            )
            envelope = signer.envelope(user, device_id)
        return {"device_id": device_id, "device_token": token, "policy": envelope}

    @app.get("/api/client/policy")
    def policy(request: Request):
        with store.db() as db:
            device = device_auth(db, request)
            user = db.execute(
                "SELECT * FROM users WHERE id=?", (device["user_id"],)
            ).fetchone()
            db.execute(
                "UPDATE devices SET last_seen=? WHERE id=?",
                (int(time.time()), device["id"]),
            )
            return signer.envelope(user, device["id"])

    @app.get("/api/client/policy/wait")
    async def wait_policy(request: Request,
                          after_revision: int = Query(ge=0, le=9007199254740991),
                          timeout: int = Query(default=20, ge=0, le=25)):
        def snapshot():
            with store.read() as db:
                device = device_auth(db, request)
                user = db.execute("SELECT * FROM users WHERE id=?", (device["user_id"],)).fetchone()
                if after_revision > user["revision"]:
                    raise HTTPException(409, "Requested revision is ahead of the server")
                if user["revision"] > after_revision:
                    return signer.envelope(user, device["id"])
            return None

        def seen():
            with store.db() as db:
                device = device_auth(db, request)
                db.execute("UPDATE devices SET last_seen=? WHERE id=?",
                           (int(time.time()), device["id"]))

        deadline = time.monotonic() + timeout
        await asyncio.to_thread(seen)
        while True:
            envelope = await asyncio.to_thread(snapshot)
            if envelope is not None:
                return envelope
            remaining = deadline - time.monotonic()
            if remaining <= 0 or await request.is_disconnected():
                return Response(status_code=204)
            await asyncio.sleep(min(0.25, remaining))

    @app.post("/api/client/ack")
    def ack(body: Ack, request: Request):
        with store.db() as db:
            device = device_auth(db, request)
            policy = db.execute(
                "SELECT * FROM policies WHERE user_id=? AND revision=?",
                (device["user_id"], body.revision),
            ).fetchone()
            if not policy or not hmac.compare_digest(
                hashlib.sha256(payload(policy, device["id"])).hexdigest(),
                body.policy_sha256,
            ):
                raise HTTPException(
                    409, "Acknowledgement does not match an issued policy"
                )
            if body.revision < device["applied_revision"]:
                raise HTTPException(409, "Acknowledgement cannot go backwards")
            now = int(time.time())
            db.execute(
                "UPDATE devices SET applied_revision=?,applied_at=?,last_seen=? WHERE id=?",
                (body.revision, now, now, device["id"]),
            )
            if body.revision > device["applied_revision"]:
                audit(
                    db,
                    "device:" + device["id"],
                    device["user_id"],
                    "policy.acknowledged",
                    {"revision": body.revision},
                )
        return {"ok": True}
