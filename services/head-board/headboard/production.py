"""Production bootstrap never invents/replaces a pinned management key."""
import base64
import hmac
import os
from pathlib import Path
from urllib.parse import urlsplit

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey


def encoded(raw):
    return base64.urlsafe_b64encode(raw).decode().rstrip('=')


def prepare():
    origin = os.environ.get('HEAD_PUBLIC_URL', '')
    url = urlsplit(origin)
    if (url.scheme != 'https' or not url.hostname or url.username or url.password
            or url.query or url.fragment or url.path not in ('', '/')):
        raise ValueError('Production HTTPS origin is required')
    directory = os.environ.get('HEAD_DATA_DIR')
    if not directory:
        raise ValueError('Production persistent data directory is required')
    owner = os.environ.get('HEAD_OWNER_ID', '')
    if not owner.isascii() or not owner.isdigit() or not 0 < int(owner) <= 281474976710655:
        raise ValueError('Production owner ID is required')
    expected = os.environ.get('HEAD_EXPECTED_PUBLIC_KEY', '')
    if not expected:
        raise ValueError('Production expected public key is required')
    data = Path(directory)
    data.mkdir(parents=True, exist_ok=True)
    data.chmod(0o700)
    path = data / 'policy-ed25519.key'
    if path.is_symlink():
        raise ValueError('Production key must be a regular private file')
    bootstrap = os.environ.pop('HEAD_BOOTSTRAP_KEY', None)
    if path.exists():
        raw = path.read_bytes()
        if path.stat().st_mode & 0o077:
            raise ValueError('Production key permissions must be private')
    elif bootstrap:
        try:
            raw = base64.b64decode(bootstrap + '=', altchars=b'-_', validate=True)
            if len(raw) != 32 or encoded(raw) != bootstrap:
                raise ValueError()
        except ValueError:
            raise ValueError('Invalid production bootstrap key') from None
    else:
        raise ValueError('Production key missing; refusing automatic rotation')
    if len(raw) != 32:
        raise ValueError('Invalid production key size')
    public = encoded(Ed25519PrivateKey.from_private_bytes(raw).public_key().public_bytes_raw())
    if not hmac.compare_digest(public, expected):
        raise ValueError('Production signing key does not match pinned public key')
    if not path.exists():
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'wb') as output:
            output.write(raw)
            output.flush()
            os.fsync(output.fileno())
    return public
