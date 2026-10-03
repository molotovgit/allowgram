"""Opt-in offline managed-policy regression overlays; never linked into production."""
from pathlib import Path


def instrument_managed(root: Path, test_root: Path, fixture: Path,
                       main: str, sources: list) -> tuple[str, list]:
    tests = test_root / 'Telegram/SourceFiles/test'
    output = fixture / 'test'
    output.mkdir(exist_ok=True)
    for name in ('allowgram_managed_network_fixture.h', 'allowgram_managed_native_test.inc'):
        (output / name).write_bytes((tests / name).read_bytes())
    base = output / 'allowgram_hardening_native_test.inc'
    body = base.read_text(encoding='utf-8')
    anchor = 'auto report = QFile(qEnvironmentVariable("ALLOWGRAM_HARDENING_REPORT"));'
    assert body.count(anchor) == 1
    body = body.replace(anchor, '#include "test/allowgram_managed_native_test.inc"\n' + anchor)
    base.write_text(body, encoding='utf-8')
    main = ('#include "main/allowlist_managed_client.h"\n'
            '#include "test/allowgram_managed_network_fixture.h"\n'
            '#include <QtCore/QElapsedTimer>\n#include <QtCore/QThread>\n' + main)
    relative = 'main/allowlist_managed_client.cpp'
    client = (root / 'Telegram/SourceFiles' / relative).read_text(encoding='utf-8')
    anchor = 'new QNetworkAccessManager(this)'
    assert client.count(anchor) == 1
    client = '#include "test/allowgram_managed_network_fixture.h"\n' + client.replace(
        anchor, 'AllowgramManagedFixture::create(this)')
    return main, sources + [('managed_client', relative, client)]
