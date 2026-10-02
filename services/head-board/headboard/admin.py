import json
import sqlite3
import time

from fastapi import HTTPException, Request

from .models import Assign, HeadEdit, Identity, PolicyEdit, valid_id


def audit(db, actor, user_id, action, detail):
    db.execute(
        "INSERT INTO audit(at,actor_id,user_id,action,detail) VALUES(?,?,?,?,?)",
        (
            int(time.time()),
            actor,
            user_id,
            action,
            json.dumps(detail, ensure_ascii=False),
        ),
    )


def visible(db, principal, user_id):
    try:
        valid_id(user_id)
    except ValueError:
        raise HTTPException(404, "User not found")
    if (
        principal["role"] != "owner"
        and not db.execute(
            "SELECT 1 FROM scopes WHERE head_id=? AND user_id=?",
            (principal["id"], user_id),
        ).fetchone()
    ):
        raise HTTPException(404, "User not found")
    row = db.execute("SELECT * FROM users WHERE id=?", (user_id,)).fetchone()
    if not row:
        raise HTTPException(404, "User not found")
    return row


def set_policy(db, user_id, revision, peers, actor):
    encoded = json.dumps(peers, ensure_ascii=False, separators=(",", ":"))
    stamp = int(time.time())
    db.execute(
        "UPDATE users SET revision=?,peers=?,updated_at=? WHERE id=?",
        (revision, encoded, stamp, user_id),
    )
    db.execute(
        "INSERT INTO policies VALUES(?,?,?,?)", (user_id, revision, encoded, stamp)
    )
    audit(
        db,
        actor,
        user_id,
        "policy.saved",
        {"revision": revision, "peer_count": len(peers)},
    )


def register(app):
    store, principal = app.state.store, app.state.principal

    @app.get("/api/heads")
    def heads(request: Request):
        principal(request, owner=True)
        with store.db() as db:
            return [
                dict(r)
                for r in db.execute(
                    "SELECT id,label,role,active FROM heads ORDER BY role DESC,label,id"
                )
            ]

    @app.post("/api/heads", status_code=201)
    def create_head(body: Identity, request: Request):
        who = principal(request, write=True, owner=True)
        with store.db() as db:
            try:
                db.execute(
                    "INSERT INTO heads(id,label,role) VALUES(?,?,'head')",
                    (body.id, body.label),
                )
            except sqlite3.IntegrityError:
                raise HTTPException(409, "Head already exists")
            audit(
                db,
                who["id"],
                None,
                "head.created",
                {"id": body.id, "label": body.label},
            )
        return {"id": body.id, "label": body.label, "role": "head", "active": 1}

    @app.patch("/api/heads/{head_id}")
    def update_head(head_id: str, body: HeadEdit, request: Request):
        who = principal(request, write=True, owner=True)
        with store.db() as db:
            row = db.execute("SELECT * FROM heads WHERE id=?", (head_id,)).fetchone()
            if not row:
                raise HTTPException(404, "Head not found")
            if row["role"] == "owner" and not body.active:
                raise HTTPException(409, "The owner cannot be disabled")
            db.execute(
                "UPDATE heads SET label=?,active=? WHERE id=?",
                (body.label, int(body.active), head_id),
            )
            if not body.active:
                db.execute("DELETE FROM sessions WHERE head_id=?", (head_id,))
                db.execute("DELETE FROM login_codes WHERE head_id=?", (head_id,))
            audit(
                db,
                who["id"],
                None,
                "head.updated",
                {"id": head_id, "active": body.active, "label": body.label},
            )
        return {"ok": True}

    @app.post("/api/heads/{head_id}/access-code", status_code=201)
    def access_code(head_id: str, request: Request):
        import secrets

        from .store import digest

        who = principal(request, write=True, owner=True)
        with store.db() as db:
            row = db.execute(
                "SELECT * FROM heads WHERE id=? AND active=1", (head_id,)
            ).fetchone()
            if not row:
                raise HTTPException(404, "Active head not found")
            code, expires = secrets.token_urlsafe(32), int(time.time()) + 900
            db.execute(
                "INSERT INTO login_codes VALUES(?,?,?,0)",
                (digest(code), head_id, expires),
            )
            audit(
                db,
                who["id"],
                None,
                "head.access-issued",
                {"head_id": head_id, "expires": expires},
            )
        return {"code": code, "expires": expires}

    @app.get("/api/users")
    def users(request: Request, search: str = ""):
        who = principal(request)
        if len(search) > 128:
            raise HTTPException(422, "Search too long")
        with store.db() as db:
            rows = db.execute(
                "SELECT * FROM users WHERE (?='owner' OR id IN (SELECT user_id FROM scopes WHERE head_id=?)) AND (label LIKE ? OR id LIKE ?) ORDER BY label,id LIMIT 1000",
                (who["role"], who["id"], "%" + search + "%", "%" + search + "%"),
            ).fetchall()
            from .wire import devices

            return [
                {
                    **dict(r),
                    "peer_count": len(json.loads(r["peers"])),
                    "peers": None,
                    "devices": devices(db, r["id"]),
                }
                for r in rows
            ]

    @app.post("/api/users", status_code=201)
    def create_user(body: Identity, request: Request):
        who = principal(request, write=True, owner=True)
        with store.db() as db:
            try:
                db.execute(
                    "INSERT INTO users(id,label) VALUES(?,?)", (body.id, body.label)
                )
            except sqlite3.IntegrityError:
                raise HTTPException(409, "User already exists")
            audit(db, who["id"], body.id, "user.created", {"label": body.label})
        return {"id": body.id, "label": body.label, "revision": 0}

    @app.get("/api/users/{user_id}")
    def user(user_id: str, request: Request):
        who = principal(request)
        with store.db() as db:
            row = visible(db, who, user_id)
            result = dict(row)
            result["peers"] = json.loads(row["peers"])
            from .wire import devices

            result["devices"] = devices(db, user_id)
            result["head_ids"] = [
                r[0]
                for r in db.execute(
                    "SELECT head_id FROM scopes WHERE user_id=? ORDER BY head_id",
                    (user_id,),
                )
            ]
            return result

    @app.put("/api/users/{user_id}/heads")
    def scopes(user_id: str, body: Assign, request: Request):
        who = principal(request, write=True, owner=True)
        with store.db() as db:
            visible(db, who, user_id)
            ids = sorted(set(body.head_ids))
            for head in ids:
                if not db.execute(
                    "SELECT 1 FROM heads WHERE id=? AND role='head' AND active=1",
                    (head,),
                ).fetchone():
                    raise HTTPException(422, "Assign active head accounts only")
            db.execute("DELETE FROM scopes WHERE user_id=?", (user_id,))
            db.executemany(
                "INSERT INTO scopes VALUES(?,?)", [(h, user_id) for h in ids]
            )
            audit(db, who["id"], user_id, "heads.assigned", {"head_ids": ids})
        return {"ok": True}

    @app.put("/api/users/{user_id}/policy")
    def policy(user_id: str, body: PolicyEdit, request: Request):
        who = principal(request, write=True)
        with store.db() as db:
            row = visible(db, who, user_id)
            if row["revision"] != body.expected_revision:
                raise HTTPException(
                    409,
                    "The list changed. Reload before saving; your edits have not been applied.",
                )
            revision = row["revision"] + 1
            set_policy(
                db,
                user_id,
                revision,
                [p.model_dump(exclude_none=True) for p in body.peers],
                who["id"],
            )
        return {"revision": revision, "status": "pending"}

    @app.get("/api/audit")
    def activity(request: Request):
        who = principal(request)
        with store.db() as db:
            rows = db.execute(
                "SELECT * FROM audit WHERE (?='owner' OR user_id IN (SELECT user_id FROM scopes WHERE head_id=?)) ORDER BY id DESC LIMIT 200",
                (who["role"], who["id"]),
            ).fetchall()
            return [{**dict(r), "detail": json.loads(r["detail"])} for r in rows]
