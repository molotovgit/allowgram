"""Exercise the compiled production Qt resolver against transient synthetic TLS peers."""
import argparse
import json
from pathlib import Path
import secrets
import socketserver
import ssl
import subprocess
import sys
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "services/allowgram-sheet-broker/tests"))
from tls_support import certificate

PHONE = "+998000000001"
MATCHED = {"version": 1, "status": "matched", "phone": PHONE,
           "user_ids": ["201", "202", "203"], "group_ids": ["-1000000000101", "-1000000000102"]}
ABSENT = {"version": 1, "status": "not_found", "phone": PHONE}


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            self.request.settimeout(2)
            with self.server.context.wrap_socket(self.request, server_side=True) as connection:
                data = bytearray()
                while b"\r\n\r\n" not in data:
                    part = connection.recv(4096)
                    if not part:
                        return
                    data.extend(part)
                    self.server.application_bytes += len(part)
                    if len(data) > 8192:
                        return
                head, body = data.split(b"\r\n\r\n", 1)
                headers = dict(line.split(b": ", 1) for line in bytes(head).split(b"\r\n")[1:])
                while len(body) < int(headers[b"Content-Length"]):
                    part = connection.recv(4096)
                    if not part:
                        return
                    body.extend(part)
                self.server.requests.append((head, bytes(body)))
                if self.server.delay:
                    self.server.stop.wait(self.server.delay)
                connection.sendall(self.server.response)
        except (OSError, ValueError):
            pass


class Fixture(socketserver.ThreadingTCPServer):
    daemon_threads = False
    allow_reuse_address = False

    def __init__(self, cert, key):
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.minimum_version = ssl.TLSVersion.TLSv1_2
        self.context.load_cert_chain(cert, key)
        self.requests = []
        self.errors = []
        self.application_bytes = 0
        self.delay = 0
        self.stop = threading.Event()
        self.response = b""
        super().__init__(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.serve_forever, kwargs={"poll_interval": 0.01})
        self.thread.start()

    def handle_error(self, request, client_address):
        self.errors.append(type(sys.exception()).__name__ if hasattr(sys, "exception") else sys.exc_info()[0].__name__)

    def close(self):
        self.stop.set()
        self.shutdown()
        self.server_close()
        self.thread.join()
        if self.errors:
            raise AssertionError("Synthetic TLS fixture handler failed: " + ", ".join(self.errors))

    def respond(self, body, status=200, extra=b"", declared=None):
        body = json.dumps(body).encode() if isinstance(body, dict) else body
        self.response = (b"HTTP/1.1 " + str(status).encode() + b" Result\r\nContent-Type: application/json\r\n"
                         b"Content-Length: " + str(len(body) if declared is None else declared).encode()
                         + b"\r\n" + extra + b"\r\n" + body)


class NativeHttpsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.profile_path = self.root / "allowgram-sheet-access.json"

    def fixture(self, **cert_options):
        cert, key, pin = certificate(self.root, **cert_options)
        fixture = Fixture(cert, key)
        self.addCleanup(fixture.close)
        self.profile = {"version": 1, "endpoint": "https://127.0.0.1:" + str(fixture.server_address[1]) + "/v1/allowlist/resolve",
                        "phone": PHONE, "token": secrets.token_urlsafe(32), "certificate_sha256": pin}
        self.write_profile()
        fixture.respond(MATCHED)
        return fixture

    def write_profile(self):
        self.profile_path.write_text(json.dumps(self.profile), encoding="utf-8")
        self.profile_path.chmod(0o600)

    def run_native(self, mode="resolve", phone=PHONE):
        run = subprocess.run([str(EXECUTABLE), mode, str(self.profile_path), phone],
                             capture_output=True, text=True, timeout=14)
        self.assertEqual(run.returncode, 0, "native test process must complete successfully")
        self.assertNotIn(self.profile["token"] if hasattr(self, "profile") else "PRIVATE_SENTINEL", run.stderr)
        return json.loads(run.stdout)

    def test_matched_self_signed_and_exact_request(self):
        fixture = self.fixture()
        result = self.run_native()
        self.assertEqual((result["status"], result["users"], result["groups"]), ("matched", MATCHED["user_ids"], MATCHED["group_ids"]))
        self.assertEqual(len(fixture.requests), 1)
        head, body = fixture.requests[0]
        self.assertEqual(json.loads(body), {"version": 1, "phone": PHONE})
        self.assertIn(b"Authorization: Bearer " + self.profile["token"].encode(), head)
        self.assertNotIn(PHONE.encode(), head)

    def test_absent(self):
        fixture = self.fixture()
        fixture.respond(ABSENT)
        self.assertEqual(self.run_native()["status"], "not_found")

    def test_certificate_mismatch_sends_zero_application_bytes(self):
        fixture = self.fixture()
        self.profile["certificate_sha256"] = "0" * 64
        self.write_profile()
        self.assertEqual(self.run_native()["status"], "failed")
        self.assertEqual(fixture.application_bytes, 0)

    def test_bad_hostname_sends_zero_application_bytes(self):
        fixture = self.fixture(host="127.0.0.2")
        self.assertEqual(self.run_native()["status"], "failed")
        self.assertEqual(fixture.application_bytes, 0)

    def test_expired_certificate_sends_zero_application_bytes(self):
        fixture = self.fixture(expired=True)
        self.assertEqual(self.run_native()["status"], "failed")
        self.assertEqual(fixture.application_bytes, 0)

    def test_future_certificate_sends_zero_application_bytes(self):
        fixture = self.fixture(future=True)
        self.assertEqual(self.run_native()["status"], "failed")
        self.assertEqual(fixture.application_bytes, 0)

    def test_http_failures_redirect_and_truncation(self):
        fixture = self.fixture()
        for status in (301, 302, 307, 401, 403, 404, 429, 503):
            fixture.respond(ABSENT, status, b"Location: https://127.0.0.1:1/v1/allowlist/resolve\r\n")
            self.assertEqual(self.run_native()["status"], "failed")
        fixture.respond(b'{', declared=100)
        self.assertEqual(self.run_native()["status"], "failed")
        fixture.respond(b' ' * 65537)
        self.assertEqual(self.run_native()["status"], "failed")
        fixture.respond(ABSENT, extra=b"Transfer-Encoding: chunked\r\n")
        self.assertEqual(self.run_native()["status"], "failed")

    def test_protocol_errors_never_report_absent(self):
        fixture = self.fixture()
        for body in (b'{"version":1,"version":1,"status":"not_found","phone":"+998000000001"}',
                     dict(ABSENT, phone="+998000000002"), dict(ABSENT, version=True),
                     dict(ABSENT, status="unknown"), dict(ABSENT, user_ids=[]),
                     dict(MATCHED, user_ids=[]), dict(MATCHED, user_ids=[201]),
                     dict(MATCHED, group_ids=["channel:101"]), {}):
            fixture.respond(body)
            self.assertEqual(self.run_native()["status"], "failed")

    def test_missing_corrupt_oversized_empty_session_before_request(self):
        fixture = self.fixture()
        self.profile_path.unlink()
        self.assertEqual(self.run_native()["status"], "failed")
        for data in (b'{', b' ' * 16385):
            self.profile_path.write_bytes(data)
            self.assertEqual(self.run_native()["status"], "failed")
        self.write_profile()
        self.assertEqual(self.run_native(phone="")["status"], "failed")
        self.assertEqual(fixture.application_bytes, 0)

    def test_install_token_sends_authenticated_session_phone(self):
        fixture = self.fixture()
        other = "+998000000002"
        fixture.respond({"version": 1, "status": "not_found", "phone": other})
        result = self.run_native(phone=other)
        self.assertEqual(result["status"], "not_found")
        self.assertEqual(len(fixture.requests), 1)
        head, body = fixture.requests[0]
        self.assertEqual(json.loads(body), {"version": 1, "phone": other})

    def test_total_timeout(self):
        fixture = self.fixture()
        fixture.delay = 11
        result = self.run_native()
        self.assertEqual(result["status"], "failed")
        self.assertGreaterEqual(result["elapsed_ms"], 9000)
        self.assertLess(result["elapsed_ms"], 11500)

    def test_cancel_and_destroy_suppress_callbacks(self):
        fixture = self.fixture()
        fixture.delay = 0.2
        for mode in ("cancel", "destroy"):
            self.assertEqual(self.run_native(mode), {"status": "cancelled", "callbacks": 0})


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=Path)
    args, rest = parser.parse_known_args()
    EXECUTABLE = args.executable.resolve(strict=True)
    unittest.main(argv=[sys.argv[0]] + rest)
