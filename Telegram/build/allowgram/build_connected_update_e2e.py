"""Build isolated connected updater E2E Allowgram fixtures.

The script consumes an already configured Windows Ninja Release tree. It does
not rebuild or overwrite the production Release executables; it compiles a
small set of overlay translation units and relinks owned fixture binaries.
"""

from pathlib import Path
import argparse
import base64
import datetime
import hashlib
import json
import re
import subprocess


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument('--repository', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--trust', type=Path, required=True)
    parser.add_argument('--test-repository', type=Path)
    parser.add_argument('--fixture-exe-name', default='Allowgram.exe')
    parser.add_argument('--display-version', default='7.2.8.8')
    parser.add_argument('--sequence', type=int, default=8)
    parser.add_argument('--compile-only', action='store_true')
    return parser.parse_args()


args = parse_args()
root = args.repository.resolve()
build = root / 'out'
fixture = args.output.resolve()
trust = args.trust.resolve()
test_root = (args.test_repository or root).resolve()

if fixture.exists():
    raise RuntimeError(f'fixture output already exists: {fixture}')
fixture.mkdir(parents=True)

cache = (build / 'CMakeCache.txt').read_text()
private = tuple(re.findall(r'^TDESKTOP_API_(?:ID|HASH):STRING=(.+)$', cache, re.M))
assert len(private) == 2, 'Configured API values are needed for log redaction.'


def redact(text: str) -> str:
    for value in private:
        text = text.replace(value, '[REDACTED]')
    return text


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8', newline='\r\n')


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ninja_path(path: Path) -> str:
    return str(path).replace('\\', '/').replace(':', '$:')


def run(command, *, cwd: Path = build, shell: bool = False) -> None:
    completed = subprocess.run(
        command,
        cwd=cwd,
        shell=shell,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT)
    print(redact(completed.stdout.decode(errors='replace')), flush=True)
    if completed.returncode:
        raise SystemExit(completed.returncode)


def ninja_command(target: str) -> str:
    output = subprocess.check_output(
        ['ninja', '-f', 'build-Release.ninja', '-t', 'commands', target],
        cwd=build)
    return output.decode(errors='replace').splitlines()[-1]


def patch_sequence(command: str) -> str:
    patched = re.sub(
        r'([-/]D)TDESKTOP_ALLOWGRAM_UPDATE_SEQUENCE=\d+',
        rf'\1TDESKTOP_ALLOWGRAM_UPDATE_SEQUENCE={args.sequence}',
        command)
    if args.sequence != 8 and '/wd4651' not in patched:
        marker = ' /showIncludes'
        if marker in patched:
            patched = patched.replace(marker, ' /wd4651' + marker, 1)
        else:
            patched += ' /wd4651'
    return patched


def replace_existing(text: str, old: str, new: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'anchor matched {count} times: {old[:80]!r}')
    return text.replace(old, new)


def patch_resource_text(text: str) -> str:
    dotted = args.display_version
    comma = ','.join(dotted.split('.'))
    text = re.sub(r'FILEVERSION \d+,\d+,\d+,\d+', f'FILEVERSION {comma}', text)
    text = re.sub(
        r'PRODUCTVERSION \d+,\d+,\d+,\d+',
        f'PRODUCTVERSION {comma}',
        text)
    text = re.sub(
        r'VALUE "FileVersion", "\d+\.\d+\.\d+\.\d+"',
        f'VALUE "FileVersion", "{dotted}"',
        text)
    text = re.sub(
        r'VALUE "ProductVersion", "\d+\.\d+\.\d+\.\d+"',
        f'VALUE "ProductVersion", "{dotted}"',
        text)
    return text


def compile_resource(
        *,
        source: Path,
        target: str,
        output_name: str,
        absolute_icon: bool = False) -> tuple[str, str]:
    source_text = patch_resource_text(source.read_text(encoding='utf-8'))
    if absolute_icon:
        icon = (root / 'Telegram/Resources/art/allowgram/allowgram.ico')
        source_text = source_text.replace(
            '"..\\\\art\\\\allowgram\\\\allowgram.ico"',
            '"' + str(icon).replace('\\', '\\\\') + '"')
    fixture_source = fixture / (output_name + '.rc')
    fixture_object = fixture / (output_name + '.rc.res')
    write_text(fixture_source, source_text)

    command = ninja_command(target)
    source_forward = str(source).replace('\\', '/')
    source_back = str(source)
    if source_forward in command:
        command = command.replace(source_forward, str(fixture_source))
    elif source_back in command:
        command = command.replace(source_back, str(fixture_source))
    else:
        raise RuntimeError(redact(command[-600:]))

    old_object = target.replace('/', '\\')
    command = command.replace(old_object + '.d', str(fixture_object) + '.d')
    command = command.replace(old_object, str(fixture_object))
    command = patch_sequence(command)
    print(f'Compiling connected E2E resource: {output_name}', flush=True)
    run(command, shell=True)
    return old_object, str(fixture_object)


generation = subprocess.run(
    ['ninja', '-f', 'build-Release.ninja', 'Telegram/gen/lang_auto.timestamp'],
    cwd=build,
    capture_output=True,
    text=True)
print(redact(generation.stdout + generation.stderr), flush=True)
if generation.returncode:
    raise SystemExit(generation.returncode)

test_dir = fixture / 'test'
test_dir.mkdir()
for name in ('allowgram_connected_update_e2e_test.inc',):
    write_text(
        test_dir / name,
        (test_root / 'Telegram/SourceFiles/test' / name).read_text(
            encoding='utf-8'))

main = (root / 'Telegram/SourceFiles/mainwindow.cpp').read_text(encoding='utf-8')
main = replace_existing(main, '\t_intro = std::move(created);', '''\t_intro = std::move(created);
\tif (!account().sessionExists()) {
\t\t_allowlistLock.create(bodyWidget(), &controller());
\t\tresize(720, 1000);
\t\tupdateControlsGeometry();
\t\t_allowlistLock->show();
\t}
\tif (qEnvironmentVariableIsSet("ALLOWGRAM_CONNECTED_E2E_REPORT")) {
\t\tQTimer::singleShot(1800, this, [=] {
#include "test/allowgram_connected_update_e2e_test.inc"
\t\t});
\t}''')
main_includes = [
    'main/main_session_settings.h', 'main/main_account.h', 'main/main_session.h',
    'mtproto/mtproto_config.h',
    'storage/file_upload.h', 'storage/localimageloader.h',
    'data/data_download_manager.h', 'data/data_document.h', 'data/data_session.h',
    'data/data_user.h',
    'history/history.h', 'core/update_channel.h', 'core/update_checker.h',
    'core/version.h', 'ui/widgets/buttons.h', 'ui/layers/generic_box.h',
    'ui/layers/box_layer_widget.h',
]
qt_includes = '\n'.join('#include <QtCore/' + name + '>' for name in (
    'QCoreApplication', 'QDir', 'QFile', 'QFileInfo', 'QJsonArray',
    'QJsonDocument', 'QJsonObject', 'QTimer', 'QVariant'))
main = (qt_includes
        + '\n#include <QtWidgets/QApplication>\n#include <memory>\n'
        + '\n'.join('#include "' + name + '"' for name in main_includes)
        + '\n'
        + main)

application = (
    root / 'Telegram/SourceFiles/core/application.cpp'
).read_text(encoding='utf-8')
application = application.replace('autoRegisterUrlScheme();', '')
application = application.replace('Platform::NewVersionLaunched(old);', '')
tail = '\tTest::Fire(u"launch_finished"_q);'
application = replace_existing(application, tail, '''\tif (qEnvironmentVariableIsSet("ALLOWGRAM_CONNECTED_E2E_RELAUNCH_REPORT")
\t\t&& qEnvironmentVariableIntValue("ALLOWGRAM_CONNECTED_E2E_RELAUNCH_SEQUENCE")
\t\t\t== int(Core::BuildAllowgramSequence)) {
\t\tauto arguments = QJsonArray();
\t\tfor (const auto &argument : QCoreApplication::arguments()) {
\t\t\targuments.push_back(argument);
\t\t}
\t\tauto report = QFile(qEnvironmentVariable(
\t\t\t"ALLOWGRAM_CONNECTED_E2E_RELAUNCH_REPORT"));
\t\tif (report.open(QIODevice::WriteOnly)) {
\t\t\treport.write(QJsonDocument(QJsonObject{
\t\t\t\t{ "arguments", arguments },
\t\t\t\t{ "runningUpdateVersion",
\t\t\t\t\tQString::number(Core::RunningUpdateVersion()) },
\t\t\t\t{ "buildAllowgramSequence",
\t\t\t\t\tdouble(Core::BuildAllowgramSequence) },
\t\t\t\t{ "appVersion", double(AppVersion) },
\t\t\t\t{ "appVersionStr", QString::fromLatin1(AppVersionStr) },
\t\t\t}).toJson());
\t\t}
\t\tQTimer::singleShot(qEnvironmentVariableIntValue(
\t\t\t"ALLOWGRAM_CONNECTED_E2E_RELAUNCH_HOLD_MS"), [] {
\t\t\tQCoreApplication::quit();
\t\t});
\t}
''' + tail)
application = ('#include <QtCore/QCoreApplication>\n'
               '#include <QtCore/QFile>\n'
               '#include <QtCore/QJsonArray>\n'
               '#include <QtCore/QJsonDocument>\n'
               '#include <QtCore/QJsonObject>\n'
               '#include <QtCore/QTimer>\n'
               + application)

dialogs = (
    root / 'Telegram/SourceFiles/dialogs/dialogs_widget.cpp'
).read_text(encoding='utf-8')
button_anchor = '''\t\t_updateTelegram->setClickedCallback([] {
\t\t\tif (!Core::RestartToUpdate()) {
\t\t\t\treturn;
\t\t\t}
\t\t});'''
button_hook = button_anchor + '''
\t\tif (qEnvironmentVariableIsSet("ALLOWGRAM_CONNECTED_E2E_REPORT")) {
\t\t\tauto attempts = std::make_shared<int>(0);
\t\t\tauto attempt = std::make_shared<Fn<void()>>();
\t\t\t*attempt = [=] {
\t\t\t\tconst auto app = QCoreApplication::instance();
\t\t\t\tapp->setProperty(
\t\t\t\t\t"allowgramConnectedUpdateButtonVisible",
\t\t\t\t\t_updateTelegram && _updateTelegram->isVisible());
\t\t\t\tif (!_updateTelegram || _updateTelegram->isHidden()) {
\t\t\t\t\treturn;
\t\t\t\t}
\t\t\t\tif (!app->property(
\t\t\t\t\t\t"allowgramConnectedUpdateClickPermit").toBool()) {
\t\t\t\t\tif (++*attempts < 240) {
\t\t\t\t\t\tQTimer::singleShot(100, this, [=] {
\t\t\t\t\t\t\t(*attempt)();
\t\t\t\t\t\t});
\t\t\t\t\t}
\t\t\t\t\treturn;
\t\t\t\t}
\t\t\t\tif (app->property(
\t\t\t\t\t\t"allowgramConnectedUpdateButtonClicked").toBool()) {
\t\t\t\t\treturn;
\t\t\t\t}
\t\t\t\tapp->setProperty(
\t\t\t\t\t"allowgramConnectedUpdateButtonClicked",
\t\t\t\t\ttrue);
\t\t\t\t_updateTelegram->clicked({}, Qt::LeftButton);
\t\t\t};
\t\t\tQTimer::singleShot(100, this, [=] {
\t\t\t\t(*attempt)();
\t\t\t});
\t\t}'''
dialogs = replace_existing(dialogs, button_anchor, button_hook)
dialogs = ('#include <QtCore/QCoreApplication>\n'
           '#include <QtCore/QTimer>\n'
           '#include <memory>\n'
           + dialogs)

update_checker = (
    root / 'Telegram/SourceFiles/core/update_checker.cpp'
).read_text(encoding='utf-8')
url_guard = '''\tif (!url.isValid()
\t\t|| url.scheme() != QStringLiteral("https")
\t\t|| url.host() != QStringLiteral("github.com")) {
\t\tLOG(("Update Error: Bad Allowgram release feed URL."));
\t\tcrl::on_main(this, [=] { fail(); });
\t\treturn;
\t}'''
fixture_guard = '''\tconst auto connectedFixture = qEnvironmentVariableIsSet(
\t\t"ALLOWGRAM_CONNECTED_UPDATE_FEED_URL");
\tconst auto localFixtureUrl = connectedFixture
\t\t&& url.scheme() == QStringLiteral("http")
\t\t&& (url.host() == QStringLiteral("127.0.0.1")
\t\t\t|| url.host() == QStringLiteral("localhost"));
\tconst auto productionUrl = !connectedFixture
\t\t&& url.scheme() == QStringLiteral("https")
\t\t&& url.host() == QStringLiteral("github.com");
\tif (!url.isValid() || (!localFixtureUrl && !productionUrl)) {
\t\tLOG(("Update Error: Bad Allowgram release feed URL."));
\t\tcrl::on_main(this, [=] { fail(); });
\t\treturn;
\t}'''
update_checker = replace_existing(update_checker, url_guard, fixture_guard)

update_feed = (
    root / 'Telegram/SourceFiles/core/update_feed.cpp'
).read_text(encoding='utf-8')
update_feed = replace_existing(
    update_feed,
    '#include <QtCore/QRegularExpression>',
    '#include <QtCore/QRegularExpression>\n#include <QtCore/QUrl>')
update_feed = replace_existing(update_feed, '''QString StableReleaseFeedUrl() {
\treturn QStringLiteral(
\t\t"https://github.com/molotovgit/allowgram/releases/latest/download/%1"
\t).arg(QString::fromLatin1(kFeedAsset));
}''', '''QString StableReleaseFeedUrl() {
\tconst auto fixture = qEnvironmentVariable(
\t\t"ALLOWGRAM_CONNECTED_UPDATE_FEED_URL");
\treturn !fixture.isEmpty() ? fixture : QString();
}''')
update_feed = replace_existing(update_feed, '''QString StableReleaseDownloadUrl(
\t\tconst QString &tag,
\t\tconst QString &fileName) {
\treturn QStringLiteral(
\t\t"https://github.com/molotovgit/allowgram/releases/download/%1/%2"
\t).arg(tag, fileName);
}''', '''QString StableReleaseDownloadUrl(
\t\tconst QString &tag,
\t\tconst QString &fileName) {
\tconst auto base = qEnvironmentVariable(
\t\t"ALLOWGRAM_CONNECTED_UPDATE_DOWNLOAD_BASE");
\tif (base.isEmpty()) {
\t\treturn QString();
\t}
\tauto url = QUrl(base);
\tif (!url.path().endsWith('/')) {
\t\turl.setPath(url.path() + '/');
\t}
\treturn url.resolved(QUrl(fileName)).toString();
}''')


def encoded_update_keys() -> str:
    root_pem = (trust / 'root-public.pem').read_bytes()
    manifest = (trust / 'manifest.min.json').read_bytes()
    manifest_sig = (trust / 'manifest.sig').read_bytes()

    def b64(data: bytes) -> str:
        return base64.b64encode(data).decode('ascii')

    return '''#include "core/update_keys.h"

namespace Core::Updates {
namespace {

QByteArray Decode(const char *value) {
\treturn QByteArray::fromBase64(QByteArray(value));
}

} // namespace

QByteArray RootPublicKeyPem() {
\treturn Decode("''' + b64(root_pem) + '''");
}

QByteArray EmbeddedManifest() {
\treturn Decode("''' + b64(manifest) + '''");
}

QByteArray EmbeddedManifestSignature() {
\treturn Decode("''' + b64(manifest_sig) + '''");
}

} // namespace Core::Updates
'''


extra_sources = [
    ('dialogs_widget', 'dialogs/dialogs_widget.cpp', dialogs),
    ('update_checker', 'core/update_checker.cpp', update_checker),
    ('update_feed', 'core/update_feed.cpp', update_feed),
    ('update_keys', 'core/update_keys.cpp', encoded_update_keys()),
]
offline_transport_sources = []
for name, relative, function in (
        ('connection_tcp', 'mtproto/connection_tcp.cpp',
         'void TcpConnection::connectToServer('),
        ('connection_http', 'mtproto/connection_http.cpp',
         'void HttpConnection::connectToServer(')):
    source = (
        root / 'Telegram/SourceFiles' / relative
    ).read_text(encoding='utf-8')
    start = source.index(function)
    opening = source.index('{', start)
    end = source.index('\n}', opening)
    source = source[:opening + 1] + source[end:]
    extra_sources.append((name, relative, source))
    offline_transport_sources.append(relative)

instance = (
    root / 'Telegram/SourceFiles/mtproto/mtp_instance.cpp'
).read_text(encoding='utf-8')
instance = replace_existing(
    instance,
    'if (_requestFilter && !_requestFilter(request)) {',
    'if (true) {')
extra_sources.append(('mtp_instance', 'mtproto/mtp_instance.cpp', instance))
offline_request_sources = ['mtproto/mtp_instance.cpp']

write_text(fixture / 'mainwindow.cpp', main)
write_text(fixture / 'application.cpp', application)
for name, relative, source in extra_sources:
    write_text(fixture / (name + '.cpp'), source)

executable = root / 'out/Release/Telegram.exe'
updater = root / 'out/Release/Updater.exe'
original_hash = sha256(executable)
original_updater_hash = sha256(updater)
replacements = []


def compile_source(name: str, relative: str) -> None:
    target = 'Telegram/CMakeFiles/Telegram.dir/Release/SourceFiles/' + relative + '.obj'
    command = ninja_command(target)
    old_source = str(root / 'Telegram/SourceFiles' / relative).replace('\\', '/')
    if old_source not in command:
        old_source = old_source.replace('/', '\\')
    if old_source not in command:
        raise RuntimeError(redact(command[-600:]))
    command = command.replace(
        old_source,
        str(fixture / (name + '.cpp')).replace('\\', '/'))
    old_object = target.replace('/', '\\')
    new_object = str(fixture / (name + '.obj'))
    if old_object not in command:
        raise RuntimeError(redact(command[-600:]))
    command = command.replace(old_object, new_object)
    command = patch_sequence(command)
    command = command.replace('/showIncludes', '')
    replacements.append((old_object, new_object))
    print(f'Compiling connected E2E fixture object: {name}', flush=True)
    run(command, shell=True)


for name, relative in [
        ('mainwindow', 'mainwindow.cpp'),
        ('application', 'core/application.cpp'),
        *[(name, relative) for name, relative, source in extra_sources]]:
    compile_source(name, relative)

telegram_resource = compile_resource(
    source=root / 'Telegram/Resources/winrc/Telegram.rc',
    target='Telegram/CMakeFiles/Telegram.dir/Release/Resources/winrc/Telegram.rc.res',
    output_name='Telegram',
    absolute_icon=True)
replacements.append(telegram_resource)

contents = (build / 'CMakeFiles/impl-Release.ninja').read_text()
begin = contents.index('build Release\\Telegram.exe:')
end = contents.index('\n\n', begin)
block = contents[begin:end]
for old, new in replacements:
    if old not in block:
        raise RuntimeError(f'missing link input {old}')
    block = block.replace(old, ninja_path(Path(new)))
fixture_exe = fixture / args.fixture_exe_name
block = block.replace(
    'build Release\\Telegram.exe:',
    'build ' + ninja_path(fixture_exe) + ':')
block = re.sub(
    r'^  TARGET_FILE = .*$', '  TARGET_FILE = ' + str(fixture_exe).replace('\\', '/'), block, flags=re.M)
block = re.sub(
    r'^  TARGET_IMPLIB = .*$', '  TARGET_IMPLIB = ' + str(fixture / 'Allowgram.lib').replace('\\', '/'), block, flags=re.M)
block = re.sub(
    r'^  TARGET_PDB = .*$', '  TARGET_PDB = ' + str(fixture / 'Allowgram.pdb').replace('\\', '/'), block, flags=re.M)
block = re.sub(
    r'^  OBJECT_DIR = .*$', '  OBJECT_DIR = ' + str(fixture).replace('\\', '/'), block, flags=re.M)
write_text(fixture / 'link.ninja', 'include build-Release.ninja\n\n' + block + '\n')

updater_resource = compile_resource(
    source=root / 'Telegram/Resources/winrc/Updater.rc',
    target='Telegram/CMakeFiles/Updater.dir/Release/Resources/winrc/Updater.rc.res',
    output_name='Updater')
updater_begin = contents.index('build Release\\Updater.exe:')
updater_end = contents.index('\n\n', updater_begin)
updater_block = contents[updater_begin:updater_end]
old, new = updater_resource
if old not in updater_block:
    raise RuntimeError(f'missing updater link input {old}')
updater_block = updater_block.replace(old, ninja_path(Path(new)))
fixture_updater = fixture / 'AllowgramUpdater.exe'
updater_block = updater_block.replace(
    'build Release\\Updater.exe:',
    'build ' + ninja_path(fixture_updater) + ':')
updater_block = re.sub(
    r'^  TARGET_FILE = .*$', '  TARGET_FILE = ' + str(fixture_updater).replace('\\', '/'), updater_block, flags=re.M)
updater_block = re.sub(
    r'^  TARGET_IMPLIB = .*$', '  TARGET_IMPLIB = ' + str(fixture / 'AllowgramUpdater.lib').replace('\\', '/'), updater_block, flags=re.M)
updater_block = re.sub(
    r'^  TARGET_PDB = .*$', '  TARGET_PDB = ' + str(fixture / 'AllowgramUpdater.pdb').replace('\\', '/'), updater_block, flags=re.M)
updater_block = re.sub(
    r'^  OBJECT_DIR = .*$', '  OBJECT_DIR = ' + str(fixture).replace('\\', '/'), updater_block, flags=re.M)
write_text(
    fixture / 'updater-link.ninja',
    'include build-Release.ninja\n\n' + updater_block + '\n')

compile_inputs = {
    str(path.relative_to(fixture)): sha256(path)
    for path in fixture.rglob('*')
    if path.suffix in ('.cpp', '.inc', '.h', '.obj', '.ninja', '.res', '.rc')
}
write_text(fixture / 'compile-evidence.json', json.dumps({
    'sourceCommit': subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
    'sourceDirty': subprocess.check_output(
        ['git', 'status', '--porcelain'], cwd=root, text=True).splitlines(),
    'testCommit': subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=test_root, text=True).strip(),
    'testDirty': subprocess.check_output(
        ['git', 'status', '--porcelain'], cwd=test_root, text=True).splitlines(),
    'displayVersion': args.display_version,
    'allowgramSequence': args.sequence,
    'fixtureExeName': args.fixture_exe_name,
    'productionExecutableSha256': original_hash,
    'productionUpdaterSha256': original_updater_hash,
    'mtprotoNetworkDisabled': True,
    'offlineTransportSources': offline_transport_sources,
    'offlineRequestSources': offline_request_sources,
    'inputs': compile_inputs,
}, indent=2) + '\n')
if args.compile_only:
    raise SystemExit(0)

print('Linking connected E2E client fixture.', flush=True)
run([
    'ninja',
    '-j',
    '4',
    '-f',
    str(fixture / 'link.ninja'),
    str(fixture_exe).replace('\\', '/'),
], cwd=build)
if sha256(executable) != original_hash:
    raise RuntimeError('Production executable changed.')
if not fixture_exe.is_file():
    raise RuntimeError(f'missing fixture executable: {fixture_exe}')

print('Linking connected E2E updater fixture.', flush=True)
run([
    'ninja',
    '-j',
    '4',
    '-f',
    str(fixture / 'updater-link.ninja'),
    str(fixture_updater).replace('\\', '/'),
], cwd=build)
if sha256(updater) != original_updater_hash:
    raise RuntimeError('Production updater changed.')
if not fixture_updater.is_file():
    raise RuntimeError(f'missing fixture updater: {fixture_updater}')

linked_objects = {
    path.name: {
        'sha256': sha256(path),
        'modifiedUtc': datetime.datetime.fromtimestamp(
            path.stat().st_mtime,
            datetime.timezone.utc).isoformat(),
    }
    for path in sorted(fixture.glob('*.obj'))
}
write_text(fixture / 'build-evidence.json', json.dumps({
    'sourceCommit': subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
    'sourceDirty': subprocess.check_output(
        ['git', 'status', '--porcelain'], cwd=root, text=True).splitlines(),
    'compileEvidenceSha256': sha256(fixture / 'compile-evidence.json'),
    'displayVersion': args.display_version,
    'allowgramSequence': args.sequence,
    'fixtureExeName': args.fixture_exe_name,
    'productionExecutableSha256': original_hash,
    'productionUpdaterSha256': original_updater_hash,
    'fixtureExecutableSha256': sha256(fixture_exe),
    'fixtureUpdaterSha256': sha256(fixture_updater),
    'fixtureInputs': {
        str(path.relative_to(fixture)): sha256(path)
        for path in fixture.rglob('*')
        if path.suffix in ('.cpp', '.inc', '.h', '.rc')
    },
    'fixtureLinkedObjects': linked_objects,
    'overlay': [
        'synthetic account/model construction for connected updater E2E',
        'fixture-only local updater feed and download transport',
        'real production updater verifier, stage, ready check and launcher',
        'real upload/download manager callbacks and confirmation boxes',
        'MTProto TCP/HTTP connection and fixture request delivery disabled',
        'real Allowgram updater helper linked from production helper target',
    ],
}, indent=2) + '\n')
print('Connected E2E fixtures built; original Release binaries unchanged.', flush=True)
