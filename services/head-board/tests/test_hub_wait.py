from concurrent.futures import ThreadPoolExecutor, TimeoutError
import json
import time

from fastapi.testclient import TestClient
import pytest

from headboard.api import create_app, configured_app
from test_hub_service import SERVICE, SERVICE_KEY, SUBJECT, app, client, enroll, enrollment


def test_wait_unchanged_does_not_ack(client):
    body, token = enrollment()
    enroll(client, body)
    r = client.get('/api/client/policy/wait?after_revision=1&timeout=0',
                   headers={'Authorization': 'Bearer ' + token})
    assert r.status_code == 204, r.text
    assert r.content == b'' and r.headers['cache-control'] == 'no-store'
    row = client.get('/api/service/v1/users/' + SUBJECT, headers=SERVICE).json()
    assert row['devices'][0]['acked_revision'] == 0


def test_wait_reads_changes_from_another_app_instance(client, app):
    body, token = enrollment()
    first = enroll(client, body)
    other = create_app(app.state.store.directory, public_url='https://head.test',
                       hub_service_token=SERVICE_KEY)
    with TestClient(other, base_url='https://head.test') as writer, ThreadPoolExecutor() as pool:
        future = pool.submit(client.get, '/api/client/policy/wait?after_revision=1&timeout=2',
                             headers={'Authorization': 'Bearer ' + token})
        with pytest.raises(TimeoutError):
            future.result(timeout=0.05)
        started = time.monotonic()
        r = writer.put('/api/service/v1/users/' + SUBJECT + '/policy', headers=SERVICE,
                       json={'expected_revision': 1, 'peers': [], 'actor_label': 'Management'})
        assert r.status_code == 200, r.text
        received = future.result(timeout=3)
        assert received.status_code == 200, received.text
        assert received.json() != first['policy']
        assert time.monotonic() - started < 2


def test_wait_rechecks_revocation(client):
    body, token = enrollment()
    result = enroll(client, body)
    with ThreadPoolExecutor() as pool:
        future = pool.submit(client.get, '/api/client/policy/wait?after_revision=1&timeout=2',
                             headers={'Authorization': 'Bearer ' + token})
        with pytest.raises(TimeoutError):
            future.result(timeout=0.05)
        r = client.post(f'/api/service/v1/users/{SUBJECT}/devices/{result["device_id"]}/revoke',
                        headers=SERVICE, json={})
        assert r.status_code == 200, r.text
        assert future.result(timeout=3).status_code == 401


def test_wait_rejects_invalid_auth_and_future_revision(client):
    assert client.get('/api/client/policy/wait?after_revision=0&timeout=0').status_code == 401
    body, token = enrollment()
    enroll(client, body)
    auth = {'Authorization': 'Bearer ' + token}
    assert client.get('/api/client/policy/wait?after_revision=2&timeout=0', headers=auth).status_code == 409
    for query in ['after_revision=-1', 'timeout=26', 'timeout=-1']:
        assert client.get('/api/client/policy/wait?' + query, headers=auth).status_code == 422


def test_configured_service_key_is_opt_in_and_private(tmp_path, monkeypatch):
    monkeypatch.setenv('HEAD_PRODUCTION', '0')
    monkeypatch.setenv('HEAD_DATA_DIR', str(tmp_path / 'db'))
    monkeypatch.setenv('HEAD_PUBLIC_URL', 'https://head.test')
    path = tmp_path / 'hub-key'
    path.write_text(SERVICE_KEY + '\n')
    path.chmod(0o600)
    monkeypatch.setenv('HEAD_HUB_SERVICE_KEY_FILE', str(path))
    with TestClient(configured_app(), base_url='https://head.test') as c:
        assert c.get('/api/service/v1/users/' + SUBJECT, headers=SERVICE).status_code == 404
    path.chmod(0o644)
    with pytest.raises(ValueError):
        configured_app()
    path.chmod(0o600)
    link = tmp_path / 'link'
    link.symlink_to(path)
    monkeypatch.setenv('HEAD_HUB_SERVICE_KEY_FILE', str(link))
    with pytest.raises(ValueError):
        configured_app()
