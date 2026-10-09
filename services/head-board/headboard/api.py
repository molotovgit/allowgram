import hmac
import json
import os
import time
from collections import defaultdict, deque
from pathlib import Path
from threading import Lock
from urllib.parse import urlsplit

from fastapi import FastAPI, HTTPException, Request, Response
from fastapi.responses import JSONResponse
from pydantic import BaseModel, ConfigDict, Field

from .store import Store, digest

COOKIE = "agh_session"
MAX_RATE_SOURCES = 4096
_rate_now = time.monotonic


class Code(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)
    code: str = Field(min_length=20, max_length=200)


def create_app(
    data_dir=None, public_url="http://127.0.0.1:28444", owner_id="8683512953",
    hub_service_token=None, **kwargs
):
    if hub_service_token is not None and (
        not isinstance(hub_service_token, str)
        or not hub_service_token.isascii()
        or not 32 <= len(hub_service_token) <= 200
        or any(not (c.isalnum() or c in "-_") for c in hub_service_token)
    ):
        raise ValueError("Invalid Hub service credential")
    public_url = public_url.rstrip("/")
    url = urlsplit(public_url)
    if (
        not url.hostname
        or url.username
        or url.password
        or url.query
        or url.fragment
        or url.path
        or (
            url.scheme != "https"
            and not (
                url.scheme == "http" and url.hostname in {"127.0.0.1", "localhost"}
            )
        )
    ):
        raise ValueError(
            "Public URL must be an HTTPS origin or explicit local loopback HTTP"
        )
    app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None)
    store = Store(
        data_dir
        or os.environ.get(
            "HEAD_DATA_DIR", str(Path.home() / ".local/share/allowgram-head")
        ),
        owner_id,
    )
    app.state.store = store
    app.state.public_url = public_url
    buckets, lock = defaultdict(deque), Lock()

    @app.middleware("http")
    async def security(request: Request, call_next):
        railway_health = (kwargs.get("production_public_key") and request.url.hostname == "healthcheck.railway.app"
                          and request.method == "GET" and request.url.path == "/api/health")
        if not railway_health and request.url.hostname not in {url.hostname, "127.0.0.1", "localhost"}:
            return JSONResponse({"detail": "Invalid host"}, status_code=400)
        now = _rate_now()
        key = (
            request.client.host if request.client else "unknown",
            "auth" if request.url.path.startswith("/api/auth/") else "api",
        )
        with lock:
            if key not in buckets:
                for old in [k for k, q in buckets.items() if not q or q[-1] < now - 60]:
                    del buckets[old]
                if len(buckets) >= MAX_RATE_SOURCES:
                    return JSONResponse({"detail": "Busy; retry later"}, status_code=429,
                                        headers={"Retry-After": "60"})
            bucket = buckets[key]
            while bucket and bucket[0] < now - 60:
                bucket.popleft()
            if len(bucket) >= (20 if key[1] == "auth" else 600):
                return JSONResponse(
                    {"detail": "Too many requests"},
                    status_code=429,
                    headers={"Retry-After": "60"},
                )
            bucket.append(now)
        service_request = request.url.path.startswith("/api/service/v1/")
        if service_request:
            if hub_service_token is None:
                return JSONResponse({"detail": "Hub service disabled"}, status_code=503)
            supplied = request.headers.get("authorization", "").encode("utf-8")
            if not hmac.compare_digest(supplied, ("Bearer " + hub_service_token).encode("ascii")):
                return JSONResponse({"detail": "Service authorization required"}, status_code=401)
        if request.method in {"POST", "PUT", "PATCH", "DELETE"}:
            if (
                not service_request
                and not request.url.path.startswith("/api/client/")
                and request.headers.get("origin") != public_url
            ):
                return JSONResponse({"detail": "Invalid origin"}, status_code=403)
            if (
                request.headers.get("content-type", "").split(";")[0].strip().lower()
                != "application/json"
            ):
                return JSONResponse({"detail": "JSON required"}, status_code=415)
            import asyncio

            async def read_limited():
                content = bytearray()
                async for chunk in request.stream():
                    if len(content) + len(chunk) > 1048576:
                        raise OverflowError()
                    content.extend(chunk)
                return bytes(content)

            try:
                raw = await asyncio.wait_for(read_limited(), timeout=15)
            except OverflowError:
                return JSONResponse({"detail": "Request too large"}, status_code=413)
            except TimeoutError:
                return JSONResponse({"detail": "Request timed out"}, status_code=408)
            # Starlette's cached middleware request replays this bounded body to routing.
            request._body = raw

            def unique(pairs):
                result = {}
                for k, v in pairs:
                    if k in result:
                        raise ValueError("Duplicate field")
                    result[k] = v
                return result

            try:
                json.loads(
                    raw,
                    object_pairs_hook=unique,
                    parse_constant=lambda _: (_ for _ in ()).throw(ValueError()),
                )
            except (ValueError, UnicodeError):
                return JSONResponse({"detail": "Invalid JSON"}, status_code=400)
        response = await call_next(request)
        response.headers.update(
            {
                "Cache-Control": "no-store",
                "X-Content-Type-Options": "nosniff",
                "Referrer-Policy": "no-referrer",
                "X-Frame-Options": "DENY",
                "Permissions-Policy": "camera=(), microphone=(), geolocation=()",
                "Content-Security-Policy": "default-src 'self'; script-src 'self' https://telegram.org; style-src 'self'; img-src 'self' data:; frame-src https://oauth.telegram.org; connect-src 'self'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'",
            }
        )
        if url.scheme == "https":
            response.headers["Strict-Transport-Security"] = "max-age=86400"
        return response

    def principal(request, write=False, owner=False):
        session = store.session(request.cookies.get(COOKIE, ""))
        if not session:
            raise HTTPException(401, "Sign in required")
        if write and not hmac.compare_digest(
            request.headers.get("x-csrf-token", ""), session["csrf"]
        ):
            raise HTTPException(403, "Invalid CSRF token")
        if owner and session["role"] != "owner":
            raise HTTPException(403, "Owner access required")
        return session

    app.state.principal = principal

    @app.get("/api/health")
    def health():
        result = {"ok": True, "service": "Allowgram Head", "version": "0.1.0"}
        if kwargs.get("production_public_key"):
            result.update(policy_public_key=kwargs["production_public_key"],
                          build_commit=os.environ.get("HEAD_BUILD_COMMIT", "unknown"))
        return result

    @app.get("/api/session")
    def session(request: Request):
        return principal(request)

    @app.get("/api/auth/config")
    def auth_config():
        return {"telegram_enabled": False, "bootstrap_enabled": True}

    @app.post("/api/auth/bootstrap")
    def bootstrap(body: Code, response: Response):
        result = store.claim_code(body.code)
        if not result:
            raise HTTPException(401, "Invalid or expired access code")
        token, session = result
        response.set_cookie(
            COOKIE,
            token,
            secure=url.scheme == "https",
            httponly=True,
            samesite="strict",
            max_age=43200,
            path="/",
        )
        return session

    @app.post("/api/auth/logout")
    def logout(request: Request, response: Response):
        principal(request, write=True)
        with store.db() as db:
            db.execute(
                "DELETE FROM sessions WHERE hash=?",
                (digest(request.cookies.get(COOKIE, "")),),
            )
        response.delete_cookie(
            COOKIE,
            path="/",
            secure=url.scheme == "https",
            httponly=True,
            samesite="strict",
        )
        return {"ok": True}

    from . import admin

    admin.register(app)
    from . import wire

    wire.register(app)
    from . import registration

    registration.register(app)
    from . import hub

    hub.register(app)
    from fastapi.staticfiles import StaticFiles

    dist = Path(__file__).resolve().parent.parent / "web" / "dist"
    if dist.is_dir():
        app.mount("/", StaticFiles(directory=dist, html=True), name="dashboard")
    return app


def configured_app():
    from .production import hub_service_key

    service_key = hub_service_key()
    public_key = None
    if os.environ.get("HEAD_PRODUCTION") == "1":
        from .production import prepare
        public_key = prepare()
    return create_app(
        production_public_key=public_key,
        hub_service_token=service_key,
        public_url=os.environ.get("HEAD_PUBLIC_URL", "http://127.0.0.1:28444"),
        owner_id=os.environ.get("HEAD_OWNER_ID", "8683512953"),
    )
