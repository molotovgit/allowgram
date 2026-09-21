"""Run only receipt-verified, MTProto-disabled production-widget fixtures on Windows."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import subprocess
import tempfile

from native_process import process_identity, require_session_zero, stop_owned
from test_sheet_https import Fixture, MATCHED, ABSENT, PHONE, certificate


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    require_session_zero()
    executable = args.executable.resolve(strict=True)
    evidence = json.loads((executable.parent / "build-evidence.json").read_text())
    if (not evidence.get("sheetFixture") or not evidence.get("mtprotoNetworkDisabled")
            or hashlib.sha256(executable.read_bytes()).hexdigest() != evidence.get("fixtureExecutableSha256")):
        raise RuntimeError("Only the verified isolated sheet fixture may run.")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    results = []
    for scene in ("matched", "absent", "missing", "corrupt", "http-error", "bad-pin", "save-failure", "phone-change", "destroy-widget"):
        with tempfile.TemporaryDirectory(prefix="sheet-widget-", dir=output) as temporary:
            profile = Path(temporary)
            (profile / "roaming").mkdir()
            (profile / "local").mkdir()
            cert, key, pin = certificate(profile)
            server = Fixture(cert, key)
            server.respond(ABSENT if scene == "absent" else MATCHED, 503 if scene == "http-error" else 200)
            if scene in ("phone-change", "destroy-widget"):
                server.delay = 0.2
            access = profile / "allowgram-sheet-access.json"
            access.write_text(json.dumps({"version": 1,
                "endpoint": "https://127.0.0.1:" + str(server.server_address[1]) + "/v1/allowlist/resolve",
                "phone": PHONE, "token": secrets.token_urlsafe(32),
                "certificate_sha256": "0" * 64 if scene == "bad-pin" else pin}), encoding="utf-8")
            access.chmod(0o600)
            if scene == "missing":
                access.unlink()
            elif scene == "corrupt":
                access.write_text("{", encoding="utf-8")
            report = output / (scene + ".json")
            env = dict(os.environ, APPDATA=str(profile / "roaming"), LOCALAPPDATA=str(profile / "local"),
                       ALLOWGRAM_SHEET_SCENE=scene, ALLOWGRAM_SHEET_REPORT=str(report), QT_SCALE_FACTOR="1")
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            process = None
            identity = None
            try:
                process = subprocess.Popen([str(executable), "-many", "-noupdate", "-workdir", str(profile)],
                                           cwd=profile, env=env, startupinfo=startup,
                                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                identity = process_identity(process, executable)
                code = process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                stop_owned(process, executable, identity)
                raise RuntimeError("Synthetic sheet widget fixture timed out.") from None
            finally:
                server.close()
                if process is not None and process.poll() is None and identity is not None:
                    stop_owned(process, executable, identity)
            if not report.is_file():
                raise RuntimeError("Missing synthetic widget receipt: " + scene)
            data = json.loads(report.read_text())
            data.update(scene=scene, exit=code)
            if scene in ("missing", "corrupt", "bad-pin") and server.application_bytes != 0:
                data["failures"] += 1
            results.append(data)
            print(scene + ": " + str(len(data["checks"])) + " checks, " + str(data["failures"]) + " failures")
    (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    return int(any(data["exit"] or data["failures"] or not data.get("finished") for data in results))


if __name__ == "__main__":
    raise SystemExit(main())
