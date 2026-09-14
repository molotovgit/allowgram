"""Generate, package and run the connected Allowgram updater E2E fixture."""

from __future__ import annotations

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit
import argparse
import base64
import ctypes
from ctypes import wintypes
import datetime as dt
import hashlib
import json
import os
import shutil
import subprocess
import sys
import threading
import time

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ed25519


PRODUCT = 'Allowgram'
CHANNEL = 'stable'
PLATFORM = 'win64'
ASSET_OS = 'win'
ASSET_ARCH = 'x64'
KEY_ID = 'connected-e2e-release'
FEED_SIGNING_DOMAIN = b'Allowgram stable release feed v1\n'


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b'=').decode('ascii')


def raw_public(key: ed25519.Ed25519PublicKey) -> bytes:
    return key.public_bytes(
        serialization.Encoding.Raw,
        serialization.PublicFormat.Raw)


def write_private(path: Path, key: ed25519.Ed25519PrivateKey) -> None:
    path.write_bytes(key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption()))


def write_public(path: Path, key: ed25519.Ed25519PublicKey) -> None:
    path.write_bytes(key.public_bytes(
        serialization.Encoding.PEM,
        serialization.PublicFormat.SubjectPublicKeyInfo))


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def require_new_dir(path: Path) -> Path:
    path = path.resolve()
    if path.exists():
        raise RuntimeError(f'output already exists: {path}')
    path.mkdir(parents=True)
    return path


def display_from_base_sequence(base: int, sequence: int) -> str:
    major = base // 1000000
    minor = (base // 1000) % 1000
    patch = base % 1000
    return f'{major}.{minor}.{patch}.{sequence}'


class VSFixedFileInfo(ctypes.Structure):
    _fields_ = [
        ('dwSignature', wintypes.DWORD),
        ('dwStrucVersion', wintypes.DWORD),
        ('dwFileVersionMS', wintypes.DWORD),
        ('dwFileVersionLS', wintypes.DWORD),
        ('dwProductVersionMS', wintypes.DWORD),
        ('dwProductVersionLS', wintypes.DWORD),
        ('dwFileFlagsMask', wintypes.DWORD),
        ('dwFileFlags', wintypes.DWORD),
        ('dwFileOS', wintypes.DWORD),
        ('dwFileType', wintypes.DWORD),
        ('dwFileSubtype', wintypes.DWORD),
        ('dwFileDateMS', wintypes.DWORD),
        ('dwFileDateLS', wintypes.DWORD),
    ]


def version_pair(ms: int, ls: int) -> str:
    return f'{ms >> 16}.{ms & 0xffff}.{ls >> 16}.{ls & 0xffff}'


def pe_version(path: Path) -> dict[str, str]:
    version = ctypes.windll.version
    size = version.GetFileVersionInfoSizeW(str(path), None)
    if not size:
        return {'file': '', 'product': ''}
    buffer = ctypes.create_string_buffer(size)
    if not version.GetFileVersionInfoW(str(path), 0, size, buffer):
        raise ctypes.WinError()
    pointer = ctypes.c_void_p()
    length = wintypes.UINT()
    if not version.VerQueryValueW(buffer, '\\', ctypes.byref(pointer), ctypes.byref(length)):
        raise ctypes.WinError()
    fixed = ctypes.cast(pointer, ctypes.POINTER(VSFixedFileInfo)).contents
    return {
        'file': version_pair(fixed.dwFileVersionMS, fixed.dwFileVersionLS),
        'product': version_pair(fixed.dwProductVersionMS, fixed.dwProductVersionLS),
    }


FILETIME_EPOCH = 11644473600


def filetime_to_iso(value: wintypes.FILETIME) -> str:
    ticks = (value.dwHighDateTime << 32) + value.dwLowDateTime
    seconds = ticks / 10000000 - FILETIME_EPOCH
    return dt.datetime.fromtimestamp(seconds, dt.timezone.utc).isoformat()


PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
PROCESS_TERMINATE = 0x0001
TH32CS_SNAPPROCESS = 0x00000002
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
STILL_ACTIVE = 259
MAX_PATH = 260


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ('dwSize', wintypes.DWORD),
        ('cntUsage', wintypes.DWORD),
        ('th32ProcessID', wintypes.DWORD),
        ('th32DefaultHeapID', ctypes.POINTER(ctypes.c_ulong)),
        ('th32ModuleID', wintypes.DWORD),
        ('cntThreads', wintypes.DWORD),
        ('th32ParentProcessID', wintypes.DWORD),
        ('pcPriClassBase', wintypes.LONG),
        ('dwFlags', wintypes.DWORD),
        ('szExeFile', wintypes.WCHAR * MAX_PATH),
    ]


kernel32 = ctypes.windll.kernel32
kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
kernel32.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
kernel32.Process32FirstW.restype = wintypes.BOOL
kernel32.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
kernel32.Process32NextW.restype = wintypes.BOOL
kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.QueryFullProcessImageNameW.argtypes = [
    wintypes.HANDLE,
    wintypes.DWORD,
    wintypes.LPWSTR,
    ctypes.POINTER(wintypes.DWORD),
]
kernel32.QueryFullProcessImageNameW.restype = wintypes.BOOL
kernel32.GetProcessTimes.argtypes = [
    wintypes.HANDLE,
    ctypes.POINTER(wintypes.FILETIME),
    ctypes.POINTER(wintypes.FILETIME),
    ctypes.POINTER(wintypes.FILETIME),
    ctypes.POINTER(wintypes.FILETIME),
]
kernel32.GetProcessTimes.restype = wintypes.BOOL
kernel32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
kernel32.GetExitCodeProcess.restype = wintypes.BOOL
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
kernel32.TerminateProcess.restype = wintypes.BOOL


def query_process_path(handle: wintypes.HANDLE) -> str:
    buffer = ctypes.create_unicode_buffer(32768)
    size = wintypes.DWORD(len(buffer))
    if not kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)):
        return ''
    return buffer.value


def query_process_creation(handle: wintypes.HANDLE) -> str:
    created = wintypes.FILETIME()
    exited = wintypes.FILETIME()
    kernel = wintypes.FILETIME()
    user = wintypes.FILETIME()
    if not kernel32.GetProcessTimes(
            handle,
            ctypes.byref(created),
            ctypes.byref(exited),
            ctypes.byref(kernel),
            ctypes.byref(user)):
        return ''
    return filetime_to_iso(created)


def snapshot_processes() -> list[tuple[int, str, str]]:
    snapshot = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snapshot == INVALID_HANDLE_VALUE:
        raise ctypes.WinError()
    try:
        entry = PROCESSENTRY32W()
        entry.dwSize = ctypes.sizeof(PROCESSENTRY32W)
        result: list[tuple[int, str, str]] = []
        ok = kernel32.Process32FirstW(snapshot, ctypes.byref(entry))
        while ok:
            handle = kernel32.OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION,
                False,
                entry.th32ProcessID)
            if handle:
                try:
                    path = query_process_path(handle)
                    if path:
                        result.append((
                            int(entry.th32ProcessID),
                            path,
                            query_process_creation(handle)))
                finally:
                    kernel32.CloseHandle(handle)
            ok = kernel32.Process32NextW(snapshot, ctypes.byref(entry))
        return result
    finally:
        kernel32.CloseHandle(snapshot)


class ProcessObserver:
    def __init__(self, install_dir: Path):
        self.install_dir = install_dir.resolve()
        self.client = str(self.install_dir / 'Allowgram.exe').lower()
        self.updater = str(self.install_dir / 'AllowgramUpdater.exe').lower()
        self.records: dict[int, dict[str, object]] = {}
        self.handles: dict[int, wintypes.HANDLE] = {}

    def sample(self) -> None:
        present = set()
        for pid, path, created in snapshot_processes():
            lowered = path.lower()
            if lowered not in (self.client, self.updater):
                continue
            present.add(pid)
            if pid in self.records:
                continue
            handle = kernel32.OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION,
                False,
                pid)
            if handle:
                self.handles[pid] = handle
            self.records[pid] = {
                'pid': pid,
                'path': path,
                'role': 'helper' if lowered == self.updater else 'client',
                'creationUtc': created,
                'firstSeenUtc': dt.datetime.now(dt.timezone.utc).isoformat(),
            }
        self.refresh_exits(present)

    def refresh_exits(self, present: set[int] | None = None) -> None:
        if present is None:
            present = {pid for pid, _, _ in snapshot_processes()}
        for pid, handle in list(self.handles.items()):
            if pid in present:
                continue
            code = wintypes.DWORD()
            if kernel32.GetExitCodeProcess(handle, ctypes.byref(code)):
                if code.value != STILL_ACTIVE:
                    self.records[pid]['exitCode'] = int(code.value)
                    self.records[pid]['exitSeenUtc'] = dt.datetime.now(
                        dt.timezone.utc).isoformat()

    def terminate_owned(self) -> list[int]:
        killed = []
        for pid, record in list(self.records.items()):
            if 'exitCode' in record:
                continue
            handle = kernel32.OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                False,
                pid)
            if not handle:
                continue
            try:
                path = query_process_path(handle).lower()
                if path in (self.client, self.updater):
                    if kernel32.TerminateProcess(handle, 1):
                        killed.append(pid)
            finally:
                kernel32.CloseHandle(handle)
        return killed

    def close(self) -> None:
        for handle in self.handles.values():
            kernel32.CloseHandle(handle)
        self.handles.clear()

    def as_list(self) -> list[dict[str, object]]:
        self.refresh_exits()
        return sorted(self.records.values(), key=lambda item: (str(item['creationUtc']), int(item['pid'])))


class FixtureServer:
    def __init__(self, feed: Path, update: Path):
        self.feed = feed.resolve()
        self.update = update.resolve()
        self.asset_name = update.name
        self.requests: list[dict[str, str]] = []
        self._lock = threading.Lock()

        outer = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, format, *args):  # noqa: A003
                return

            def do_GET(self):
                parsed = urlsplit(self.path)
                with outer._lock:
                    outer.requests.append({
                        'utc': dt.datetime.now(dt.timezone.utc).isoformat(),
                        'path': parsed.path,
                    })
                if parsed.path.endswith('/allowgram-update-feed.json'):
                    self._serve(outer.feed, 'application/json')
                    return
                if parsed.path.endswith('/' + outer.asset_name):
                    self._serve(outer.update, 'application/octet-stream')
                    return
                self.send_error(404)

            def _serve(self, path: Path, content_type: str):
                data = path.read_bytes()
                self.send_response(200)
                self.send_header('Content-Type', content_type)
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        self.httpd = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)

    @property
    def port(self) -> int:
        return int(self.httpd.server_address[1])

    def start(self) -> None:
        self.thread.start()

    def stop(self) -> None:
        self.httpd.shutdown()
        self.thread.join(timeout=10)
        self.httpd.server_close()


def command_keys(args: argparse.Namespace) -> int:
    output = require_new_dir(args.output)
    root_key = ed25519.Ed25519PrivateKey.generate()
    release_key = ed25519.Ed25519PrivateKey.generate()
    write_private(output / 'root-private.pem', root_key)
    write_private(output / 'release-private.pem', release_key)
    write_public(output / 'root-public.pem', root_key.public_key())
    write_public(output / 'release-public.pem', release_key.public_key())
    manifest = {
        'format': 1,
        'manifest_version': 1,
        'issued': 1700000000,
        'expires': 2000000000,
        'keys': [{
            'id': KEY_ID,
            'alg': 'Ed25519',
            'x': b64url(raw_public(release_key.public_key())),
        }],
        'channels': {'stable': [[KEY_ID]]},
        'revoked': [],
    }
    manifest_bytes = json.dumps(
        manifest,
        separators=(',', ':'),
        sort_keys=True).encode('utf-8')
    (output / 'manifest.min.json').write_bytes(manifest_bytes)
    (output / 'manifest.sig').write_bytes(root_key.sign(manifest_bytes))
    result = {
        'fixture': 'connected-updater-e2e',
        'keyId': KEY_ID,
        'rootPublicSha256': sha256(output / 'root-public.pem'),
        'releasePublicSha256': sha256(output / 'release-public.pem'),
        'manifestSha256': sha256(output / 'manifest.min.json'),
        'manifestSignatureSha256': sha256(output / 'manifest.sig'),
    }
    write_json(output / 'keys.json', result)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


def command_package(args: argparse.Namespace) -> int:
    output = require_new_dir(args.output)
    keys = args.keys.resolve()
    payload = output / 'payload'
    payload.mkdir()
    display = display_from_base_sequence(args.base, args.sequence)
    if display != args.version:
        raise RuntimeError('version does not match base/sequence')
    client = args.client.resolve()
    updater = args.updater.resolve()
    packer = args.packer.resolve()
    shutil.copy2(client, payload / 'Allowgram.exe')
    shutil.copy2(updater, payload / 'AllowgramUpdater.exe')
    build_info = {
        'product': PRODUCT,
        'channel': CHANNEL,
        'version': display,
        'base_version': args.base,
        'sequence': args.sequence,
        'fixture': 'connected-updater-e2e',
    }
    write_json(payload / 'build-info.json', build_info)
    command = [
        str(packer),
        '-path', str(payload / 'Allowgram.exe'),
        '-path', str(payload / 'AllowgramUpdater.exe'),
        '-path', str(payload / 'build-info.json'),
        '-version', str(args.base),
        '-target', PLATFORM,
        '-channel', CHANNEL,
        '-counter', str(args.sequence),
        '-keys-loc', str(keys),
        '-local-key', str(keys / 'release-private.pem'),
        '-local-key-id', KEY_ID,
    ]
    completed = subprocess.run(
        command,
        cwd=output,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT)
    (output / 'packer.log').write_text(completed.stdout, encoding='utf-8')
    if completed.returncode:
        print(completed.stdout)
        return completed.returncode
    asset_name = f'allowgram-update-stable-win-x64-{display}.tdup'
    update_path = output / asset_name
    if not update_path.is_file():
        raise RuntimeError(f'expected package was not produced: {update_path}')
    update_sha = sha256(update_path)
    signed_feed = {
        'format': 1,
        'product': PRODUCT,
        'channel': CHANNEL,
        'release': {
            'tag': 'v' + display,
            'draft': False,
            'prerelease': False,
        },
        'version': {
            'display': display,
            'base': args.base,
            'sequence': args.sequence,
        },
        'files': {
            PLATFORM: {
                'os': ASSET_OS,
                'arch': ASSET_ARCH,
                'file': asset_name,
                'size': update_path.stat().st_size,
                'sha256': update_sha,
            },
        },
    }
    signed_bytes = json.dumps(
        signed_feed,
        ensure_ascii=False,
        separators=(',', ':')).encode('utf-8')
    private_key = serialization.load_pem_private_key(
        (keys / 'release-private.pem').read_bytes(),
        password=None)
    signature = private_key.sign(FEED_SIGNING_DOMAIN + signed_bytes)
    private_key.public_key().verify(signature, FEED_SIGNING_DOMAIN + signed_bytes)
    feed = {
        'format': 1,
        'signed': b64url(signed_bytes),
        'signatures': [{
            'key_id': KEY_ID,
            'signature': b64url(signature),
        }],
    }
    feed_path = output / 'allowgram-update-feed.json'
    feed_path.write_text(json.dumps(feed, separators=(',', ':')) + '\n', encoding='utf-8')
    result = {
        'fixture': 'connected-updater-e2e',
        'version': display,
        'baseVersion': args.base,
        'updateSequence': args.sequence,
        'tag': 'v' + display,
        'keyId': KEY_ID,
        'payload': str(payload),
        'payloadHashes': {
            'Allowgram.exe': sha256(payload / 'Allowgram.exe'),
            'AllowgramUpdater.exe': sha256(payload / 'AllowgramUpdater.exe'),
            'build-info.json': sha256(payload / 'build-info.json'),
        },
        'payloadPeVersions': {
            'Allowgram.exe': pe_version(payload / 'Allowgram.exe'),
            'AllowgramUpdater.exe': pe_version(payload / 'AllowgramUpdater.exe'),
        },
        'updateAsset': asset_name,
        'updatePath': str(update_path),
        'updateSha256': update_sha,
        'updateSize': update_path.stat().st_size,
        'feedAsset': 'allowgram-update-feed.json',
        'feedPath': str(feed_path),
        'feedSha256': sha256(feed_path),
        'keys': str(keys),
        'packer': str(packer),
        'packerLog': str(output / 'packer.log'),
    }
    write_json(output / 'package.json', result)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


def read_json(path: Path) -> object:
    return json.loads(path.read_text(encoding='utf-8'))


def sample_stage(work_dir: Path, samples: dict[str, dict[str, object]]) -> None:
    for relative in (
            'tupdates/temp/ready',
            'tupdates/temp/Allowgram.exe',
            'tupdates/temp/AllowgramUpdater.exe',
            'tupdates/temp/build-info.json',
            'tupdates/temp/tdata/version',
            'tupdates/temp/tdata/stage-manifest.bin'):
        path = work_dir / relative
        key = relative.replace('/', '\\')
        if key in samples or not path.is_file():
            continue
        samples[key] = {
            'sha256': sha256(path),
            'size': path.stat().st_size,
            'modifiedUtc': dt.datetime.fromtimestamp(
                path.stat().st_mtime,
                dt.timezone.utc).isoformat(),
        }

def collect_log_summary(work_dir: Path) -> dict[str, object]:
    paths: list[Path] = []
    for path in [work_dir / 'log.txt', *sorted((work_dir / 'DebugLogs').glob('log_*.txt'))]:
        if path.is_file() and path not in paths:
            paths.append(path)
    lines: list[str] = []
    for path in paths:
        lines.extend(path.read_text(encoding='utf-8', errors='replace').splitlines())
    return {
        'paths': [str(path) for path in paths],
        'updaterLaunchLines': [
            line for line in lines
            if 'Application Info: executing' in line and 'AllowgramUpdater.exe' in line
        ],
        'relaunchCommandLines': [
            line for line in lines
            if 'Command line:' in line and '-noupdate' in line
        ],
    }

def command_run(args: argparse.Namespace) -> int:
    output = require_new_dir(args.output)
    package = read_json(args.package_json.resolve())
    install = output / 'install'
    work = output / 'work'
    install.mkdir()
    work.mkdir()
    shutil.copy2(args.old_client.resolve(), install / 'Allowgram.exe')
    shutil.copy2(args.old_updater.resolve(), install / 'AllowgramUpdater.exe')
    sentinels = {
        'syntheticAccount': work / 'tdata' / 'synthetic-account-sentinel.txt',
        'syntheticAllowlist': work / 'tdata' / 'synthetic-allowlist-sentinel.txt',
        'syntheticDraft': work / 'tdata' / 'synthetic-DRAFT-sentinel.txt',
    }
    for name, path in sentinels.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(f'{name}: preserve me\n', encoding='utf-8')
    before = {
        'clientSha256': sha256(install / 'Allowgram.exe'),
        'updaterSha256': sha256(install / 'AllowgramUpdater.exe'),
        'clientPeVersion': pe_version(install / 'Allowgram.exe'),
        'updaterPeVersion': pe_version(install / 'AllowgramUpdater.exe'),
        'sentinels': {name: path.read_text(encoding='utf-8') for name, path in sentinels.items()},
    }
    server = FixtureServer(
        Path(str(package['feedPath'])),
        Path(str(package['updatePath'])))
    server.start()
    native_report = output / 'native-report.json'
    relaunch_report = output / 'relaunch-report.json'
    env = os.environ.copy()
    env.update({
        'ALLOWGRAM_CONNECTED_E2E_REPORT': str(native_report),
        'ALLOWGRAM_CONNECTED_E2E_RELAUNCH_REPORT': str(relaunch_report),
        'ALLOWGRAM_CONNECTED_E2E_RELAUNCH_SEQUENCE': str(args.new_sequence),
        'ALLOWGRAM_CONNECTED_E2E_RELAUNCH_HOLD_MS': '1200',
        'ALLOWGRAM_CONNECTED_UPDATE_FEED_URL':
            f'http://127.0.0.1:{server.port}/allowgram-update-feed.json',
        'ALLOWGRAM_CONNECTED_UPDATE_DOWNLOAD_BASE':
            f'http://127.0.0.1:{server.port}/download/v{args.version}/',
        'PYTHONUTF8': '1',
    })
    observer = ProcessObserver(install)
    stage_samples: dict[str, dict[str, object]] = {}
    failures: list[str] = []
    old_process = None
    timed_out = False
    try:
        command = [
            str(install / 'Allowgram.exe'),
            '-many',
            '-debug',
            '-workdir',
            str(work),
        ]
        old_process = subprocess.Popen(command, cwd=install, env=env)
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            observer.sample()
            sample_stage(work, stage_samples)
            if relaunch_report.is_file() and old_process.poll() is not None:
                time.sleep(1.0)
                observer.sample()
                sample_stage(work, stage_samples)
                break
            time.sleep(0.05)
        else:
            timed_out = True
            failures.append('connected updater E2E timed out')
            if old_process.poll() is None:
                old_process.terminate()
            failures.extend(f'terminated owned pid {pid}' for pid in observer.terminate_owned())
    finally:
        server.stop()
        observer.sample()
        sample_stage(work, stage_samples)

    if old_process is not None and old_process.poll() is None:
        try:
            old_process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            failures.append('old process remained alive after termination request')
    process_records = observer.as_list()
    observer.close()

    native = read_json(native_report) if native_report.is_file() else None
    relaunch = read_json(relaunch_report) if relaunch_report.is_file() else None
    after = {
        'clientSha256': sha256(install / 'Allowgram.exe') if (install / 'Allowgram.exe').is_file() else '',
        'updaterSha256': sha256(install / 'AllowgramUpdater.exe') if (install / 'AllowgramUpdater.exe').is_file() else '',
        'clientPeVersion': pe_version(install / 'Allowgram.exe') if (install / 'Allowgram.exe').is_file() else {},
        'updaterPeVersion': pe_version(install / 'AllowgramUpdater.exe') if (install / 'AllowgramUpdater.exe').is_file() else {},
        'sentinels': {
            name: path.read_text(encoding='utf-8') if path.is_file() else ''
            for name, path in sentinels.items()
        },
        'tupdatesExists': (work / 'tupdates').exists(),
    }

    log_summary = collect_log_summary(work)

    def check(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    request_names = [request['path'].rsplit('/', 1)[-1] for request in server.requests]
    check('allowgram-update-feed.json' in request_names, 'release feed was not requested')
    check(str(package['updateAsset']) in request_names, 'signed update asset was not requested')
    check(native is not None, 'native updater report is missing')
    if isinstance(native, dict):
        check(native.get('finished') is True, 'native report did not finish cleanly')
        check(native.get('failures') == 0, f'native checks failed: {native.get("checks")}')
        check(native.get('updateReady') is True, 'native did not report authenticated ready update')
        check(native.get('updateButtonClicked') is True, 'native did not click real bottom update button')
        check(native.get('finalConfirmationAccepted') is True, 'native final confirmation was not accepted')
        check(str(native.get('buildAllowgramSequence')) in ('8', '8.0'), 'old client sequence is not 8')
    check(relaunch is not None, 'new client relaunch report is missing')
    if isinstance(relaunch, dict):
        check(str(relaunch.get('buildAllowgramSequence')) in (str(args.new_sequence), f'{args.new_sequence}.0'),
              'relaunched client sequence does not match new fixture')
        arguments = relaunch.get('arguments') or []
        check(isinstance(arguments, list), 'relaunched client arguments report is malformed')
    updater_launch_text = '\n'.join(str(line) for line in log_summary['updaterLaunchLines'])
    relaunch_command_text = '\n'.join(str(line) for line in log_summary['relaunchCommandLines'])
    normalized_work = str(work).replace('\\', '/').rstrip('/')
    normalized_relaunch_command = relaunch_command_text.replace('\\', '/').rstrip('/')
    check('AllowgramUpdater.exe' in updater_launch_text and ' -update' in updater_launch_text,
          'logged helper launch command is missing')
    check('-stagehash' in updater_launch_text and '-exename "Allowgram.exe"' in updater_launch_text,
          'logged helper launch did not include authenticated stage hash and exact exe name')
    if isinstance(native, dict) and native.get('readyStageHash'):
        check(str(native.get('readyStageHash')) in updater_launch_text,
              'logged helper launch stage hash did not match native ready stage hash')
    check('-noupdate' in relaunch_command_text,
          'logged relaunched client command did not include -noupdate')
    check('-workdir' in relaunch_command_text and normalized_work in normalized_relaunch_command,
          'logged relaunched client command did not preserve the synthetic workdir')
    check(old_process is not None and old_process.returncode == 0,
          f'old client exit code was {None if old_process is None else old_process.returncode}')
    check(after['clientSha256'] == sha256(args.new_client.resolve()),
          'installed Allowgram.exe does not match new fixture payload')
    check(after['updaterSha256'] == sha256(args.new_updater.resolve()),
          'installed AllowgramUpdater.exe does not match new fixture payload')
    check(after['clientPeVersion'].get('file') == args.version,
          'installed Allowgram.exe PE file version does not match target')
    check(after['clientPeVersion'].get('product') == args.version,
          'installed Allowgram.exe PE product version does not match target')
    check(after['updaterPeVersion'].get('file') == args.version,
          'installed helper PE file version does not match target')
    check(after['sentinels'] == before['sentinels'],
          'synthetic account/allowlist/DRAFT sentinels changed')
    check(not (work / 'tupdates' / 'temp' / 'ready').exists(),
          'ready marker remained after helper apply')
    roles = [record.get('role') for record in process_records]
    client_pids = [
        int(record['pid']) for record in process_records
        if record.get('role') == 'client'
    ]
    helper_pids = [
        int(record['pid']) for record in process_records
        if record.get('role') == 'helper'
    ]
    check(len(client_pids) >= 2, f'expected old and relaunched client processes, saw {client_pids}')
    check(helper_pids, 'expected actual AllowgramUpdater.exe helper process')
    if old_process is not None:
        check(old_process.pid in client_pids, 'old client pid was not observed by process watcher')
        check(any(pid != old_process.pid for pid in client_pids),
              'no distinct relaunched client pid was observed')
    check('tupdates\\temp\\AllowgramUpdater.exe' in stage_samples,
          'staged AllowgramUpdater.exe hash was not sampled before apply')
    check('tupdates\\temp\\Allowgram.exe' in stage_samples,
          'staged Allowgram.exe hash was not sampled before apply')
    if timed_out:
        check(False, 'run timed out before relaunch receipt')

    result = {
        'pass': not failures,
        'failures': failures,
        'command': command if old_process is not None else [],
        'oldPid': old_process.pid if old_process is not None else None,
        'oldExitCode': old_process.returncode if old_process is not None else None,
        'before': before,
        'after': after,
        'package': package,
        'nativeReport': native,
        'relaunchReport': relaunch,
        'server': {
            'port': server.port,
            'requests': server.requests,
        },
        'stageSamples': stage_samples,
        'processes': process_records,
        'logSummary': log_summary,
        'paths': {
            'output': str(output),
            'install': str(install),
            'work': str(work),
            'nativeReport': str(native_report),
            'relaunchReport': str(relaunch_report),
        },
    }
    write_json(output / 'results.json', result)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if not failures else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest='command', required=True)

    keys = subparsers.add_parser('keys')
    keys.add_argument('--output', type=Path, required=True)
    keys.set_defaults(func=command_keys)

    package = subparsers.add_parser('package')
    package.add_argument('--output', type=Path, required=True)
    package.add_argument('--keys', type=Path, required=True)
    package.add_argument('--client', type=Path, required=True)
    package.add_argument('--updater', type=Path, required=True)
    package.add_argument('--packer', type=Path, required=True)
    package.add_argument('--version', required=True)
    package.add_argument('--base', type=int, required=True)
    package.add_argument('--sequence', type=int, required=True)
    package.set_defaults(func=command_package)

    run = subparsers.add_parser('run')
    run.add_argument('--output', type=Path, required=True)
    run.add_argument('--old-client', type=Path, required=True)
    run.add_argument('--old-updater', type=Path, required=True)
    run.add_argument('--new-client', type=Path, required=True)
    run.add_argument('--new-updater', type=Path, required=True)
    run.add_argument('--package-json', type=Path, required=True)
    run.add_argument('--version', required=True)
    run.add_argument('--new-sequence', type=int, required=True)
    run.add_argument('--timeout', type=int, default=300)
    run.set_defaults(func=command_run)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return args.func(args)
    except Exception as error:
        print(f'test_connected_update_e2e.py: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
