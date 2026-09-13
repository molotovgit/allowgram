from pathlib import Path
import hashlib
import json
import re
import subprocess
import argparse

parser = argparse.ArgumentParser(description="Build a disposable real-widget regression/capture executable from a configured Windows Ninja Release build. Run in its MSVC environment.")
parser.add_argument('--repository', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--hardening', action='store_true')
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
(fixture / 'application.cpp').write_text(application, encoding='utf-8')

extra_sources = []
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
    from call_fixture import instrument_calls, instrument_transport
    extra_sources.append(instrument_transport(root, json_includes))
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
    for name, relative, source in extra_sources:
        (fixture / (name + '.cpp')).write_text(source, encoding='utf-8')

(fixture / 'mainwindow.cpp').write_text(main, encoding='utf-8')
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
(fixture / 'build-evidence.json').write_text(json.dumps({
    'sourceCommit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
    'productionExecutableSha256': original_hash,
    'fixtureExecutableSha256': hashlib.sha256((fixture / 'Allowgram-Docs.exe').read_bytes()).hexdigest(),
    'fixtureInputs': {str(path.relative_to(fixture)): hashlib.sha256(path.read_bytes()).hexdigest()
                      for path in fixture.rglob('*') if path.suffix in ('.cpp', '.inc', '.h')},
    'layoutUnmodified': True,
    'hardeningFixture': args.hardening,
    'mtprotoNetworkDisabled': args.hardening,
    'callDeviceAndPanelSideEffectsSuppressed': args.hardening,
    'overlay': (['synthetic account/model construction', 'real composer callbacks',
                 'MTProto TCP/HTTP connection and request delivery disabled'] if args.hardening else
                ['unsigned-in construction', 'neutral scene values and parser-only validation'])
               + ['process-local style scale', 'real-widget regression include'],
}, indent=2) + '\n')
print('Fixture built; original Release binary unchanged.', flush=True)
