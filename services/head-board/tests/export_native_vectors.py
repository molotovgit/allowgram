"""Export public, synthetic interop vectors from the actual board signer."""

import argparse
import json
from pathlib import Path
from tempfile import TemporaryDirectory

from headboard.api import create_app
from headboard.signing import DOMAIN, Signer, b64, payload


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--subject", default="90001")
    parser.add_argument("--gui-peers", action="store_true")
    args = parser.parse_args()
    with TemporaryDirectory(prefix="agh-public-vectors-") as directory:
        app = create_app(Path(directory))
        signer = Signer(app.state.store)
        device = "a" * 32
        base = {
            "id": args.subject,
            "revision": 2,
            "updated_at": 1700000000,
            "peers": json.dumps(
                [{"id": "42", "kind": "chat"}, {"id": "42", "kind": "user"}]
                if args.gui_peers
                else [
                    {"id": "12345", "kind": "channel"},
                    {"id": "23456", "kind": "user"},
                ]
            ),
        }
        good = payload(base, device)

        def signed(raw):
            return {"signed": b64(raw), "signature": b64(signer.key.sign(DOMAIN + raw))}

        def altered(**changes):
            value = json.loads(good)
            value.update(changes)
            return signed(
                json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
            )

        vectors = [
            {
                "name": "valid backend policy",
                "expected": True,
                "count": 2,
                "envelope": signer.envelope(base, device),
            },
            {
                "name": "valid deny-all",
                "expected": True,
                "count": 0,
                "envelope": altered(revision=3, peers=[]),
            },
            {
                "name": "bad signature",
                "expected": False,
                "envelope": dict(
                    signer.envelope(base, device), signature=b64(bytes(64))
                ),
            },
            {
                "name": "wrong subject",
                "expected": False,
                "envelope": altered(telegram_user_id="90002"),
            },
            {
                "name": "wrong device",
                "expected": False,
                "envelope": altered(device_id="b" * 32),
            },
            {
                "name": "rollback",
                "expected": False,
                "floor": 2,
                "previous": b64(good),
                "envelope": altered(revision=1),
            },
            {
                "name": "same revision unchanged",
                "expected": True,
                "count": 2,
                "floor": 2,
                "previous": b64(good),
                "envelope": signer.envelope(base, device),
            },
            {
                "name": "same revision different bytes",
                "expected": False,
                "floor": 2,
                "previous": b64(good),
                "envelope": altered(peers=[]),
            },
            {
                "name": "zero revision",
                "expected": False,
                "envelope": altered(revision=0),
            },
            {
                "name": "boolean revision",
                "expected": False,
                "envelope": altered(revision=True),
            },
            {
                "name": "fraction revision",
                "expected": False,
                "envelope": altered(revision=2.5),
            },
            {"name": "unknown version", "expected": False, "envelope": altered(v=2)},
            {
                "name": "future issuance",
                "expected": False,
                "envelope": altered(issued_at=1700000301),
            },
            {
                "name": "invalid id",
                "expected": False,
                "envelope": altered(peers=[{"id": "0", "kind": "user"}]),
            },
            {
                "name": "huge id",
                "expected": False,
                "envelope": altered(peers=[{"id": "281474976710656", "kind": "user"}]),
            },
            {
                "name": "numeric id",
                "expected": False,
                "envelope": altered(peers=[{"id": 123, "kind": "user"}]),
            },
            {
                "name": "duplicate peer",
                "expected": False,
                "envelope": altered(peers=[{"id": "1", "kind": "user"}] * 2),
            },
            {
                "name": "unknown peer kind",
                "expected": False,
                "envelope": altered(peers=[{"id": "1", "kind": "alien"}]),
            },
            {
                "name": "extra field",
                "expected": False,
                "envelope": altered(unexpected="reject"),
            },
            {
                "name": "extra peer field",
                "expected": False,
                "envelope": altered(
                    peers=[{"id": "1", "kind": "user", "label": "reject"}]
                ),
            },
            {
                "name": "duplicate payload key",
                "expected": False,
                "envelope": signed(good[:-1] + b',"v":1}'),
            },
            {
                "name": "duplicate envelope key",
                "expected": False,
                "raw_envelope": '{"signed":"bad",'
                + json.dumps(signer.envelope(base, device), separators=(",", ":"))[1:],
            },
        ]
        invitation = {
            "v": 1,
            "url": "https://board.example",
            "telegram_user_id": args.subject,
            "code": "A" * 43,
            "public_key": signer.public,
        }
        data = {
            "public_key": signer.public,
            "subject": args.subject,
            "device": device,
            "now": 1700000000,
            "invitation": "AGH1."
            + b64(json.dumps(invitation, separators=(",", ":")).encode()),
            "enrollment": {
                "device_id": device,
                "device_token": "B" * 43,
                "policy": signer.envelope(base, device),
            },
            "vectors": vectors,
            "restored": altered(
                revision=4,
                peers=[{"id": "43", "kind": "chat"}, {"id": "43", "kind": "user"}],
            ),
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(data, indent=2) + "\n")
        print("Exported public synthetic vectors:", len(vectors))


if __name__ == "__main__":
    main()
