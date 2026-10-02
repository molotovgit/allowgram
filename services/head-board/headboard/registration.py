"""Consent registrations are unverified device claims, not Telegram identities."""
import base64
import hashlib
import hmac
import json
import re
import secrets
import time

from fastapi import HTTPException, Request
from pydantic import Field, field_validator

from .admin import audit, set_policy
from .models import Id, Label, Model, Peer, PeerList
from .store import digest


MAX_PENDING = 250
MAX_REGISTRATIONS = 10000
MAX_NEW_PER_MINUTE = 30
PENDING_SECONDS = 7 * 86400


class Approval(Model):
    fingerprint: str = Field(pattern=r"^[0-9A-F]{4}(?:-[0-9A-F]{4}){3}$")


class Connection(Model):
    telegram_user_id: Id
    device_name: Label
    client_version: str = Field(min_length=1, max_length=40)
    consent_version: int = Field(ge=1, le=1)
    initial_peers: list[Peer] = Field(max_length=10000)

    @field_validator("initial_peers")
    @classmethod
    def validate_peers(cls, peers):
        return PeerList(peers=peers).peers


def token_from(request):
    auth = request.headers.get("authorization", "")
    if not auth.startswith("Bearer "):
        raise HTTPException(401, "Connection authorization required")
    token = auth[7:]
    if not re.fullmatch(r"[A-Za-z0-9_-]{43}", token):
        raise HTTPException(401, "Connection authorization required")
    decoded = base64.urlsafe_b64decode(token + "=")
    if base64.urlsafe_b64encode(decoded).decode().rstrip("=") != token:
        raise HTTPException(401, "Connection authorization required")
    return token


def fingerprint(token):
    code = hashlib.sha256(b"ALLOWGRAM_CONNECTION_V1\n" + token.encode()).hexdigest()[:16].upper()
    return "-".join(code[n:n+4] for n in range(0, 16, 4))


def register(app):
    store, principal = app.state.store, app.state.principal
    with store.db() as db:
        db.executescript("""
        CREATE TABLE IF NOT EXISTS registrations(
            id TEXT PRIMARY KEY, token_hash TEXT UNIQUE NOT NULL,
            user_id TEXT NOT NULL, device_name TEXT NOT NULL, client_version TEXT NOT NULL,
            initial_peers TEXT NOT NULL, request_hash TEXT NOT NULL, fingerprint TEXT NOT NULL,
            status TEXT NOT NULL DEFAULT 'pending', created_at INTEGER NOT NULL,
            device_id TEXT UNIQUE REFERENCES devices(id));
        CREATE INDEX IF NOT EXISTS registration_created ON registrations(created_at);
        """)

    def result(db, row):
        answer = {"v": 1, "id": row["id"], "status": row["status"],
                  "telegram_user_id": row["user_id"], "fingerprint": row["fingerprint"]}
        if row["status"] == "pending" and row["created_at"] < int(time.time()) - PENDING_SECONDS:
            db.execute("UPDATE registrations SET status='rejected',initial_peers='[]' WHERE id=?", (row["id"],))
            answer["status"] = "rejected"
        if row["status"] == "approved":
            device = db.execute("SELECT * FROM devices WHERE id=? AND revoked=0", (row["device_id"],)).fetchone()
            if not device:
                answer["status"] = "rejected"
            else:
                user = db.execute("SELECT * FROM users WHERE id=?", (row["user_id"],)).fetchone()
                answer.update(device_id=device["id"], policy=app.state.signer.envelope(user, device["id"]))
        return answer

    @app.post("/api/client/register")
    def connect(body: Connection, request: Request):
        token = token_from(request)
        try:
            peers = PeerList(peers=body.initial_peers).model_dump()["peers"]
        except ValueError:
            raise HTTPException(422, "Invalid selected chats")
        canonical = json.dumps({**body.model_dump(exclude={"peers"}), "initial_peers": peers},
                               sort_keys=True, separators=(",", ":"))
        request_hash = digest(canonical)
        with store.db() as db:
            row = db.execute("SELECT * FROM registrations WHERE token_hash=?", (digest(token),)).fetchone()
            if row:
                if not hmac.compare_digest(row["request_hash"], request_hash):
                    raise HTTPException(409, "Connection request cannot change")
                return result(db, row)
            now = int(time.time())
            db.execute("UPDATE registrations SET status='rejected',initial_peers='[]' WHERE status='pending' AND created_at<?", (now - PENDING_SECONDS,))
            total, pending_count, recent = db.execute("SELECT count(*),coalesce(sum(status='pending'),0),coalesce(sum(created_at>=?),0) FROM registrations", (now - 60,)).fetchone()
            if total >= MAX_REGISTRATIONS or pending_count >= MAX_PENDING or recent >= MAX_NEW_PER_MINUTE:
                raise HTTPException(429, "Connection capacity reached; retry later", headers={"Retry-After": "60"})
            if db.execute("SELECT 1 FROM devices WHERE token_hash=?", (digest(token),)).fetchone():
                raise HTTPException(409, "Connection request cannot change")
            rid = secrets.token_hex(16)
            db.execute("INSERT INTO registrations(id,token_hash,user_id,device_name,client_version,initial_peers,request_hash,fingerprint,created_at) VALUES(?,?,?,?,?,?,?,?,?)",
                       (rid, digest(token), body.telegram_user_id, body.device_name, body.client_version,
                        json.dumps(peers, separators=(",", ":")), request_hash, fingerprint(token), int(time.time())))
            return result(db, db.execute("SELECT * FROM registrations WHERE id=?", (rid,)).fetchone())

    @app.get("/api/client/registration")
    def status(request: Request):
        token = token_from(request)
        with store.db() as db:
            row = db.execute("SELECT * FROM registrations WHERE token_hash=?", (digest(token),)).fetchone()
            if not row:
                raise HTTPException(401, "Connection authorization required")
            return result(db, row)

    @app.post("/api/registrations/{rid}/approve")
    def approve(rid: str, body: Approval, request: Request):
        who = principal(request, write=True, owner=True)
        with store.db() as db:
            row = db.execute("SELECT * FROM registrations WHERE id=?", (rid,)).fetchone()
            if not row:
                raise HTTPException(404, "Connection not found")
            if not hmac.compare_digest(row["fingerprint"], body.fingerprint):
                raise HTTPException(409, "Verify the code on the user's device first")
            if row["status"] == "approved":
                if not db.execute("SELECT 1 FROM devices WHERE id=? AND revoked=0", (row["device_id"],)).fetchone():
                    raise HTTPException(409, "Connection revoked")
                return {"ok": True}
            if row["status"] != "pending" or row["created_at"] < int(time.time()) - PENDING_SECONDS:
                raise HTTPException(409, "Connection is no longer pending")
            user = db.execute("SELECT * FROM users WHERE id=?", (row["user_id"],)).fetchone()
            if not user:
                db.execute("INSERT INTO users(id,label) VALUES(?,?)", (row["user_id"], "Allowgram user " + row["user_id"]))
            if not user or user["revision"] == 0:
                set_policy(db, row["user_id"], 1, json.loads(row["initial_peers"]), who["id"])
            did = secrets.token_hex(16)
            db.execute("INSERT INTO devices(id,token_hash,user_id,device_name,client_version,last_seen) VALUES(?,?,?,?,?,?)",
                       (did, row["token_hash"], row["user_id"], row["device_name"], row["client_version"], int(time.time())))
            db.execute("UPDATE registrations SET status='approved',device_id=?,initial_peers='[]' WHERE id=?", (did, rid))
            audit(db, who["id"], row["user_id"], "connection.approved", {"id": rid, "device_id": did})
            return {"ok": True}

    @app.post("/api/registrations/{rid}/reject")
    def reject(rid: str, request: Request):
        who = principal(request, write=True, owner=True)
        with store.db() as db:
            row = db.execute("SELECT * FROM registrations WHERE id=?", (rid,)).fetchone()
            if not row:
                raise HTTPException(404, "Connection not found")
            if row["status"] == "approved":
                raise HTTPException(409, "Revoke the approved device instead")
            if row["status"] == "pending":
                db.execute("UPDATE registrations SET status='rejected',initial_peers='[]' WHERE id=?", (rid,))
                audit(db, who["id"], row["user_id"], "connection.rejected", {"id": rid})
            return {"ok": True}

    @app.get("/api/registrations")
    def pending(request: Request):
        principal(request, owner=True)
        with store.db() as db:
            db.execute("UPDATE registrations SET status='rejected',initial_peers='[]' WHERE status='pending' AND created_at<?", (int(time.time()) - PENDING_SECONDS,))
            return [{"id": r["id"], "telegram_user_id": r["user_id"], "device_name": r["device_name"],
                     "client_version": r["client_version"], "status": r["status"], "created_at": r["created_at"],
                     "fingerprint": r["fingerprint"], "identity_verified": False}
                    for r in db.execute("SELECT * FROM registrations WHERE status='pending' ORDER BY created_at,id LIMIT 250")]
