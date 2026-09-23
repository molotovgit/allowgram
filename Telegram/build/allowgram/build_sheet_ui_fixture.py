"""Generate/build an isolated production-widget overlay; never regenerate the cached app.

Generate-only works without a toolchain. Build mode requires an owner-authorized
Windows MSVC environment and a compatible existing Ninja cache. No app is launched.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError("Fixture source anchor changed; review the overlay.")
    return text.replace(old, new)


def generate(root, output):
    source = root / "Telegram/SourceFiles"
    overlay = output / "SourceFiles"
    overlay.mkdir(parents=True, exist_ok=False)
    for relative in ("window/window_allowlist.h", "main/allowlist_sheet.h", "main/allowlist_sheet_resolver.h",
                     "test/allowlist_sheet_widget_probe.h", "test/allowlist_sheet_widget_test.inc",
                     "test/allowlist_sheet_widget_unlock.inc"):
        target = overlay / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((source / relative).read_bytes())
    files = {}
    widget = (source / "window/window_allowlist.cpp").read_text(encoding="utf-8")
    widget = widget.replace('#include "main/main_session.h"', '#include "main/main_session.h"\n#include "main/main_session_settings.h"\n#include "test/allowlist_sheet_widget_probe.h"')
    language = (root / "Telegram/Resources/langs/lang.strings").read_text(encoding="utf-8")
    aliases = []
    for key in ("resolving", "retry", "failed"):
        name = "lng_allowgram_sheet_" + key
        match = re.search(r'^"' + name + r'" = ("(?:[^"\\]|\\.)*");$', language, re.M)
        if not match:
            raise RuntimeError("Missing sheet language key.")
        literal = match.group(1)
        function = "SheetFixture" + key.title()
        aliases.append('QString ' + function + '(decltype(tr::now)) { return u' + literal + '_q; }\n'
                       'rpl::producer<QString> ' + function + '() { return rpl::single(u' + literal + '_q); }')
        widget = widget.replace("tr::" + name, function)
    widget = replace_once(widget, "namespace Window {", "\n".join(aliases) + "\n\nnamespace Window {")
    widget = replace_once(widget, '\tcrl::on_main(this, [=] { resolve(); });',
                          '\tcrl::on_main(this, [=] { resolve(); });\n\tQTimer::singleShot(0, this, [=] {\n#include "test/allowlist_sheet_widget_test.inc"\n\t});')
    widget = widget.replace("\t\t\tUnlockAllowlistWindows(session);", '\n#include "test/allowlist_sheet_widget_unlock.inc"\n\t\t\tUnlockAllowlistWindows(session);')
    widget = widget.replace("\n\tUnlockAllowlistWindows(session);", '\n#include "test/allowlist_sheet_widget_unlock.inc"\n\tUnlockAllowlistWindows(session);')
    files["window/window_allowlist.cpp"] = widget
    main = (source / "mainwindow.cpp").read_text(encoding="utf-8")
    main = '#include "test/allowlist_sheet_widget_probe.h"\n#include "main/main_account.h"\n#include "main/main_domain.h"\n#include "main/main_session.h"\n#include "main/main_session_settings.h"\n#include "data/data_user.h"\n' + main
    main = replace_once(main, '\t_intro = std::move(created);', '''\t_intro = std::move(created);
    QTimer::singleShot(500, this, [=] {
        SheetWidgetTest::Check(account().domain().accountsAuthedCount() == 0,
            "fresh synthetic profile has no authenticated account");
        const auto user = MTPUser(MTP_user(
            MTP_flags(MTPDuser::Flag::f_self | MTPDuser::Flag::f_first_name | MTPDuser::Flag::f_phone),
            MTP_long(99), MTPlong(), MTP_string("Synthetic"),
            MTPstring(), MTPstring(), MTP_string("998000000001"), MTPUserProfilePhoto(),
            MTPUserStatus(), MTPint(), MTPVector<MTPRestrictionReason>(),
            MTPstring(), MTPstring(), MTPEmojiStatus(), MTPVector<MTPUsername>(),
            MTPRecentStory(), MTPPeerColor(), MTPPeerColor(), MTPint(), MTPlong(),
            MTPlong(), MTPlong()));
        SheetWidgetTest::Check(account().createSession(user), "native synthetic session created");
        SheetWidgetTest::Check(_main && _main->isHidden() && _allowlistLock,
            "main window remains hidden behind onboarding lock");
        if (qEnvironmentVariable("ALLOWGRAM_SHEET_SCENE") == u"destroy-widget"_q) {
            QTimer::singleShot(30, this, [=] { _allowlistLock.destroy(); });
            QTimer::singleShot(400, this, [=] {
                SheetWidgetTest::Check(!account().session().allowlistConfigured() && _main->isHidden(),
                    "destroyed widget cancels lookup and never applies delayed policy");
                SheetWidgetTest::Finish();
            });
        }
        QTimer::singleShot(12000, QCoreApplication::instance(), [] {
            SheetWidgetTest::Check(false, "widget test timed out");
            SheetWidgetTest::Finish();
        });
    });''')
    start = main.index("void MainWindow::clearAllowlistLock() {")
    end = main.index("\n}", start)
    main = main[:end] + '''
    SheetWidgetTest::Check(!_allowlistLock && _main && !_main->isHidden(),
        "existing unlock callback reveals main UI after saved policy");
    SheetWidgetTest::Check(QCoreApplication::instance()->property("sheetSaved").toBool(),
        "main UI never unlocked before verified configuration");
    SheetWidgetTest::Finish();
''' + main[end:]
    files["mainwindow.cpp"] = main
    session = (source / "main/main_session.cpp").read_text(encoding="utf-8")
    session = '#include <QtCore/QCoreApplication>\n' + session
    session = replace_once(session, 'if (!local().writeSessionSettingsVerified(candidate.serialize())) {',
                           'if (qEnvironmentVariable("ALLOWGRAM_SHEET_SCENE") == u"save-failure"_q\n\t\t|| !local().writeSessionSettingsVerified(candidate.serialize())) {')
    files["main/main_session.cpp"] = session
    application = (source / "core/application.cpp").read_text(encoding="utf-8")
    application = application.replace('autoRegisterUrlScheme();', '').replace('Platform::NewVersionLaunched(old);', '')
    files["core/application.cpp"] = application
    for relative, function in (("mtproto/connection_tcp.cpp", "void TcpConnection::connectToServer("),
                               ("mtproto/connection_http.cpp", "void HttpConnection::connectToServer(")):
        text = (source / relative).read_text(encoding="utf-8")
        start = text.index("{", text.index(function)) + 1
        end = text.index("\n}", start)
        files[relative] = text[:start] + text[end:]
    instance = (source / "mtproto/mtp_instance.cpp").read_text(encoding="utf-8")
    instance = replace_once(instance, 'if (_requestFilter && !_requestFilter(request)) {', 'if (true) {')
    files["mtproto/mtp_instance.cpp"] = instance
    for relative in ("main/allowlist_sheet.cpp", "main/allowlist_sheet_resolver.cpp"):
        files[relative] = (source / relative).read_text(encoding="utf-8")
    for relative, text in files.items():
        target = overlay / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write(text)
    (output / "overlay.json").write_text(json.dumps({
        "sources": list(files), "mtprotoNetworkDisabled": True,
        "productionResolver": True, "productionWidget": True, "productionConfigureAllowlist": True,
        "saveFailureAtPersistenceBoundary": True,
        "localizedTextFromProductionStringsWithoutCacheRegeneration": True,
        "hashes": {str(path.relative_to(output)): hashlib.sha256(path.read_bytes()).hexdigest()
                   for path in overlay.rglob("*") if path.is_file()},
    }, indent=2) + "\n")
    return files


def build(cache_root, output, files, config):
    cache = cache_root / "out"
    original = cache / config / "Telegram.exe"
    private = tuple(re.findall(r'^TDESKTOP_API_(?:ID|HASH):STRING=(.+)$',
                              (cache / "CMakeCache.txt").read_text(), re.M))
    def diagnostic(data, name):
        text = data.decode(errors="replace")
        for value in private:
            text = text.replace(value, "[REDACTED]")
        (output / name).write_text(text[-262144:], encoding="utf-8")
    def fingerprints():
        selected = [original, cache / ".ninja_log", cache / ".ninja_deps"]
        cached_sources = set(files) - {"main/allowlist_sheet.cpp", "main/allowlist_sheet_resolver.cpp"}
        cached_sources.add("main/allowlist_policy.cpp")
        selected += [cache / "Telegram/CMakeFiles/Telegram.dir" / config / "SourceFiles" / (relative + ".obj")
                     for relative in cached_sources]
        selected += list((cache / "Telegram/CMakeFiles/Telegram.dir" / config).rglob("*.pch"))
        selected += list((cache / "Telegram/CMakeFiles/Telegram.dir" / config).rglob("*.pdb"))
        return {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in selected if path.is_file()}
    before = fingerprints()
    try:
        replacements = {}
        objects = []
        for index, relative in enumerate(files):
            new_module = relative in ("main/allowlist_sheet.cpp", "main/allowlist_sheet_resolver.cpp")
            template = "main/allowlist_policy.cpp" if new_module else relative
            target = "Telegram/CMakeFiles/Telegram.dir/" + config + "/SourceFiles/" + template + ".obj"
            command = subprocess.check_output(["ninja", "-f", "build-" + config + ".ninja", "-t", "commands", target], cwd=cache, text=True).splitlines()[-1]
            old_source = str(cache_root / "Telegram/SourceFiles" / template).replace("\\", "/")
            if old_source not in command:
                old_source = old_source.replace("/", "\\")
            if old_source not in command:
                raise RuntimeError("Cached compile command source did not match.")
            new_object = output / ("sheet-" + str(index) + ".obj")
            command = command.replace(old_source, str(output / "SourceFiles" / relative).replace("\\", "/"))
            old_object = target.replace("/", "\\")
            if old_object not in command:
                old_object = target
            if old_object not in command:
                raise RuntimeError("Cached compile object path did not match.")
            command = command.replace(old_object, str(new_object))
            command = re.sub(r'/Fd(?:"[^"]*"|\S+)', lambda _: '/Fd"' + str(output / "compiler.pdb") + '"', command)
            compiler = re.search(r'(?:"[^"\n]*cl.exe"|\S*cl.exe)', command, re.I)
            if not compiler:
                raise RuntimeError("Expected MSVC compiler command.")
            command = command[:compiler.end()] + ' /I"' + str(output / "SourceFiles") + '" ' + command[compiler.end():]
            command = command.replace("/showIncludes", "")
            if re.search(r"(?:^|\s)/Yc|(?:^|\s)/FA|(?:^|\s)/FR", command, re.I):
                raise RuntimeError("Unsupported compiler side output; review recipe before compiling.")
            command = re.sub(r'/sourceDependencies\s+(?:"[^"]*"|\S+)',
                             lambda _: '/sourceDependencies "' + str(output / ("dependencies-" + str(index) + ".json")) + '"', command)
            run = subprocess.run(command, shell=True, cwd=cache, capture_output=True)
            diagnostic(run.stdout + run.stderr, "compile-" + str(index) + ".log")
            print("Compiled fixture source " + relative + ": exit " + str(run.returncode), flush=True)
            if run.returncode:
                raise RuntimeError("Fixture compilation failed; see bounded sanitized compile log in fixture output.")
            objects.append(new_object)
            if not new_module:
                replacements[target] = new_object
        contents = (cache / "CMakeFiles" / ("impl-" + config + ".ninja")).read_text()
        anchor = "build " + config + "\\Telegram.exe:"
        start = contents.index(anchor)
        block = contents[start:contents.index("\n\n", start)]
        lines = block.splitlines()
        rule_and_inputs = re.findall(r"(?:\$.|[^ ])+", lines[0].split(": ", 1)[1])
        inputs = rule_and_inputs[1:]
        def ninja_path(path):
            return str(path).replace("\\", "/").replace("$", "$$").replace(":", "$:").replace(" ", "$ ")
        rewritten = []
        for entry in inputs:
            if entry == "||":
                break
            if entry == "|":
                rewritten.append(entry)
                continue
            normal = entry.replace("\\", "/").replace("$:", ":").replace("$ ", " ")
            path = replacements.get(normal)
            if path is None:
                path = Path(normal)
                if not path.is_absolute():
                    path = cache / path
            rewritten.append(ninja_path(path))
        additions = [path for path in objects if path not in replacements.values()]
        rewritten = [ninja_path(path) for path in additions] + rewritten
        executable = output / "Allowgram-Sheet-Test.exe"
        manifest = 'builddir = ' + ninja_path(output) + '\n\n'
        manifest += '''rule sheet_fixture_link
  command = link.exe /nologo @$RSP_FILE
  description = Linking isolated sheet widget fixture
  rspfile = $RSP_FILE
  rspfile_content = $in_newline $LINK_PATH $LINK_LIBRARIES $LINK_FLAGS /out:"$TARGET_FILE" /implib:"$TARGET_IMPLIB" /pdb:"$TARGET_PDB" /MANIFEST:EMBED,ID=1 /MANIFESTINPUT:"$SHEET_MANIFEST"

'''
        manifest += "build " + ninja_path(executable) + ": sheet_fixture_link " + " ".join(rewritten) + "\n"
        overrides = {"TARGET_FILE": str(executable), "TARGET_IMPLIB": str(output / "fixture.lib"),
                     "TARGET_PDB": str(output / "fixture.pdb"), "OBJECT_DIR": str(output),
                     "PRE_LINK": "cd .", "POST_BUILD": "cd .", "RSP_FILE": str(output / "link.rsp"),
                     "SHEET_MANIFEST": str(cache_root / "Telegram/Resources/winrc/Telegram.manifest")}
        seen = set()
        for line in lines[1:]:
            match = re.match(r"  (\w+) = (.*)", line)
            if match:
                seen.add(match[1])
            if match and match[1] in overrides:
                line = "  " + match[1] + " = " + overrides[match[1]].replace("\\", "/")
            manifest += line + "\n"
        for key, value in overrides.items():
            if key not in seen:
                manifest += "  " + key + " = " + value.replace("\\", "/") + "\n"
        (output / "link.ninja").write_text(manifest, encoding="utf-8")
        run = subprocess.run(["ninja", "-f", str(output / "link.ninja"), str(executable)], cwd=cache, capture_output=True)
        diagnostic(run.stdout + run.stderr, "link.log")
        if run.returncode:
            raise RuntimeError("Isolated fixture link failed; see bounded sanitized link log in fixture output.")
        (output / "build-evidence.json").write_text(json.dumps({
            "sheetFixture": True, "mtprotoNetworkDisabled": True,
            "fixtureExecutableSha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
            "cachedExecutableUnchanged": True,
            "objects": {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in objects},
        }, indent=2) + "\n")
    finally:
        after = fingerprints()
        unchanged = after == before
        (output / "cache-preservation.json").write_text(json.dumps({
            "unchanged": unchanged, "before": before, "after": after,
        }, indent=2) + "\n")
        if not unchanged:
            raise RuntimeError("Original cache preservation check failed.")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cache-repository", type=Path)
    parser.add_argument("--configuration", default="Debug", choices=("Debug", "Release"))
    parser.add_argument("--generate-only", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    files = generate(args.repository.resolve(), output)
    if not args.generate_only:
        if not args.cache_repository:
            parser.error("--cache-repository is required for compilation")
        build(args.cache_repository.resolve(), output, files, args.configuration)
    print("Sheet widget overlay generated" + (" and compiled." if not args.generate_only else "; native execution still required."))


if __name__ == "__main__":
    main()
