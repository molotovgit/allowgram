"""Offline-only native picker fixture; never injected into the shipping target."""
from pathlib import Path

def instrument_picker(root, fixture):
    test = root / 'Telegram/SourceFiles/test'
    (fixture/'test').mkdir(exist_ok=True)
    for name in ('allowlist_picker_transport.inc','allowlist_picker_native.inc'):
        (fixture/'test'/name).write_bytes((test/name).read_bytes())
    qt='\n'.join('#include <QtCore/'+n+'>' for n in ('QCoreApplication','QTimer','QFile','QDir','QJsonDocument','QJsonArray','QJsonObject','QVariant','QEventLoop'))+'\n'
    main=(root/'Telegram/SourceFiles/mainwindow.cpp').read_text()
    includes='\n'.join('#include "'+n+'"' for n in ('main/main_account.h','main/main_session.h','main/main_session_settings.h','mtproto/mtproto_config.h','ui/widgets/checkbox.h'))+'\n'
    anchor='\t_intro = std::move(created);'
    boot=r'''
    QTimer::singleShot(1200, this, [=] {
        if (account().sessionExists()) return;
        QCoreApplication::instance()->setProperty("allowgramPickerScene", "error");
        const auto user = MTP_user(MTP_flags(MTPDuser::Flag::f_self | MTPDuser::Flag::f_first_name),
            MTP_long(99), MTPlong(), MTP_string("Synthetic picker account"),
            MTPstring(), MTPstring(), MTPstring(), MTPUserProfilePhoto(),
            MTPUserStatus(), MTPint(), MTPVector<MTPRestrictionReason>(),
            MTPstring(), MTPstring(), MTPEmojiStatus(), MTPVector<MTPUsername>(),
            MTPRecentStory(), MTPPeerColor(), MTPPeerColor(), MTPint(), MTPlong(),
            MTPlong(), MTPlong());
        if (!account().createSession(user)) { QCoreApplication::exit(3); return; }
        resize(qEnvironmentVariableIntValue("ALLOWGRAM_UI_WIDTH"),
            qEnvironmentVariableIntValue("ALLOWGRAM_UI_HEIGHT"));
        updateControlsGeometry();
    });
'''
    assert main.count(anchor)==1
    main=qt+includes+main.replace(anchor,anchor+boot)
    widget=(root/'Telegram/SourceFiles/window/window_allowlist.cpp').read_text()
    anchor='crl::on_main(this, [=] { load(); });'
    assert widget.count(anchor)==1
    widget=qt+'#include <QtCore/QPointer>\n#include "main/main_session_settings.h"\n'+widget.replace(anchor,anchor+'\nQTimer::singleShot(800, this, [=] {\n#include "test/allowlist_picker_native.inc"\n});')
    transport=(root/'Telegram/SourceFiles/mtproto/mtp_instance.cpp').read_text()
    anchor='if (_requestFilter && !_requestFilter(request)) {'
    assert transport.count(anchor)==1
    transport=qt+transport.replace(anchor,'\n#include "test/allowlist_picker_transport.inc"\nif (true) {')
    storage=(root/'Telegram/SourceFiles/storage/storage_account.cpp').read_text()
    anchor='bool Account::writeSessionSettingsVerified(const QByteArray &serialized) {'
    assert storage.count(anchor)==1
    storage=qt+storage.replace(anchor,anchor+'\nif (QCoreApplication::instance()->property("allowgramPickerSaveFail").toBool()) return false;')
    return main,widget,[('picker_transport','mtproto/mtp_instance.cpp',transport),('picker_storage','storage/storage_account.cpp',storage)]
