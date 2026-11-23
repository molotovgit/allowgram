"""Logout diagnostics sent by Allowgram clients (7.2.8.15+).

A client sends a report when its account is logged out, when it starts without a login on a computer that had one,
when it notices that another Allowgram copy was used last, or when the user presses "Send diagnostic logs". It
authenticates with the head device token of the last signed-in account on that computer - also a REVOKED device's
token, because a revoke is exactly when a report may be needed. Reports are display data for the owner and heads:
never signed, never part of a policy.
"""

import json
import time
from typing import Literal

from fastapi import HTTPException, Query, Request
from pydantic import Field

from .admin import audit, visible
from .models import Model
from .store import digest

PER_DEVICE_PER_DAY = 60
KEEP_PER_USER = 300


class Report(Model):
    v: Literal[1]
    kind: Literal["logout", "login_missing", "another_copy", "manual"]
    reason: str = Field(max_length=600)
    at: int = Field(ge=0, le=9007199254740991)
    client_version: str = Field(min_length=1, max_length=40)
    exe_path: str = Field(max_length=2000)
    working_dir: str = Field(max_length=2000)
    portable: bool
    previous: dict = Field(default_factory=dict)
    events: str = Field(max_length=300_000)
    log_tail: str = Field(max_length=400_000)
    user_id: str = Field(default="", max_length=20)


def register(app):
    store, principal = app.state.store, app.state.principal
    with store.db() as db:
        db.executescript("""
        CREATE TABLE IF NOT EXISTS diagnostics(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            received_at INTEGER NOT NULL,
            device_id TEXT NOT NULL REFERENCES devices(id),
            user_id TEXT NOT NULL REFERENCES users(id),
            kind TEXT NOT NULL,
            reason TEXT NOT NULL,
            client_version TEXT NOT NULL,
            exe_path TEXT NOT NULL,
            portable INTEGER NOT NULL,
            body TEXT NOT NULL);
        CREATE INDEX IF NOT EXISTS diagnostics_user ON diagnostics(user_id, id);
        CREATE INDEX IF NOT EXISTS diagnostics_device ON diagnostics(device_id, received_at);
        """)

    @app.post("/api/client/diagnostics", status_code=201)
    def receive(body: Report, request: Request):
        auth = request.headers.get("authorization", "")
        if not auth.startswith("Bearer ") or not 32 <= len(auth[7:]) <= 200:
            raise HTTPException(401, "Device authorization required")
        if len(json.dumps(body.previous)) > 20_000:
            raise HTTPException(422, "Previous state too large")
        now = int(time.time())
        with store.db() as db:
            device = db.execute(
                "SELECT id,user_id FROM devices WHERE token_hash=?", (digest(auth[7:]),)
            ).fetchone()
            if not device:
                raise HTTPException(401, "Device authorization required")
            recent = db.execute(
                "SELECT COUNT(*) FROM diagnostics WHERE device_id=? AND received_at>?",
                (device["id"], now - 86400),
            ).fetchone()[0]
            if recent >= PER_DEVICE_PER_DAY:
                raise HTTPException(429, "Too many diagnostics today")
            cursor = db.execute(
                "INSERT INTO diagnostics(received_at,device_id,user_id,kind,reason,client_version,exe_path,portable,body) "
                "VALUES(?,?,?,?,?,?,?,?,?)",
                (now, device["id"], device["user_id"], body.kind, body.reason, body.client_version,
                 body.exe_path, int(body.portable), json.dumps(body.model_dump(), ensure_ascii=False)),
            )
            db.execute(
                "DELETE FROM diagnostics WHERE user_id=? AND id NOT IN "
                "(SELECT id FROM diagnostics WHERE user_id=? ORDER BY id DESC LIMIT ?)",
                (device["user_id"], device["user_id"], KEEP_PER_USER),
            )
            audit(db, "device:" + device["id"], device["user_id"], "diagnostics.received",
                  {"id": cursor.lastrowid, "kind": body.kind, "reason": body.reason[:200]})
        return {"ok": True, "id": cursor.lastrowid}

    def summary(row):
        return {k: row[k] for k in ("id", "received_at", "device_id", "user_id", "kind", "reason",
                                     "client_version", "exe_path")} | {"portable": bool(row["portable"])}

    @app.get("/api/diagnostics")
    def listing(request: Request, user_id: str = "", limit: int = Query(100, ge=1, le=500)):
        who = principal(request)
        with store.db() as db:
            if user_id:
                visible(db, who, user_id)
            rows = db.execute(
                "SELECT * FROM diagnostics WHERE (?='' OR user_id=?) AND (?='owner' OR user_id IN "
                "(SELECT user_id FROM scopes WHERE head_id=?)) ORDER BY id DESC LIMIT ?",
                (user_id, user_id, who["role"], who["id"], limit),
            ).fetchall()
            return [summary(r) for r in rows]

    @app.get("/api/diagnostics/{report_id}")
    def report(report_id: int, request: Request):
        who = principal(request)
        with store.db() as db:
            row = db.execute("SELECT * FROM diagnostics WHERE id=?", (report_id,)).fetchone()
            if not row:
                raise HTTPException(404, "Report not found")
            visible(db, who, row["user_id"])
            return summary(row) | {"report": json.loads(row["body"])}
