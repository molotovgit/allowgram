import json
import secrets
import time

import pytest
from fastapi.testclient import TestClient
from test_board import ORIGIN, headers, login
from test_registration import registration
from test_wire import decode

from headboard.api import create_app


def test_rejection_is_owner_only_final_and_cannot_leak_existing_policy(tmp_path):
    with TestClient(create_app(tmp_path), base_url=ORIGIN) as c:
        owner, _ = login(c, tmp_path)
        h = headers(owner)
        assert c.post('/api/users', json={'id':'90001','label':'Existing'}, headers=h).status_code == 201
        peers = [{'kind':'chat','id':'45678'}]
        assert c.put('/api/users/90001/policy', json={'expected_revision':0,'peers':peers}, headers=h).status_code == 200
        _, auth, body = registration()
        p = c.post('/api/client/register', json=body, headers=auth).json()
        route = '/api/registrations/' + p['id']
        assert 'policy' not in p
        assert c.post(route+'/approve', json={'fingerprint':p['fingerprint']}, headers=h).status_code == 200
        assert json.loads(decode(c.get('/api/client/registration',headers=auth).json()['policy']['signed']))['peers'] == peers
        assert c.get('/api/users/90001').json()['label'] == 'Existing'
        _, a2, b2 = registration('8683512953')
        p2 = c.post('/api/client/register', json=b2, headers=a2).json()
        route2 = '/api/registrations/' + p2['id']
        assert c.post('/api/heads', json={'id':'70001','label':'Test head'},headers=h).status_code == 201
        code = c.post('/api/heads/70001/access-code',json={},headers=h).json()['code']
        head = c.post('/api/auth/bootstrap',json={'code':code},headers={'Origin':ORIGIN}).json()
        for path, body in [(route2+'/approve',{'fingerprint':p2['fingerprint']}),(route2+'/reject',{})]:
            assert c.post(path,json=body,headers=headers(head)).status_code == 403
        assert c.get('/api/registrations').status_code == 403
        # A fresh owner session; role is never granted by registering the owner's ID.
        output = tmp_path/'owner-code.txt'
        output.unlink()
        owner,_ = login(c,tmp_path); h=headers(owner)
        assert c.post(route2+'/reject',json={},headers=h).status_code == 200
        assert c.post(route2+'/reject',json={},headers=h).status_code == 200
        assert c.get('/api/client/registration',headers=a2).json()['status'] == 'rejected'
        assert c.post('/api/client/register',json=b2,headers=a2).json()['status'] == 'rejected'
        assert c.post(route2+'/approve',json={'fingerprint':p2['fingerprint']},headers=h).status_code == 409
        assert len(c.get('/api/heads').json()) == 2


@pytest.mark.parametrize('change', [
    {'consent_version':0}, {'consent_version':True}, {'telegram_user_id':90001},
    {'telegram_user_id':'090001'}, {'telegram_user_id':'281474976710656'},
    {'role':'owner'}, {'peers':[]}, {'initial_peers':[{'kind':'unknown','id':'1'}]},
    {'initial_peers':[{'kind':'user','id':'1'},{'kind':'user','id':'1'}]},
])
def test_invalid_registration_cannot_create_records(tmp_path, change):
    app=create_app(tmp_path)
    with TestClient(app,base_url=ORIGIN) as c:
        _,auth,body=registration()
        assert c.post('/api/client/register',json={**body,**change},headers=auth).status_code == 422
        with app.state.store.db() as db:
            assert db.execute('SELECT count(*) FROM registrations').fetchone()[0] == 0


def test_registration_survives_restart_and_abuse_is_bounded(tmp_path,monkeypatch):
    import headboard.registration as reg
    monkeypatch.setattr(reg,'MAX_PENDING',1,raising=False)
    app=create_app(tmp_path)
    with TestClient(app,base_url=ORIGIN) as c:
        _,auth,body=registration()
        p=c.post('/api/client/register',headers=auth,json=body).json()
        _,other_auth,other_body=registration('90002')
        assert c.post('/api/client/register',headers=other_auth,json=other_body).status_code == 429
        assert c.post('/api/client/register',headers=auth,json=body).json() == p
    with TestClient(create_app(tmp_path),base_url=ORIGIN) as c:
        assert c.get('/api/client/registration',headers=auth).json() == p
        assert c.get('/api/client/registration',headers=other_auth).status_code == 401
        assert c.get('/api/client/registration',headers={'Authorization':'Bearer '+'x'*43}).status_code == 401
        with app.state.store.db() as db:
            db.execute('UPDATE registrations SET created_at=?',(int(time.time())-8*86400,))
        assert c.get('/api/client/registration',headers=auth).json()['status'] == 'rejected'
