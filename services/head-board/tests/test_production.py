import base64
import os

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from fastapi.testclient import TestClient

from headboard.api import configured_app


def production(monkeypatch, tmp_path):
    key = Ed25519PrivateKey.generate()
    public = base64.urlsafe_b64encode(key.public_key().public_bytes_raw()).decode().rstrip('=')
    monkeypatch.setenv('HEAD_PRODUCTION', '1')
    monkeypatch.setenv('HEAD_DATA_DIR', str(tmp_path))
    monkeypatch.setenv('HEAD_PUBLIC_URL', 'https://board.example.com')
    monkeypatch.setenv('HEAD_OWNER_ID', '8683512953')
    monkeypatch.setenv('HEAD_EXPECTED_PUBLIC_KEY', public)
    monkeypatch.delenv('HEAD_BOOTSTRAP_KEY', raising=False)
    return key, public


def test_production_refuses_missing_key_instead_of_rotating(monkeypatch, tmp_path):
    production(monkeypatch, tmp_path)
    with pytest.raises(ValueError, match='key'):
        configured_app()
    assert not (tmp_path/'policy-ed25519.key').exists()


def test_production_bootstraps_explicit_pinned_key_and_survives_restart(monkeypatch, tmp_path):
    key, public = production(monkeypatch, tmp_path)
    bootstrap = base64.urlsafe_b64encode(key.private_bytes_raw()).decode().rstrip('=')
    monkeypatch.setenv('HEAD_BOOTSTRAP_KEY', bootstrap)
    with TestClient(configured_app(), base_url='https://board.example.com') as client:
        assert client.get('/api/health').json()['policy_public_key'] == public
    path = tmp_path/'policy-ed25519.key'
    assert path.read_bytes() == key.private_bytes_raw()
    assert path.stat().st_mode & 0o777 == 0o600
    monkeypatch.delenv('HEAD_BOOTSTRAP_KEY', raising=False)
    with TestClient(configured_app(), base_url='https://board.example.com') as client:
        assert client.get('/api/health').json()['policy_public_key'] == public


def test_production_rejects_wrong_pin_and_missing_origin(monkeypatch, tmp_path):
    key, public = production(monkeypatch, tmp_path)
    path = tmp_path/'policy-ed25519.key'
    path.write_bytes(key.private_bytes_raw())
    path.chmod(0o600)
    monkeypatch.setenv('HEAD_EXPECTED_PUBLIC_KEY', 'A'*43)
    with pytest.raises(ValueError, match='key'):
        configured_app()
    monkeypatch.setenv('HEAD_EXPECTED_PUBLIC_KEY', public)
    monkeypatch.delenv('HEAD_PUBLIC_URL')
    with pytest.raises(ValueError, match='origin'):
        configured_app()
    assert path.read_bytes() == key.private_bytes_raw()
