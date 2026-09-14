#!/usr/bin/env python3
import argparse
import base64
import hashlib
import json
from pathlib import Path
import struct
import sys
import time

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ed25519

MAGIC = b'TDUP'
FORMAT = 2
CHANNEL_STABLE = 0
OS_WINDOWS = 0
ARCH_X64 = 1
MAX_MANIFEST_SIZE = 64 * 1024
MAX_MANIFEST_SIGNATURE_SIZE = 512
MAX_SIGNATURES = 8
MAX_KEY_ID_SIZE = 64
MAX_SIGNATURE_SIZE = 512
MAX_PAYLOAD_SIZE = 256 * 1024 * 1024
FEED_SIGNING_DOMAIN = b'Allowgram stable release feed v1\n'
PLATFORM = 'win64'
ASSET_OS = 'win'
ASSET_ARCH = 'x64'
PRODUCT = 'Allowgram'
CHANNEL = 'stable'


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b'=').decode('ascii')


def b64url_decode(text: str) -> bytes:
    return base64.urlsafe_b64decode(text + '=' * (-len(text) % 4))


def read_u8(data: bytes, offset: int) -> tuple[int, int]:
    if offset + 1 > len(data):
        raise ValueError('truncated u8')
    return data[offset], offset + 1


def read_u32(data: bytes, offset: int) -> tuple[int, int]:
    if offset + 4 > len(data):
        raise ValueError('truncated u32')
    return struct.unpack_from('<I', data, offset)[0], offset + 4


def read_u64(data: bytes, offset: int) -> tuple[int, int]:
    if offset + 8 > len(data):
        raise ValueError('truncated u64')
    return struct.unpack_from('<Q', data, offset)[0], offset + 8


def read_bytes(data: bytes, offset: int, length: int, label: str) -> tuple[bytes, int]:
    if length <= 0 or offset + length > len(data):
        raise ValueError(f'bad {label}')
    return data[offset:offset + length], offset + length


def append_u32(parts: list[bytes], value: int) -> None:
    parts.append(struct.pack('<I', value))


def append_signature(parts: list[bytes], key_id: str, signature: bytes) -> None:
    key_bytes = key_id.encode('ascii')
    if not key_bytes or len(key_bytes) > MAX_KEY_ID_SIZE:
        raise ValueError('bad key id size')
    if not signature or len(signature) > MAX_SIGNATURE_SIZE:
        raise ValueError('bad signature size')
    append_u32(parts, len(key_bytes))
    parts.append(key_bytes)
    append_u32(parts, len(signature))
    parts.append(signature)


def parse_unsigned_envelope(data: bytes) -> dict:
    if len(data) < 4 or data[:4] != MAGIC:
        raise ValueError('bad envelope magic')
    offset = 4
    fmt, offset = read_u32(data, offset)
    if fmt != FORMAT:
        raise ValueError('bad envelope format')
    channel, offset = read_u8(data, offset)
    os_id, offset = read_u8(data, offset)
    arch_id, offset = read_u8(data, offset)
    version, offset = read_u64(data, offset)
    created, offset = read_u64(data, offset)
    manifest_size, offset = read_u32(data, offset)
    if not manifest_size or manifest_size > MAX_MANIFEST_SIZE:
        raise ValueError('bad manifest size')
    manifest, offset = read_bytes(data, offset, manifest_size, 'manifest')
    signed_region = data[:offset]
    manifest_sig_size, offset = read_u32(data, offset)
    if not manifest_sig_size or manifest_sig_size > MAX_MANIFEST_SIGNATURE_SIZE:
        raise ValueError('bad manifest signature size')
    manifest_sig, offset = read_bytes(data, offset, manifest_sig_size, 'manifest signature')
    count, offset = read_u32(data, offset)
    if count > MAX_SIGNATURES:
        raise ValueError('bad envelope signature count')
    if count != 0:
        raise ValueError('unsigned envelope already contains signatures')
    payload_size, offset = read_u32(data, offset)
    if not payload_size or payload_size > MAX_PAYLOAD_SIZE:
        raise ValueError('bad payload size')
    payload, offset = read_bytes(data, offset, payload_size, 'payload')
    if offset != len(data):
        raise ValueError('trailing bytes after payload')
    if channel != CHANNEL_STABLE or os_id != OS_WINDOWS or arch_id != ARCH_X64:
        raise ValueError('unsigned update is not stable Windows x64')
    return {
        'version': version,
        'created': created,
        'manifest': manifest,
        'manifest_sig': manifest_sig,
        'signed_region': signed_region,
        'payload': payload,
    }


def load_private_key(path: Path) -> ed25519.Ed25519PrivateKey:
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, ed25519.Ed25519PrivateKey):
        raise ValueError('release private key is not Ed25519')
    return key


def load_public_key(path: Path) -> ed25519.Ed25519PublicKey:
    key = serialization.load_pem_public_key(path.read_bytes())
    if not isinstance(key, ed25519.Ed25519PublicKey):
        raise ValueError('root public key is not Ed25519')
    return key


def parse_manifest(manifest_bytes: bytes, manifest_sig: bytes, root_public: Path) -> dict:
    root = load_public_key(root_public)
    root.verify(manifest_sig, manifest_bytes)
    manifest = json.loads(manifest_bytes.decode('utf-8'))
    if manifest.get('format') != 1:
        raise ValueError('unsupported manifest format')
    return manifest


def manifest_key(manifest: dict, key_id: str) -> dict:
    keys = [item for item in manifest.get('keys', []) if item.get('id') == key_id]
    if len(keys) != 1:
        raise ValueError(f'manifest key {key_id!r} not found exactly once')
    key = keys[0]
    if key.get('alg') != 'Ed25519':
        raise ValueError(f'manifest key {key_id!r} is not Ed25519')
    stable = manifest.get('channels', {}).get(CHANNEL)
    if not isinstance(stable, list) or [key_id] not in stable:
        raise ValueError(f'manifest key {key_id!r} is not authorized for stable')
    if key_id in manifest.get('revoked', []):
        raise ValueError(f'manifest key {key_id!r} is revoked')
    now = int(time.time())
    if manifest.get('expires', 0) and manifest['expires'] <= now:
        raise ValueError('manifest is expired')
    if key.get('expires', 0) and key['expires'] <= now:
        raise ValueError(f'manifest key {key_id!r} is expired')
    return key


def display_from_base_sequence(base: int, sequence: int) -> str:
    major = base // 1000000
    minor = (base // 1000) % 1000
    patch = base % 1000
    if major <= 0 or minor > 999 or patch > 999 or sequence <= 0 or sequence > 65535:
        raise ValueError('bad Allowgram base/sequence')
    return f'{major}.{minor}.{patch}.{sequence}'


def request_value(request: dict | None, key: str, fallback):
    if request is None:
        return fallback
    value = request.get(key, fallback)
    if fallback is not None and value != fallback:
        raise ValueError(f'request {key} does not match CLI value')
    return value


def load_request(path: Path | None, allow_fixture: bool) -> dict | None:
    if path is None:
        return None
    request = json.loads(path.read_text(encoding='utf-8'))
    if request.get('product') != PRODUCT:
        raise ValueError('request is not for Allowgram')
    if request.get('channel') != CHANNEL:
        raise ValueError('request is not for stable')
    if request.get('platform') != PLATFORM:
        raise ValueError('request is not for Windows x64')
    if request.get('productionReady') is not True and not allow_fixture:
        raise ValueError('request is marked non-production; pass --allow-fixture for scoped tests')
    return request


def main() -> int:
    parser = argparse.ArgumentParser(description='Sign an Allowgram stable update and release feed.')
    parser.add_argument('--unsigned-update', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--private-key', required=True, type=Path)
    parser.add_argument('--key-id', required=True)
    parser.add_argument('--root-public', required=True, type=Path)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--manifest-sig', required=True, type=Path)
    parser.add_argument('--version')
    parser.add_argument('--base', type=int)
    parser.add_argument('--sequence', type=int)
    parser.add_argument('--request-json', type=Path)
    parser.add_argument('--allow-fixture', action='store_true')
    args = parser.parse_args()

    try:
        request = load_request(args.request_json, args.allow_fixture)
        if request:
            args.version = request_value(request, 'version', args.version)
            args.base = int(request_value(request, 'baseVersion', args.base))
            args.sequence = int(request_value(request, 'updateSequence', args.sequence))
            expected_unsigned = request.get('unsignedUpdateSha256')
            expected_input = request.get('signingInputSha256')
        else:
            expected_unsigned = None
            expected_input = None
        if not args.version or args.base is None or args.sequence is None:
            raise ValueError('--version, --base and --sequence are required without --request-json')
        display = display_from_base_sequence(args.base, args.sequence)
        if display != args.version:
            raise ValueError('version does not match base/sequence')
        asset_name = f'allowgram-update-stable-win-x64-{display}.tdup'
        tag = 'v' + display

        unsigned_bytes = args.unsigned_update.read_bytes()
        unsigned_sha = hashlib.sha256(unsigned_bytes).hexdigest()
        if expected_unsigned and unsigned_sha != expected_unsigned:
            raise ValueError('unsigned update hash does not match request')
        envelope = parse_unsigned_envelope(unsigned_bytes)
        expected_version = (args.base << 32) | args.sequence
        if envelope['version'] != expected_version:
            raise ValueError('unsigned update version does not match requested Allowgram sequence')

        manifest_bytes = args.manifest.read_bytes()
        manifest_sig = args.manifest_sig.read_bytes()
        if envelope['manifest'] != manifest_bytes or envelope['manifest_sig'] != manifest_sig:
            raise ValueError('unsigned update does not carry the requested manifest')
        manifest = parse_manifest(manifest_bytes, manifest_sig, args.root_public)
        key = manifest_key(manifest, args.key_id)

        private_key = load_private_key(args.private_key)
        public_raw = private_key.public_key().public_bytes(
            encoding=serialization.Encoding.Raw,
            format=serialization.PublicFormat.Raw)
        if public_raw != b64url_decode(key['x']):
            raise ValueError('release private key does not match the manifest key id')

        signing_input = envelope['signed_region'] + hashlib.sha256(envelope['payload']).digest()
        signing_input_sha = hashlib.sha256(signing_input).hexdigest()
        if expected_input and signing_input_sha != expected_input:
            raise ValueError('signing input hash does not match request')
        signature = private_key.sign(signing_input)
        private_key.public_key().verify(signature, signing_input)

        output_dir = args.output_dir.resolve()
        output_dir.mkdir(parents=True, exist_ok=False)
        update_path = output_dir / asset_name
        parts = [
            envelope['signed_region'],
            struct.pack('<I', len(manifest_sig)),
            manifest_sig,
            struct.pack('<I', 1),
        ]
        append_signature(parts, args.key_id, signature)
        append_u32(parts, len(envelope['payload']))
        parts.append(envelope['payload'])
        update_bytes = b''.join(parts)
        update_path.write_bytes(update_bytes)
        update_sha = hashlib.sha256(update_bytes).hexdigest()

        signed_feed = {
            'format': 1,
            'product': PRODUCT,
            'channel': CHANNEL,
            'release': {
                'tag': tag,
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
                    'size': len(update_bytes),
                    'sha256': update_sha,
                },
            },
        }
        signed_bytes = json.dumps(
            signed_feed,
            ensure_ascii=False,
            separators=(',', ':'),
        ).encode('utf-8')
        feed_signature = private_key.sign(FEED_SIGNING_DOMAIN + signed_bytes)
        private_key.public_key().verify(feed_signature, FEED_SIGNING_DOMAIN + signed_bytes)
        feed = {
            'format': 1,
            'signed': b64url(signed_bytes),
            'signatures': [{
                'key_id': args.key_id,
                'signature': b64url(feed_signature),
            }],
        }
        feed_path = output_dir / 'allowgram-update-feed.json'
        feed_path.write_text(json.dumps(feed, separators=(',', ':')) + '\n', encoding='utf-8')

        metadata = {
            'product': PRODUCT,
            'version': display,
            'tag': tag,
            'baseVersion': args.base,
            'updateSequence': args.sequence,
            'platform': PLATFORM,
            'keyId': args.key_id,
            'unsignedUpdateSha256': unsigned_sha,
            'signingInputSha256': signing_input_sha,
            'updateAsset': asset_name,
            'updateSha256': update_sha,
            'feedAsset': 'allowgram-update-feed.json',
            'feedSha256': hashlib.sha256(feed_path.read_bytes()).hexdigest(),
        }
        (output_dir / 'release-assets.json').write_text(
            json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
        sha_lines = []
        for path in sorted(output_dir.iterdir()):
            if path.is_file():
                sha_lines.append(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}')
        (output_dir / 'SHA256SUMS.txt').write_text('\n'.join(sha_lines) + '\n', encoding='utf-8')
        print('Signed update:', update_path)
        print('Signed feed:', feed_path)
        return 0
    except (InvalidSignature, OSError, ValueError, json.JSONDecodeError) as error:
        print('sign_release.py:', error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())