import argparse
import os
from pathlib import Path
import re
import sys


def release_commands(name, commands):
    if name == 'openssl3':
        commands, count = re.subn(
            r'(?m)^perl Configure[^\n]*debug-VC-[\s\S]*?(?=^perl Configure[^\n]* VC-)',
            '',
            commands,
        )
        if count != 1:
            raise RuntimeError('OpenSSL preparation changed; review release selection.')
    if name == 'tde2e':
        commands, count = re.subn(
            r'(?m)^mkdir Debug\n[\s\S]*?^mkdir Release\n',
            'mkdir Release\n',
            commands,
        )
        if count != 1:
            raise RuntimeError('TD E2E preparation changed; review release selection.')
    lines = []
    for line in commands.splitlines():
        if re.match(r'cmake --(?:build|install) .*--config Debug(?:\s|$)', line):
            continue
        if re.match(r'msbuild .*Configuration=Debug(?:\s|$)', line):
            continue
        if re.match(r'ninja -C out/Debug', line):
            continue
        if re.match(r'nmake .*CFG=debug-', line):
            continue
        if line.startswith('meson ') and re.search(r'debug|/Debug', line):
            continue
        if line.startswith('meson compile '):
            line = line.replace('meson compile ', 'meson compile -j2 ', 1)
        if line.startswith('ninja '):
            line = line.replace('ninja ', 'ninja -j2 ', 1)
        if line.startswith('msbuild -m '):
            line = line.replace('msbuild -m ', 'msbuild -m:2 ', 1)
        line = line.replace('jom -j%NUMBER_OF_PROCESSORS%', 'jom -j2')
        if line in (
            'bash --login ../patches/build_libvpx_win.sh',
            'bash --login ../patches/build_ffmpeg_win.sh',
        ):
            script = line.removeprefix('bash --login ')
            lines.append(
                'python -c "from pathlib import Path; '
                + f"source = Path('{script}'); "
                + "target = source.with_name('allowgram_' + source.name); "
                + "target.write_bytes(source.read_bytes().replace("
                + "b'NUMBER_OF_PROCESSORS', b'ALLOWGRAM_BUILD_JOBS'))\""
            )
            line = line.replace('/build_', '/allowgram_build_')
        line = line.replace(
            'TD_ENABLE_MULTI_PROCESSOR_COMPILATION=ON',
            'TD_ENABLE_MULTI_PROCESSOR_COMPILATION=OFF',
        )
        if name.startswith('qt_'):
            if line == '-force-debug-info ^':
                continue
            if line.startswith('SET CONFIGURATIONS='):
                line = 'SET CONFIGURATIONS=-release'
            if line in ('cmake --build .', 'cmake --install .'):
                line += ' --config Release'
        lines.append(line)
    result = '\n'.join(lines) + '\n'
    if re.search(r'(?m)^(?:cmake --build|meson |ninja |nmake |msbuild |perl Configure).*\b(?:Debug|debug-static|debug-VC)\b', result):
        raise RuntimeError(f'Debug build command remains in {name}.')
    return result


def main():
    parser = argparse.ArgumentParser(description='Prepare upstream Windows dependencies for the Release client.')
    parser.add_argument('--print-only', action='store_true')
    args, upstream_args = parser.parse_known_args()
    if sys.platform != 'win32':
        parser.error('Run this script on Windows in an x64 Visual Studio environment.')
    if os.environ.get('Platform') != 'x64':
        parser.error('Initialize vcvars64.bat before running this script.')
    os.environ['CMAKE_BUILD_PARALLEL_LEVEL'] = '2'
    os.environ['CARGO_BUILD_JOBS'] = '2'
    os.environ['ALLOWGRAM_BUILD_JOBS'] = '2'
    upstream = Path(__file__).resolve().parent.parent / 'prepare' / 'prepare.py'
    source = upstream.read_text(encoding='utf-8')
    marker = 'def runStages():\n'
    if source.count(marker) != 1:
        raise RuntimeError('Upstream preparation entry point changed.')
    injected = (
        marker
        + "    for item in stages:\n"
        + "        item['commands'] = _release_commands(item['name'], item['commands'])\n"
        + "    if _print_only:\n"
        + "        for item in stages:\n"
        + "            print('### ' + item['name'] + '\\n' + item['commands'])\n"
        + "        return\n"
    )
    source = source.replace(marker, injected)
    sys.argv = [str(upstream), 'qt6', 'silent', *upstream_args]
    namespace = {
        '__file__': str(upstream),
        '__name__': '__main__',
        '_release_commands': release_commands,
        '_print_only': args.print_only,
    }
    exec(compile(source, str(upstream), 'exec'), namespace)


if __name__ == '__main__':
    main()
