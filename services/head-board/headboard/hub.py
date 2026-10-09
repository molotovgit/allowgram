"""Dedicated Hub principal; Telegram proof and staff scope are verified by Hub."""
import base64
import json
import re
import secrets
import time

from fastapi import HTTPException
from pydantic import Field, field_validator

from .admin import audit, set_policy
from .models import Id, Label, Model, Peer, PeerList, PolicyEdit, valid_id
from .store import digest
from .signing import b64
from .wire import Empty, devices


class HubPolicyEdit(PolicyEdit):
    actor_label: Label


class HubEnrollment(Model):
    tg_id: Id
    device_public_key: str = Field(pattern=r"^[A-Za-z0-9_-]{43}$")
    credential_hash: str = Field(pattern=r"^[0-9a-f]{64}$")
    device_name: str = Field(min_length=1, max_length=100)
    client_version: str = Field(min_length=1, max_length=40)
    initial_peers: list[Peer] = Field(max_length=10000)
    hub_enrollment_id: str = Field(pattern=r"^[0-9a-f]{32}$")

    @field_validator("device_public_key")
    @classmethod
    def canonical_key(cls, value):
        raw = base64.b64decode(value + "=", altchars=b"-_", validate=True)
        if len(raw) != 32 or b64(raw) != value:
            raise ValueError("Invalid device public key")
        return value

    @field_validator("initial_peers")
    @classmethod
    def unique_peers(cls, value):
        return PeerList(peers=value).peers


def subject(db, tg_id):
    try:
        valid_id(tg_id)
    except ValueError:
        raise HTTPException(404, "User not found") from None
    row = db.execute("SELECT * FROM users WHERE id=?", (tg_id,)).fetchone()
    if not row:
        raise HTTPException(404, "User not found")
    return row


def device_status(db, tg_id):
    return [{
        "device_id": row["id"], "name": row["device_name"],
        "client_version": row["client_version"], "last_seen": row["last_seen"],
        "acked_revision": row["applied_revision"], "acked_at": row["applied_at"],
        "revoked": bool(row["revoked"]), "last_error": None,
    } for row in devices(db, tg_id)]


def register(app):
    store, signer = app.state.store, app.state.signer
    with store.db() as db:
        db.executescript("""
        CREATE TABLE IF NOT EXISTS hub_device_keys(
            user_id TEXT NOT NULL REFERENCES users(id),
            public_key TEXT NOT NULL,
            device_id TEXT UNIQUE NOT NULL REFERENCES devices(id),
            initial_peers TEXT NOT NULL,
            PRIMARY KEY(user_id,public_key)
        );
        CREATE TABLE IF NOT EXISTS hub_enrollments(
            id TEXT PRIMARY KEY,
            request_hash TEXT NOT NULL,
            device_id TEXT NOT NULL REFERENCES devices(id),
            minted_device INTEGER NOT NULL DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS hub_cancelled_enrollments(
            id TEXT PRIMARY KEY,
            cancelled_at INTEGER NOT NULL
        );
        """)
        if "minted_device" not in {row["name"] for row in db.execute("PRAGMA table_info(hub_enrollments)")} :
            db.execute("ALTER TABLE hub_enrollments ADD COLUMN minted_device INTEGER NOT NULL DEFAULT 0")

    @app.post("/api/service/v1/enroll")
    def enroll(body: HubEnrollment):
        peers = [p.model_dump(exclude_none=True) for p in body.initial_peers]
        request_hash = digest(body.model_dump_json(exclude_none=True))
        with store.db() as db:
            # A cancelled enrollment ID is final: a late or replayed request can never mint or reuse a device for it.
            if db.execute("SELECT 1 FROM hub_cancelled_enrollments WHERE id=?",
                          (body.hub_enrollment_id,)).fetchone():
                raise HTTPException(409, "Enrollment cancelled")
            receipt = db.execute("SELECT * FROM hub_enrollments WHERE id=?",
                                 (body.hub_enrollment_id,)).fetchone()
            if receipt and receipt["request_hash"] != request_hash:
                raise HTTPException(409, "Enrollment ID already used")
            binding = db.execute("SELECT * FROM hub_device_keys WHERE user_id=? AND public_key=?",
                                 (body.tg_id, body.device_public_key)).fetchone()
            device = db.execute("SELECT * FROM devices WHERE token_hash=?",
                                (body.credential_hash,)).fetchone()
            if binding and (not device or binding["device_id"] != device["id"]):
                raise HTTPException(409, "Device credential changed")
            if receipt and (not device or receipt["device_id"] != device["id"]):
                raise HTTPException(409, "Enrollment binding changed")
            if device:
                if device["user_id"] != body.tg_id or device["revoked"]:
                    raise HTTPException(409, "Device unavailable")
                old_binding = db.execute("SELECT * FROM hub_device_keys WHERE device_id=?",
                                         (device["id"],)).fetchone()
                if old_binding and old_binding["public_key"] != body.device_public_key:
                    raise HTTPException(409, "Device key changed")
                device_id = device["id"]
            else:
                db.execute("INSERT OR IGNORE INTO users(id,label) VALUES(?,?)",
                           (body.tg_id, body.tg_id))
                device_id = secrets.token_hex(16)
                db.execute(
                    "INSERT INTO devices(id,token_hash,user_id,device_name,client_version,last_seen) VALUES(?,?,?,?,?,?)",
                    (device_id, body.credential_hash, body.tg_id, body.device_name,
                     body.client_version, int(time.time())),
                )
                audit(db, "hub", body.tg_id, "device.enrolled", {"device_id": device_id})
            user = subject(db, body.tg_id)
            if user["revision"] == 0:
                set_policy(db, body.tg_id, 1, peers, "hub")
                user = subject(db, body.tg_id)
            if not binding:
                db.execute("INSERT INTO hub_device_keys VALUES(?,?,?,?)",
                           (body.tg_id, body.device_public_key, device_id, json.dumps(peers)))
            if not receipt:
                db.execute("INSERT INTO hub_enrollments(id,request_hash,device_id,minted_device) VALUES(?,?,?,?)",
                           (body.hub_enrollment_id, request_hash, device_id, int(device is None)))
            return {"device_id": device_id, "policy": signer.envelope(user, device_id)}

    @app.post("/api/service/v1/enrollments/{hub_enrollment_id}/cancel")
    def cancel(hub_enrollment_id: str, body: Empty):
        if not re.fullmatch(r"[0-9a-f]{32}", hub_enrollment_id):
            raise HTTPException(422, "Invalid enrollment ID")
        with store.db() as db:
            # The tombstone comes first, so a cancel that races ahead of its own enroll still blocks it.
            db.execute("INSERT OR IGNORE INTO hub_cancelled_enrollments(id,cancelled_at) VALUES(?,?)",
                       (hub_enrollment_id, int(time.time())))
            row = db.execute(
                "SELECT e.minted_device,d.id,d.user_id,d.revoked FROM hub_enrollments e "
                "JOIN devices d ON d.id=e.device_id WHERE e.id=?", (hub_enrollment_id,)
            ).fetchone()
            revoked = bool(row and row["minted_device"])
            if revoked and not row["revoked"]:
                db.execute("UPDATE devices SET revoked=1 WHERE id=?", (row["id"],))
                audit(db, "hub", row["user_id"], "device.revoked", {"device_id": row["id"]})
        return {"ok": True, "revoked": revoked}

    @app.get("/api/service/v1/users")
    def batch(tg_id: str):
        ids = tg_id.split(",")
        try:
            if not 1 <= len(ids) <= 200 or len(ids) != len(set(ids)):
                raise ValueError()
            for value in ids:
                valid_id(value)
        except ValueError:
            raise HTTPException(422, "Use 1 to 200 unique Telegram IDs") from None
        users, missing = [], []
        with store.db() as db:
            for value in ids:
                row = db.execute("SELECT revision FROM users WHERE id=?", (value,)).fetchone()
                if row:
                    users.append({"tg_id": value, "revision": row["revision"],
                                  "devices": device_status(db, value)})
                else:
                    missing.append(value)
        return {"users": users, "missing": missing}

    @app.put("/api/service/v1/users/{tg_id}/policy")
    def edit_policy(tg_id: str, body: HubPolicyEdit):
        with store.db() as db:
            user = subject(db, tg_id)
            if user["revision"] != body.expected_revision:
                raise HTTPException(409, "Policy changed; reload before saving")
            revision = user["revision"] + 1
            set_policy(db, tg_id, revision,
                       [p.model_dump(exclude_none=True) for p in body.peers],
                       "hub:" + body.actor_label)
        return {"revision": revision}

    @app.post("/api/service/v1/users/{tg_id}/devices/{device_id}/revoke")
    def revoke(tg_id: str, device_id: str, body: Empty):
        with store.db() as db:
            subject(db, tg_id)
            row = db.execute("SELECT revoked FROM devices WHERE id=? AND user_id=?",
                             (device_id, tg_id)).fetchone()
            if not row:
                raise HTTPException(404, "Device not found")
            if not row["revoked"]:
                db.execute("UPDATE devices SET revoked=1 WHERE id=?", (device_id,))
                audit(db, "hub", tg_id, "device.revoked", {"device_id": device_id})
        return {"ok": True}

    @app.get("/api/service/v1/users/{tg_id}")
    def detail(tg_id: str):
        with store.db() as db:
            user = subject(db, tg_id)
            offered = {}
            for row in db.execute("SELECT initial_peers FROM hub_device_keys WHERE user_id=? ORDER BY device_id", (tg_id,)):
                for peer in json.loads(row["initial_peers"]):
                    offered[(peer["kind"], peer["id"])] = peer
            return {
                "tg_id": tg_id, "revision": user["revision"],
                "peers": json.loads(user["peers"]),
                "offered_peers": [offered[key] for key in sorted(offered)],
                "devices": device_status(db, tg_id),
            }
