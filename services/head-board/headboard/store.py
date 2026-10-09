"""Private durable state. No plaintext bearer credentials are stored."""

import hashlib
import os
import secrets
import sqlite3
import time
from contextlib import contextmanager
from pathlib import Path


def digest(value: str) -> str:
    return hashlib.sha256(value.encode()).hexdigest()


class Store:
    def __init__(self, directory, owner_id="8683512953"):
        self.directory = Path(directory).expanduser().resolve()
        self.directory.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.directory.chmod(0o700)
        self.path = self.directory / "board.sqlite3"
        with self.db() as db:
            db.executescript("""
            CREATE TABLE IF NOT EXISTS heads(id TEXT PRIMARY KEY, label TEXT NOT NULL, role TEXT NOT NULL CHECK(role IN ('owner','head')), active INTEGER NOT NULL DEFAULT 1);
            CREATE TABLE IF NOT EXISTS login_codes(hash TEXT PRIMARY KEY, head_id TEXT NOT NULL REFERENCES heads(id), expires INTEGER NOT NULL, used INTEGER NOT NULL DEFAULT 0);
            CREATE TABLE IF NOT EXISTS sessions(hash TEXT PRIMARY KEY, head_id TEXT NOT NULL REFERENCES heads(id), csrf TEXT NOT NULL, expires INTEGER NOT NULL);
            CREATE TABLE IF NOT EXISTS users(id TEXT PRIMARY KEY, label TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 0, peers TEXT NOT NULL DEFAULT '[]', updated_at INTEGER NOT NULL DEFAULT 0);
            CREATE TABLE IF NOT EXISTS scopes(head_id TEXT NOT NULL REFERENCES heads(id), user_id TEXT NOT NULL REFERENCES users(id), PRIMARY KEY(head_id,user_id));
            CREATE TABLE IF NOT EXISTS policies(user_id TEXT NOT NULL REFERENCES users(id), revision INTEGER NOT NULL, peers TEXT NOT NULL, updated_at INTEGER NOT NULL, PRIMARY KEY(user_id,revision));
            CREATE TABLE IF NOT EXISTS audit(id INTEGER PRIMARY KEY AUTOINCREMENT, at INTEGER NOT NULL, actor_id TEXT NOT NULL, user_id TEXT, action TEXT NOT NULL, detail TEXT NOT NULL);

            """)
            db.execute(
                "INSERT OR IGNORE INTO heads(id,label,role) VALUES(?, 'Owner', 'owner')",
                (owner_id,),
            )
        self.path.chmod(0o600)

    @contextmanager
    def db(self):
        db = sqlite3.connect(self.path, timeout=10)
        db.row_factory = sqlite3.Row
        db.execute("PRAGMA foreign_keys=ON")
        db.execute("PRAGMA journal_mode=WAL")
        try:
            db.execute("BEGIN IMMEDIATE")
            yield db
            db.commit()
        except BaseException:
            db.rollback()
            raise
        finally:
            db.close()

    @contextmanager
    def read(self):
        db = sqlite3.connect(self.path.as_uri() + "?mode=ro", uri=True, timeout=1)
        db.row_factory = sqlite3.Row
        try:
            db.execute("BEGIN")
            yield db
        finally:
            db.close()

    def issue_owner_code(self, output):
        code = secrets.token_urlsafe(32)
        output = Path(output)
        fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            with self.db() as db:
                owner = db.execute(
                    "SELECT id FROM heads WHERE role='owner' AND active=1 ORDER BY id LIMIT 1"
                ).fetchone()
                if not owner:
                    raise ValueError("No active owner")
                db.execute(
                    "INSERT INTO login_codes VALUES(?,?,?,0)",
                    (digest(code), owner["id"], int(time.time()) + 900),
                )
            with os.fdopen(fd, "w") as stream:
                fd = -1
                stream.write(code + "\n")
        finally:
            if fd != -1:
                os.close(fd)

    def claim_code(self, code):
        with self.db() as db:
            row = db.execute(
                "SELECT h.* FROM login_codes c JOIN heads h ON h.id=c.head_id WHERE c.hash=? AND c.used=0 AND c.expires>? AND h.active=1",
                (digest(code), int(time.time())),
            ).fetchone()
            if not row:
                return None
            db.execute("UPDATE login_codes SET used=1 WHERE hash=?", (digest(code),))
            return self.new_session(db, row)

    def new_session(self, db, head):
        token, csrf = secrets.token_urlsafe(32), secrets.token_urlsafe(32)
        db.execute(
            "INSERT INTO sessions VALUES(?,?,?,?)",
            (digest(token), head["id"], csrf, int(time.time()) + 43200),
        )
        return token, {
            "id": head["id"],
            "label": head["label"],
            "role": head["role"],
            "csrf": csrf,
        }

    def session(self, token):
        with self.db() as db:
            row = db.execute(
                "SELECT h.id,h.label,h.role,s.csrf FROM sessions s JOIN heads h ON h.id=s.head_id WHERE s.hash=? AND s.expires>? AND h.active=1",
                (digest(token), int(time.time())),
            ).fetchone()
            return dict(row) if row else None
