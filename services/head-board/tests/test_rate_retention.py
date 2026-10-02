from fastapi.testclient import TestClient
from headboard.api import create_app
import headboard.api as api


def test_expired_source_buckets_are_reclaimed_not_permanent_lockout(tmp_path, monkeypatch):
    monkeypatch.setattr(api, 'MAX_RATE_SOURCES', 2, raising=False)
    now = [100.0]
    monkeypatch.setattr(api, '_rate_now', lambda: now[0], raising=False)
    app = create_app(tmp_path, public_url='http://localhost')
    for ip in ['one', 'two']:
        with TestClient(app, base_url='http://localhost', client=(ip, 1000)) as client:
            assert client.get('/api/health').status_code == 200
    with TestClient(app, base_url='http://localhost', client=('three',1000)) as client:
        assert client.get('/api/health').status_code == 429
        now[0] = 161.0
        assert client.get('/api/health').status_code == 200
