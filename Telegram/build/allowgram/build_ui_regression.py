from pathlib import Path
import base64
import hashlib
import json
import re
import subprocess
import argparse
import datetime

parser = argparse.ArgumentParser(description="Build a disposable real-widget regression/capture executable from a configured Windows Ninja Release build. Run in its MSVC environment.")
parser.add_argument('--repository', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--hardening', action='store_true')
parser.add_argument('--optional-update', action='store_true')
parser.add_argument('--optional-update-restart', action='store_true')
parser.add_argument('--optional-update-trust', type=Path)
parser.add_argument('--test-repository', type=Path)
parser.add_argument('--compile-only', action='store_true')
args = parser.parse_args()
root = args.repository.resolve()
build = root / 'out'
fixture = args.output.resolve()
fixture.mkdir(parents=True, exist_ok=True)
cache = (build / 'CMakeCache.txt').read_text()
private = tuple(re.findall(r'^TDESKTOP_API_(?:ID|HASH):STRING=(.+)$', cache, re.M))
assert len(private) == 2, 'Configured API values are needed for log redaction.'


def redact(text):
    for value in private:
        text = text.replace(value, '[REDACTED]')
    return text


generation = subprocess.run(['ninja', '-f', 'build-Release.ninja',
                             'Telegram/gen/lang_auto.timestamp'], cwd=build,
                            capture_output=True, text=True)
print(redact(generation.stdout + generation.stderr), flush=True)
if generation.returncode:
    raise SystemExit(generation.returncode)


main = (root / 'Telegram/SourceFiles/mainwindow.cpp').read_text(encoding='utf-8')
old = '\t_intro = std::move(created);'
assert main.count(old) == 1
main = main.replace(old, old + '''
	// Disposable documentation fixture: production widget, no signed-in account.
	if (!account().sessionExists()) {
		_allowlistLock.create(bodyWidget(), &controller());
		resize(720, 1000);
		updateControlsGeometry();
		_allowlistLock->show();
	}
''')
widget = (root / 'Telegram/SourceFiles/window/window_allowlist.cpp').read_text(encoding='utf-8')
subset = (build / 'Telegram/gen/lang_subsets/window/window_allowlist.cpp.h').read_text()
session_subset = (build / 'Telegram/gen/lang_subsets/main/main_session.cpp.h').read_text()
extra = [line for line in session_subset.splitlines() if 'inline constexpr' in line and 'lng_allowgram_' in line and re.search(r'lng_allowgram_\w+', line).group() not in subset]
specializations = session_subset.split('namespace tr {', 1)[1].split('inline constexpr', 1)[0]
(fixture / 'fixture_lang.h').write_text('#pragma once\n#include "lang_auto.h"\nnamespace tr {\n' + specializations + '\n'.join(extra) + '\n}\n')
widget = widget.replace('#include "lang/lang_keys.h"', '#include "lang/lang_keys.h"\n#include "fixture_lang.h"')
old = '\taddRow(false, false);\n}'
assert widget.count(old) == 1
widget = widget.replace(old, '''	addRow(false, false);
	// Only neutral fixture entries; production layout and controls are unchanged.
	const auto scene = qEnvironmentVariable("ALLOWGRAM_DOCS_SCENE");
	if (scene == "multiple" || scene == "invalid") {
		_users.front()->field()->setTextWithTags({ u"8558994389"_q, {} });
		addRow(true, false);
		_users.back()->field()->setTextWithTags({ u"123456789"_q, {} });
		_groups.front()->field()->setTextWithTags({ u"chat:123456789"_q, {} });
		addRow(false, false);
		_groups.back()->field()->setTextWithTags({ u"channel:1234567890"_q, {} });
	}
	if (scene == "invalid") {
		_users.front()->field()->setTextWithTags({ u"@example_bot"_q, {} });
		crl::on_main(this, [=] { this->submit(); });
	}
}''')
session_text = (root / 'Telegram/SourceFiles/main/main_session.cpp').read_text(encoding='utf-8')
start = session_text.index('\tconst auto parsed = Allowlist::Parse(', session_text.index('QString Session::configureAllowlist('))
end = session_text.index('\tauto peers = base::flat_set<PeerId>();', start)
validation = session_text[start:end]
validation = validation.replace('Allowlist::', 'Main::Allowlist::').replace('userIds.toStdString()', 'CollectIds(_users).toStdString()').replace('groupIds.toStdString()', 'CollectIds(_groups).toStdString()')
# Preserve the production parser and error mapping; no account or save simulation.
validation = '\t\tconst auto error = [&]() -> QString {\n' + validation + '\t\treturn QString();\n\t\t}();\n\t\tif (!error.isEmpty()) { showError(error); }\n'
old = '\tif (!session) {\n\t\treturn;\n\t}'
assert widget.count(old) == 1
widget = widget.replace(old, '\tif (!session) {\n' + validation + '\t\treturn;\n\t}')

main = main.replace('resize(720, 1000);', 'QTimer::singleShot(800, this, [=] {\n\t\t\tresize(qEnvironmentVariableIntValue("ALLOWGRAM_UI_WIDTH"),\n\t\t\t\tqEnvironmentVariableIntValue("ALLOWGRAM_UI_HEIGHT"));\n\t\t\tupdateControlsGeometry();\n\t\t});')
main = '#include <QtCore/QTimer>\n' + main
if args.optional_update_restart:
    (fixture / 'test').mkdir(exist_ok=True)
    test_root = (args.test_repository or root).resolve()
    cascade_inc = 'allowgram_optional_restart_cascade_test.inc'
    (fixture / 'test' / cascade_inc).write_text(
        (test_root / 'Telegram/SourceFiles/test' / cascade_inc).read_text(encoding='utf-8'),
        encoding='utf-8')
    restart_includes = [
        'main/main_session_settings.h', 'main/main_account.h', 'main/main_session.h',
        'mtproto/mtproto_config.h',
        'storage/file_upload.h', 'storage/localimageloader.h',
        'data/data_download_manager.h', 'data/data_document.h', 'data/data_session.h',
        'data/data_user.h',
        'history/history.h', 'core/update_channel.h', 'core/update_checker.h',
        'ui/widgets/buttons.h', 'ui/layers/generic_box.h',
        'ui/layers/box_layer_widget.h',
    ]
    restart_qt = '\n'.join('#include <QtCore/' + name + '>' for name in (
        'QCoreApplication', 'QDir', 'QFile', 'QFileInfo', 'QJsonArray',
        'QStringList',
        'QJsonDocument', 'QJsonObject', 'QTimer'))
    main = (restart_qt + '\n#include <QtWidgets/QApplication>\n#include <QtWidgets/QWidget>\n#include <memory>\n'
            + '\n'.join('#include "' + name + '"' for name in restart_includes)
            + '\nnamespace Core { bool TestUnpackUpdateForOptionalRestartFixture(const QString &filepath); }\n'
            + main)
    anchor = '\t_intro = std::move(created);'
    assert main.count(anchor) == 1
    main = main.replace(anchor, anchor + '\n\tif (qEnvironmentVariableIsSet("ALLOWGRAM_OPTIONAL_RESTART_CASCADE_REPORT")) {\n\t\tQTimer::singleShot(1800, this, [=] {\n#include "test/allowgram_optional_restart_cascade_test.inc"\n\t\t});\n\t}')
widget = '#include <QtCore/QTimer>\n#include <QtCore/QFile>\n#include <QtCore/QJsonDocument>\n#include <QtCore/QJsonArray>\n#include <QtCore/QJsonObject>\n#include <QtGui/QTextDocument>\n#include <QtWidgets/QTextEdit>\n#include <cmath>\n' + widget
anchor = '\tconst auto scene = qEnvironmentVariable("ALLOWGRAM_DOCS_SCENE");'
assert widget.count(anchor) == 1
widget = widget.replace(anchor, anchor + '\n\tQTimer::singleShot(1200, this, [=] {\n#include "test/allowlist_layout_test.inc"\n\t});')
widget = widget.replace(anchor, anchor + '\n\tif (qEnvironmentVariableIntValue("ALLOWGRAM_UI_SELECT")) {\n\t\tQTimer::singleShot(950, this, [=] {\n\t\t\t_users.front()->field()->setFocusFast();\n\t\t\t_users.front()->field()->selectAll();\n\t\t});\n\t}')
application = (root / 'Telegram/SourceFiles/core/application.cpp').read_text(encoding='utf-8')
assert application.count('style::StartManager(cScale());') == 1
application = application.replace('style::StartManager(cScale());',
    'style::SetScale(qEnvironmentVariableIntValue("ALLOWGRAM_UI_SCALE"));\n'
    '\tstyle::StartManager(style::Scale());')
application = application.replace('autoRegisterUrlScheme();', '')
application = application.replace('Platform::NewVersionLaunched(old);', '')
if args.optional_update_restart:
    application = '#include <QtCore/QTimer>\n' + application
    tail = '\tTest::Fire(u"launch_finished"_q);'
    assert application.count(tail) == 1
    application = application.replace(tail,
        '\tif (qEnvironmentVariableIsSet("ALLOWGRAM_OPTIONAL_RESTART_REPORT")) {\n'
        '\t\tCore::UpdateChecker().test();\n'
        '\t\tQTimer::singleShot(300, [] { Core::Restart(); });\n'
        '\t}\n'
        + tail)
if args.optional_update:
    application = ('#include <QtCore/QDir>\n'
                   '#include <QtCore/QFile>\n'
                   '#include <QtCore/QFileInfo>\n'
                   '#include <QtCore/QJsonArray>\n'
                   '#include <QtCore/QJsonDocument>\n'
                   '#include <QtCore/QJsonObject>\n'
                   + application)
    seed = r'''
	const auto optionalStartupReport = qEnvironmentVariable("ALLOWGRAM_OPTIONAL_STALE_UPDATE_REPORT");
	auto optionalStartupSeeded = false;
	if (!optionalStartupReport.isEmpty()) {
		const auto optionalTargetVersion = QString::number(RunningUpdateVersion() + 1);
		const auto optionalNow = base::unixtime::now();
		const auto obsoleteStatePath = QDir(cWorkingDir()).filePath(
			u"tupdates/mandatory-state.json"_q);
		QDir().mkpath(QFileInfo(obsoleteStatePath).absolutePath());
		const auto obsoleteBytes = QJsonDocument(QJsonObject{
			{ "format", 1 },
			{ "active", true },
			{ "popup_dismissed", true },
			{ "apply_started", true },
			{ "first_seen", double(optionalNow - 600) },
			{ "deadline", double(optionalNow - 1) },
			{ "target", QJsonObject{
				{ "tag", u"obsolete-mandatory-startup-fixture"_q },
				{ "file", u"allowgram-update-stable-win-x64-7.2.8.9.tdup"_q },
				{ "sha256", QString(QByteArray(64, 'd')) },
				{ "size", 64. },
				{ "packed_version", optionalTargetVersion },
				{ "display", u"7.2.8.9"_q },
			} },
		}).toJson(QJsonDocument::Compact);
		auto obsoleteState = QFile(obsoleteStatePath);
		optionalStartupSeeded = obsoleteState.open(QIODevice::WriteOnly)
			&& obsoleteState.write(obsoleteBytes) == obsoleteBytes.size();
	}
	const auto writeOptionalStartupReport = [&](bool normalFlowContinued) {
		auto checks = QJsonArray();
		auto failures = 0;
		const auto check = [&](bool pass, const char *name) {
			checks.push_back(QJsonObject{
				{ "name", name },
				{ "pass", pass },
			});
			failures += pass ? 0 : 1;
		};
		check(optionalStartupSeeded,
			"obsolete mandatory state fixture writes startup policy state");
		check(normalFlowContinued,
			"obsolete mandatory state does not stop normal launch");
		check(_domain->started(),
			"obsolete mandatory state leaves the account domain available");
		auto report = QFile(optionalStartupReport);
		if (report.open(QIODevice::WriteOnly)) {
			report.write(QJsonDocument(QJsonObject{
				{ "checks", checks },
				{ "failures", failures },
				{ "finished", true },
				{ "normalFlowContinued", normalFlowContinued },
			}).toJson());
			report.close();
		}
	};
'''
    anchor = '\tconst auto domainStarted = startDomain();'
    assert application.count(anchor) == 1
    application = application.replace(anchor, seed + anchor)
    gate = '''\tif (!domainStarted) {\n\t\tTest::Fire(u"mandatory_update_gate"_q);\n\t\tDEBUG_LOG(("Application Info: mandatory update gate active."));\n\t\t_lastActivePrimaryWindow->finishFirstShow();\n\t\t_lastActivePrimaryWindow->updateIsActiveFocus();\n\t\treturn;\n\t}'''
    if application.count(gate) == 1:
        gated = '''\tif (!domainStarted) {\n\t\tTest::Fire(u"mandatory_update_gate"_q);\n\t\tDEBUG_LOG(("Application Info: mandatory update gate active."));\n\t\t_lastActivePrimaryWindow->finishFirstShow();\n\t\t_lastActivePrimaryWindow->updateIsActiveFocus();\n\t\tif (!optionalStartupReport.isEmpty()) {\n\t\t\twriteOptionalStartupReport(false);\n\t\t\tQTimer::singleShot(200, [] { QCoreApplication::quit(); });\n\t\t}\n\t\treturn;\n\t}'''
        application = application.replace(gate, gated)
    tail = '\tTest::Fire(u"launch_finished"_q);'
    assert application.count(tail) == 1
    application = application.replace(tail,
        '\tif (!optionalStartupReport.isEmpty()) {\n'
        '\t\twriteOptionalStartupReport(true);\n'
        '\t\tQTimer::singleShot(200, [] { QCoreApplication::quit(); });\n'
        '\t\treturn;\n'
        '\t}\n'
        + tail)
(fixture / 'application.cpp').write_text(application, encoding='utf-8')

extra_sources = []
offline_fixture = args.hardening or args.optional_update_restart
offline_transport_sources = []
offline_request_sources = []
if args.optional_update_trust:
    trust = args.optional_update_trust.resolve()
    root_pem = (trust / 'root-public.pem').read_bytes()
    manifest = (trust / 'manifest.min.json').read_bytes()
    manifest_sig = (trust / 'manifest.sig').read_bytes()
    def b64(data):
        return base64.b64encode(data).decode('ascii')
    update_keys = '''#include "core/update_keys.h"

namespace Core::Updates {
namespace {

QByteArray Decode(const char *value) {
	return QByteArray::fromBase64(QByteArray(value));
}

} // namespace

QByteArray RootPublicKeyPem() {
	return Decode("''' + b64(root_pem) + '''");
}

QByteArray EmbeddedManifest() {
	return Decode("''' + b64(manifest) + '''");
}

QByteArray EmbeddedManifestSignature() {
	return Decode("''' + b64(manifest_sig) + '''");
}

} // namespace Core::Updates
'''
    extra_sources.append(('update_keys', 'core/update_keys.cpp', update_keys))
if args.optional_update_restart:
    update_checker = (root / 'Telegram/SourceFiles/core/update_checker.cpp').read_text(encoding='utf-8')
    old = """void Updater::test() {
\t_testing = true;
\tcSetLastUpdateCheck(0);
\tstart(false);
}
"""
    new = """void Updater::test() {
\t_testing = true;
\tif (qEnvironmentVariableIsSet("ALLOWGRAM_OPTIONAL_RESTART_REPORT")
\t\t|| qEnvironmentVariableIsSet("ALLOWGRAM_OPTIONAL_RESTART_CASCADE_REPORT")) {
\t\thandleReady();
\t\treturn;
\t}
\tcSetLastUpdateCheck(0);
\tstart(false);
}
"""
    assert update_checker.count(old) == 1
    update_checker = update_checker.replace(old, new)
    bridge_anchor = """} // namespace

bool UpdaterDisabled() {"""
    bridge = """} // namespace

bool TestUnpackUpdateForOptionalRestartFixture(const QString &filepath) {
	return UnpackUpdate(filepath);
}

bool UpdaterDisabled() {"""
    assert update_checker.count(bridge_anchor) == 1
    update_checker = update_checker.replace(bridge_anchor, bridge)
    extra_sources.append(('update_checker', 'core/update_checker.cpp', update_checker))
    launcher = (root / 'Telegram/SourceFiles/platform/win/launcher_win.cpp').read_text(encoding='utf-8')
    launcher = ('#include <QtCore/QCoreApplication>\n'
                '#include <QtCore/QFile>\n'
                '#include <QtCore/QJsonDocument>\n'
                '#include <QtCore/QJsonObject>\n'
                '#include <QtCore/QTimer>\n'
                + launcher)
    old = """\tLogs::closeMain();
\tCrashReports::Finish();

\tconst auto hwnd = HWND(0);
"""
    new = """\tauto optionalReport = qEnvironmentVariable(
\t\t"ALLOWGRAM_OPTIONAL_RESTART_REPORT");
\tif (optionalReport.isEmpty()) {
\t\toptionalReport = qEnvironmentVariable(
\t\t\t"ALLOWGRAM_OPTIONAL_RESTART_LAUNCH_REPORT");
\t}
\tif (!optionalReport.isEmpty()) {
\t\tauto report = QFile(optionalReport);
\t\tif (report.open(QIODevice::WriteOnly)) {
\t\t\treport.write(QJsonDocument(QJsonObject{
\t\t\t\t{ "operation", operation },
\t\t\t\t{ "binaryPath", binaryPath },
\t\t\t\t{ "arguments", arguments },
\t\t\t\t{ "restarting", cRestarting() },
\t\t\t\t{ "restartingToSettings", cRestartingToSettings() },
\t\t\t\t{ "restartingUpdate", cRestartingUpdate() },
\t\t\t\t{ "readyStageHash", Core::ReadyUpdateStageHash() },
\t\t\t}).toJson(QJsonDocument::Compact));
\t\t}
\t\tQTimer::singleShot(0, [] { QCoreApplication::quit(); });
\t\treturn true;
\t}

\tLogs::closeMain();
\tCrashReports::Finish();

\tconst auto hwnd = HWND(0);
"""
    assert launcher.count(old) == 1
    launcher = launcher.replace(old, new)
    extra_sources.append(('launcher_win', 'platform/win/launcher_win.cpp', launcher))
if offline_fixture:
    for name, relative, function in (
        ('connection_tcp', 'mtproto/connection_tcp.cpp', 'void TcpConnection::connectToServer('),
        ('connection_http', 'mtproto/connection_http.cpp', 'void HttpConnection::connectToServer('),
    ):
        source = (root / 'Telegram/SourceFiles' / relative).read_text(encoding='utf-8')
        start = source.index(function)
        opening = source.index('{', start)
        end = source.index('\n}', opening)
        source = source[:opening + 1] + source[end:]
        extra_sources.append((name, relative, source))
        offline_transport_sources.append(relative)
if args.optional_update_restart and not args.hardening:
    instance = (root / 'Telegram/SourceFiles/mtproto/mtp_instance.cpp').read_text(encoding='utf-8')
    anchor = 'if (_requestFilter && !_requestFilter(request)) {'
    assert instance.count(anchor) == 1
    instance = instance.replace(anchor, 'if (true) {')
    extra_sources.append(('mtp_instance', 'mtproto/mtp_instance.cpp', instance))
    offline_request_sources.append('mtproto/mtp_instance.cpp')
if args.hardening:
    (fixture / 'test').mkdir(exist_ok=True)
    test_root = (args.test_repository or root).resolve()
    for name in ('allowgram_hardening_native_test.inc', 'allowgram_hardening_composer_test.inc', 'allowgram_calls_native_test.inc'):
        (fixture / 'test' / name).write_text((test_root / 'Telegram/SourceFiles/test' / name).read_text(encoding='utf-8'), encoding='utf-8')
    includes = [
        'main/main_account.h', 'main/main_domain.h', 'main/main_session.h',
        'main/main_session_settings.h', 'storage/storage_domain.h',
        'mtproto/mtproto_config.h', 'info/info_memento.h',
        'info/profile/info_profile_widget.h', 'data/data_session.h',
        'data/data_user.h', 'chat_helpers/message_field.h',
        'ui/chat/attach/attach_prepare.h',
        'api/api_common.h', 'apiwrap.h', 'history/history.h',
        'data/data_document.h', 'data/data_document_media.h',
        'chat_helpers/tabbed_section.h',
        'history/history_item.h', 'data/data_media_types.h', 'data/data_photo.h',
        'media/view/media_view_overlay_widget.h',
        'window/window_main_menu.h', 'ui/widgets/buttons.h',
        'data/data_emoji_statuses.h',
        'boxes/add_contact_box.h',
        'boxes/peers/prepare_short_info_box.h', 'boxes/peer_list_controllers.h',
        'info/settings/info_settings_widget.h', 'info/stories/info_stories_widget.h',
        'settings/sections/settings_main.h', 'base/unixtime.h',
        'core/update_channel.h', 'core/update_checker.h',
        'window/window_peer_menu.h', 'calls/calls_instance.h',
        'calls/calls_call.h', 'calls/calls_box_controller.h', 'calls/group/calls_group_common.h',
        'chat_helpers/compose/compose_show.h', 'dialogs/dialogs_key.h',
        'ui/widgets/popup_menu.h', 'ui/widgets/menu/menu_add_action_callback_factory.h',
    ]
    json_includes = '\n'.join('#include <QtCore/' + name + '>' for name in (
        'QTimer', 'QFile', 'QJsonDocument', 'QJsonArray', 'QJsonObject', 'QBuffer',
        'QCoreApplication', 'QVariant', 'QEventLoop'))
    main = (root / 'Telegram/SourceFiles/mainwindow.cpp').read_text(encoding='utf-8')
    main = json_includes + '\n' + '\n'.join('#include "' + name + '"' for name in includes) + '\n' + main
    anchor = '\t_intro = std::move(created);'
    assert main.count(anchor) == 1
    main = main.replace(anchor, anchor + '\n\tQTimer::singleShot(1500, this, [=] {\n#include "test/allowgram_hardening_native_test.inc"\n\t});')
    history = (root / 'Telegram/SourceFiles/history/history_widget.cpp').read_text(encoding='utf-8')
    anchor = 'void HistoryWidget::updateControlsVisibility() {'
    assert history.count(anchor) == 1
    history = history.replace(anchor, anchor + '''
    if (_history && !property("allowgramHardeningChecked").toBool()) {
        setProperty("allowgramHardeningChecked", true);
        QTimer::singleShot(500, this, [=] {
#include "test/allowgram_hardening_composer_test.inc"
        });
    }
''')
    extra_sources.append(('history_widget', 'history/history_widget.cpp', json_includes + '\n' + history))

    from call_fixture import instrument_calls, instrument_transport
    transport = instrument_transport(root, json_includes)
    extra_sources.append(transport)
    offline_request_sources.append(transport[1])
    extra_sources.extend(instrument_calls(root, json_includes))
    top_bar = (root / 'Telegram/SourceFiles/history/view/history_view_top_bar_widget.cpp').read_text(encoding='utf-8')
    start = top_bar.index('void TopBarWidget::updateControlsVisibility() {')
    end = top_bar.index('\n}', start)
    observation = ('\n\tQCoreApplication::instance()->setProperty(' + chr(34)
                   + 'allowgramCallButtonSeen' + chr(34) + ', true);'
                   + '\n\tQCoreApplication::instance()->setProperty(' + chr(34)
                   + 'allowgramCallButtonVisible' + chr(34) + ', !_call->isHidden());\n')
    top_bar = top_bar[:end] + observation + top_bar[end:]
    extra_sources.append(('top_bar', 'history/view/history_view_top_bar_widget.cpp', json_includes + '\n' + top_bar))
(fixture / 'mainwindow.cpp').write_text(main, encoding='utf-8')
for name, relative, source in extra_sources:
    (fixture / (name + '.cpp')).write_text(source, encoding='utf-8')
(fixture / 'window_allowlist.cpp').write_text(widget, encoding='utf-8')

executable = root / 'out/Release/Telegram.exe'
original_hash = hashlib.sha256(executable.read_bytes()).hexdigest()
replacements = []
for name, relative in [('mainwindow', 'mainwindow.cpp'), ('window_allowlist', 'window/window_allowlist.cpp'), ('application', 'core/application.cpp')] + [(name, relative) for name, relative, source in extra_sources]:
    target = 'Telegram/CMakeFiles/Telegram.dir/Release/SourceFiles/' + relative + '.obj'
    command = subprocess.check_output(['ninja', '-f', 'build-Release.ninja', '-t', 'commands', target], cwd=build).decode().splitlines()[-1]
    old_source = str(root / 'Telegram/SourceFiles' / relative).replace('\\', '/')
    if old_source not in command:
        old_source = old_source.replace('/', '\\')
    assert old_source in command, redact(command[-600:])
    command = command.replace(old_source, str(fixture / (name + '.cpp')).replace('\\', '/'))
    old_object = target.replace('/', '\\')
    assert old_object in command
    new_object = str(fixture / (name + '.obj'))
    command = command.replace(old_object, new_object)
    command = command.replace('/showIncludes', '')
    replacements.append((old_object, new_object))
    print('Compiling isolated production-widget fixture:', name, flush=True)
    result = subprocess.run(command, shell=True, cwd=build, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(redact(result.stdout.decode(errors='replace')), flush=True)
    if result.returncode:
        raise SystemExit(result.returncode)

contents = (build / 'CMakeFiles/impl-Release.ninja').read_text()
begin = contents.index('build Release\\Telegram.exe:')
end = contents.index('\n\n', begin)
block = contents[begin:end]
for old, new in replacements:
    assert old in block
    block = block.replace(old, new.replace('\\', '/').replace(':', '$:'))
target = str(fixture / 'Allowgram-Docs.exe').replace('\\', '/').replace(':', '$:')
block = block.replace('build Release\\Telegram.exe:', 'build ' + target + ':')
block = re.sub(r'^  TARGET_FILE = .*$', '  TARGET_FILE = ' + str(fixture / 'Allowgram-Docs.exe').replace('\\', '/'), block, flags=re.M)
block = re.sub(r'^  TARGET_IMPLIB = .*$', '  TARGET_IMPLIB = ' + str(fixture / 'Allowgram-Docs.lib').replace('\\', '/'), block, flags=re.M)
block = re.sub(r'^  TARGET_PDB = .*$', '  TARGET_PDB = ' + str(fixture / 'Allowgram-Docs.pdb').replace('\\', '/'), block, flags=re.M)
block = re.sub(r'^  OBJECT_DIR = .*$', '  OBJECT_DIR = ' + str(fixture).replace('\\', '/'), block, flags=re.M)
(fixture / 'link.ninja').write_text('include build-Release.ninja\n\n' + block + '\n', encoding='utf-8')
(fixture / 'compile-evidence.json').write_text(json.dumps({
    'sourceCommit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
    'sourceDirty': subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True).splitlines(),
    'testCommit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=(args.test_repository or root), text=True).strip(),
    'testDirty': subprocess.check_output(['git', 'status', '--porcelain'], cwd=(args.test_repository or root), text=True).splitlines(),
    'productionExecutableSha256': original_hash,
    'hardeningFixture': args.hardening,
    'optionalFixture': args.optional_update or args.optional_update_restart,
    'optionalRestartFixture': args.optional_update_restart,
    'mtprotoNetworkDisabled': bool(offline_transport_sources and offline_request_sources),
    'offlineTransportSources': offline_transport_sources,
    'offlineRequestSources': offline_request_sources,
    'inputs': {str(path.relative_to(fixture)): hashlib.sha256(path.read_bytes()).hexdigest()
               for path in fixture.rglob('*') if path.suffix in ('.cpp', '.inc', '.h', '.obj', '.ninja')},
}, indent=2) + '\n')
if args.compile_only:
    raise SystemExit(0)

print('Linking separate documentation fixture.', flush=True)
result = subprocess.run(['ninja', '-j', '4', '-f', str(fixture / 'link.ninja'), str(fixture / 'Allowgram-Docs.exe').replace('\\', '/')], cwd=build, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
print(redact(result.stdout.decode(errors='replace')), flush=True)
assert hashlib.sha256(executable.read_bytes()).hexdigest() == original_hash, 'Production executable changed.'
if result.returncode:
    raise SystemExit(result.returncode)
assert (fixture / 'Allowgram-Docs.exe').is_file()
offline_source_set = set(offline_transport_sources + offline_request_sources)
offline_linked_objects = {
    name + '.obj': {
        'source': relative,
        'sha256': hashlib.sha256((fixture / (name + '.obj')).read_bytes()).hexdigest(),
        'modifiedUtc': datetime.datetime.fromtimestamp(
            (fixture / (name + '.obj')).stat().st_mtime,
            datetime.timezone.utc).isoformat(),
    }
    for name, relative, source in extra_sources
    if relative in offline_source_set and (fixture / (name + '.obj')).is_file()
}
fixture_linked_objects = {
    path.name: {
        'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'modifiedUtc': datetime.datetime.fromtimestamp(
            path.stat().st_mtime,
            datetime.timezone.utc).isoformat(),
    }
    for path in sorted(fixture.glob('*.obj'))
}
if args.hardening:
    overlay = [
        'synthetic account/model construction',
        'real composer callbacks',
        'MTProto TCP/HTTP connection and fixture request delivery disabled',
        'synthetic private-call server replies after production request filtering',
        'synthetic key-exchange values; device, ring, panel and controller effects intercepted',
    ]
elif args.optional_update_restart:
    overlay = [
        'synthetic account/model construction for optional updater restart',
        'real upload/download manager callbacks and confirmation boxes',
        'MTProto TCP/HTTP connection and fixture request delivery disabled',
        'signed updater stage and launcher interception',
    ]
else:
    overlay = ['unsigned-in construction', 'neutral scene values and parser-only validation']
(fixture / 'build-evidence.json').write_text(json.dumps({
    'sourceCommit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
    'sourceDirty': subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True).splitlines(),
    'compileEvidenceSha256': hashlib.sha256((fixture / 'compile-evidence.json').read_bytes()).hexdigest(),
    'syntheticKeyExchange': args.hardening,
    'dependencyObjects': {
        relative: {
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'modifiedUtc': datetime.datetime.fromtimestamp(path.stat().st_mtime, datetime.timezone.utc).isoformat(),
        }
        for relative in ('main/main_session.cpp', 'main/main_session_settings.cpp',
                         'main/main_account.cpp', 'calls/calls_call.cpp',
                         'mtproto/allowlist_request_guard.cpp')
        for path in [build / 'Telegram/CMakeFiles/Telegram.dir/Release/SourceFiles' / (relative + '.obj')]
    },
    'productionExecutableSha256': original_hash,
    'fixtureExecutableSha256': hashlib.sha256((fixture / 'Allowgram-Docs.exe').read_bytes()).hexdigest(),
    'fixtureInputs': {str(path.relative_to(fixture)): hashlib.sha256(path.read_bytes()).hexdigest()
                      for path in fixture.rglob('*') if path.suffix in ('.cpp', '.inc', '.h')},
    'layoutUnmodified': True,
    'hardeningFixture': args.hardening,
    'optionalFixture': args.optional_update or args.optional_update_restart,
    'optionalRestartFixture': args.optional_update_restart,
    'mtprotoNetworkDisabled': bool(offline_transport_sources and offline_request_sources),
    'offlineTransportSources': offline_transport_sources,
    'offlineRequestSources': offline_request_sources,
    'offlineLinkedObjects': offline_linked_objects,
    'fixtureLinkedObjects': fixture_linked_objects,
    'callDeviceAndPanelSideEffectsSuppressed': args.hardening,
    'overlay': overlay
               + ((['optional updater stale-policy startup fixture'] if args.optional_update else [])
                   + (['optional updater direct-restart launcher fixture'] if args.optional_update_restart else [])
                   + ['process-local style scale', 'real-widget regression include']),
}, indent=2) + '\n')
print('Fixture built; original Release binary unchanged.', flush=True)
