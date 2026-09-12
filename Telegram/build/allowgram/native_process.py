import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import os
from pathlib import Path


def require_session_zero():
    session = wintypes.DWORD()
    if not ctypes.windll.kernel32.ProcessIdToSessionId(os.getpid(), ctypes.byref(session)):
        raise ctypes.WinError()
    if session.value != 0:
        raise RuntimeError("Native regression must run in noninteractive Windows session 0.")


def process_identity(process, executable):
    kernel = ctypes.windll.kernel32
    handle = wintypes.HANDLE(int(process._handle))
    size = wintypes.DWORD(32768)
    path = ctypes.create_unicode_buffer(size.value)
    if not kernel.QueryFullProcessImageNameW(handle, 0, path, ctypes.byref(size)):
        raise ctypes.WinError()
    times = [wintypes.FILETIME() for _ in range(4)]
    if not kernel.GetProcessTimes(handle, *(ctypes.byref(value) for value in times)):
        raise ctypes.WinError()
    start = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
    if Path(path.value).resolve() != Path(executable).resolve():
        raise RuntimeError("Unexpected owned process executable.")
    return dict(executable=path.value, pid=process.pid, startFileTime=start,
                startUtc=datetime.fromtimestamp(start / 10000000 - 11644473600, timezone.utc).isoformat())


def stop_owned(process, executable, identity):
    if process.poll() is not None:
        return
    if process_identity(process, executable) != identity:
        raise RuntimeError("Refusing to stop changed process identity.")
    process.kill()
    process.wait()
