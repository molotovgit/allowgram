"""Create a signed Allowgram v2 update fixture with generated test keys."""

import argparse
import base64
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from cryptography.hazmat.primitives.asymmetric import ed25519
from cryptography.hazmat.primitives import serialization


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b'=').decode('ascii')


def write_private(path: Path, key: ed25519.Ed25519PrivateKey) -> None:
    path.write_bytes(key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption()))


def write_public(path: Path, key: ed25519.Ed25519PublicKey) -> None:
    path.write_bytes(key.public_bytes(
        serialization.Encoding.PEM,
        serialization.PublicFormat.SubjectPublicKeyInfo))


def raw_public(key: ed25519.Ed25519PublicKey) -> bytes:
    return key.public_bytes(
        serialization.Encoding.Raw,
        serialization.PublicFormat.Raw)


parser = argparse.ArgumentParser()
parser.add_argument('--repository', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--base-version', type=int, default=7002008)
parser.add_argument('--sequence', type=int, default=9)
parser.add_argument('--display-version', default='7.2.8.9')
args = parser.parse_args()

repository = args.repository.resolve()
output = args.output.resolve()
if output.exists():
    raise RuntimeError(f'fixture output already exists: {output}')
output.mkdir(parents=True)
keys = output / 'keys'
payload = output / 'payload'
keys.mkdir()
payload.mkdir()

root_key = ed25519.Ed25519PrivateKey.generate()
release_key = ed25519.Ed25519PrivateKey.generate()
write_private(keys / 'root-private.pem', root_key)
write_private(keys / 'release-private.pem', release_key)
write_public(keys / 'root-public.pem', root_key.public_key())
write_public(keys / 'release-public.pem', release_key.public_key())

manifest = {
    'format': 1,
    'manifest_version': 1,
    'issued': 1700000000,
    'expires': 2000000000,
    'keys': [{
        'id': 'fixture-release',
        'alg': 'Ed25519',
        'x': b64url(raw_public(release_key.public_key())),
    }],
    'channels': {'stable': [['fixture-release']]},
    'revoked': [],
}
manifest_bytes = json.dumps(
    manifest,
    separators=(',', ':'),
    sort_keys=True).encode('utf-8')
(keys / 'manifest.min.json').write_bytes(manifest_bytes)
(keys / 'manifest.sig').write_bytes(root_key.sign(manifest_bytes))

release = repository / 'out' / 'Release'
client = release / 'Telegram.exe'
updater = release / 'Updater.exe'
packer = release / 'Packer.exe'
for required in (client, updater, packer):
    if not required.is_file():
        raise RuntimeError(f'missing required build output: {required}')
shutil.copy2(client, payload / 'Allowgram.exe')
shutil.copy2(updater, payload / 'AllowgramUpdater.exe')
build_info = {
    'product': 'Allowgram',
    'channel': 'stable',
    'version': args.display_version,
    'base_version': args.base_version,
    'sequence': args.sequence,
    'fixture': True,
}
(payload / 'build-info.json').write_text(
    json.dumps(build_info, separators=(',', ':'), sort_keys=True),
    encoding='utf-8')

command = [
    str(packer),
    '-path', str(payload / 'Allowgram.exe'),
    '-path', str(payload / 'AllowgramUpdater.exe'),
    '-path', str(payload / 'build-info.json'),
    '-version', str(args.base_version),
    '-target', 'win64',
    '-channel', 'stable',
    '-counter', str(args.sequence),
    '-keys-loc', str(keys),
    '-local-key', str(keys / 'release-private.pem'),
    '-local-key-id', 'fixture-release',
]
completed = subprocess.run(
    command,
    cwd=output,
    text=True,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT)
(output / 'packer.log').write_text(completed.stdout, encoding='utf-8')
if completed.returncode:
    raise SystemExit(completed.returncode)

package = output / f'allowgram-update-stable-win-x64-{args.display_version}.tdup'
if not package.is_file():
    raise RuntimeError(f'expected package was not produced: {package}')
result = {
    'fixture': True,
    'keys': str(keys),
    'payload': str(payload),
    'package': str(package),
    'packageSha256': hashlib.sha256(package.read_bytes()).hexdigest(),
    'manifestSha256': hashlib.sha256(manifest_bytes).hexdigest(),
    'rootPublicSha256': hashlib.sha256((keys / 'root-public.pem').read_bytes()).hexdigest(),
    'releaseKeyId': 'fixture-release',
    'displayVersion': args.display_version,
    'baseVersion': args.base_version,
    'sequence': args.sequence,
    'packerLog': str(output / 'packer.log'),
}
(output / 'fixture.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result, indent=2))