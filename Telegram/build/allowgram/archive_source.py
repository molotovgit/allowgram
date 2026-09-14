from collections import deque
from pathlib import Path
import json
import os
import stat
import subprocess
import sys
import zipfile


def check_member(relative):
    if (not isinstance(relative, str) or not relative
            or '\\' in relative or ':' in relative or '\0' in relative
            or any(part in ('', '.', '..') for part in relative.split('/'))):
        raise RuntimeError(f'Invalid source archive member: {relative!r}')


def check_links(paths, symlinks, directories):
    for relative, target in symlinks.items():
        if (not isinstance(target, str) or not target or '\0' in target
                or '\\' in target or ':' in target or target.startswith('/')):
            raise RuntimeError(f'Invalid source symlink target: {relative}')
    for relative in symlinks:
        pending = deque(relative.split('/'))
        resolved = []
        followed = 0
        while pending:
            part = pending.popleft()
            if part in ('', '.'):
                continue
            if part == '..':
                if not resolved:
                    raise RuntimeError(f'Source symlink is outside the repository: {relative}')
                resolved.pop()
                continue
            candidate = '/'.join([*resolved, part])
            if candidate in symlinks:
                followed += 1
                if followed > 40:
                    raise RuntimeError(f'Cyclic or excessive source symlink chain: {relative}')
                pending.extendleft(reversed(symlinks[candidate].split('/')))
            elif candidate in directories:
                resolved.append(part)
            elif candidate in paths and not pending:
                resolved.append(part)
            else:
                raise RuntimeError(f'Source symlink target is missing or not a directory: {relative}')


def main():
    repository = Path(__file__).resolve().parents[3]
    output = Path(sys.argv[1]).resolve()
    manifest_path = repository / 'SOURCE-MANIFEST.json'
    modes = {}
    if not (repository / '.git').exists() and manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
        paths = manifest['files']
    else:
        status = subprocess.check_output(
            ['git', 'status', '--porcelain', '--untracked-files=all',
             '--ignore-submodules=none'], cwd=repository,
        )
        if status.strip():
            raise RuntimeError('Commit source changes before creating the source archive.')
        commit = subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], cwd=repository,
        ).decode('ascii').strip()
        entries = subprocess.check_output(
            ['git', 'ls-files', '--stage', '--recurse-submodules', '-z'], cwd=repository,
        ).decode('utf-8').split('\0')
        for entry in filter(None, entries):
            attributes, relative = entry.split('\t', 1)
            mode, object_id, stage = attributes.split()
            if stage != '0' or mode not in ('100644', '100755', '120000'):
                raise RuntimeError(f'Unsupported or uninitialized source entry ({mode}): {relative}')
            modes[relative] = (mode, object_id)
        paths = sorted(modes)
        manifest = {
            'product': 'Allowgram',
            'sourceCommit': commit,
            'upstream': 'https://github.com/telegramdesktop/tdesktop',
            'files': paths,
        }
    if not isinstance(paths, list):
        raise RuntimeError('Source manifest files must be a list.')
    for relative in paths:
        check_member(relative)
    paths = set(paths)
    symlinks = manifest.get('symlinks', {})
    if not isinstance(symlinks, dict) or not symlinks.keys() <= paths:
        raise RuntimeError('Source manifest symlinks must refer to listed files.')
    symlinks = symlinks.copy()
    directories = {
        parent.as_posix()
        for relative in paths
        for parent in Path(relative).parents if parent != Path('.')
    }
    if directories & paths:
        raise RuntimeError('Source archive members overlap files and directories.')
    for relative in directories:
        path = repository / relative
        if (path.is_symlink() or not path.resolve().is_relative_to(repository)
                or not path.is_dir()):
            raise RuntimeError(f'Source directory is missing or outside the repository: {relative}')
    checked = []
    for relative in sorted(paths):
        path = repository / relative
        is_symlink = path.is_symlink()
        if not is_symlink and (not path.resolve().is_relative_to(repository) or not path.is_file()):
            raise RuntimeError(f'Source file is missing or outside the repository: {relative}')
        mode, object_id = modes.get(relative, (None, None))
        if mode == '120000':
            target = subprocess.check_output(
                ['git', 'cat-file', 'blob', object_id], cwd=path.parent,
            ).decode('utf-8', errors='surrogateescape')
            if not is_symlink and path.read_bytes() != target.encode('utf-8', errors='surrogateescape'):
                raise RuntimeError(f'Source symlink placeholder disagrees with Git: {relative}')
        elif is_symlink:
            target = os.readlink(path)
        else:
            target = (path.read_bytes().decode('utf-8', errors='surrogateescape')
                      if relative in symlinks else None)
        if target is not None:
            if relative in symlinks and symlinks[relative] != target:
                raise RuntimeError(f'Source symlink disagrees with manifest: {relative}')
            symlinks[relative] = target
        checked.append((path, relative))
    check_links(paths, symlinks, directories)
    if symlinks:
        manifest['symlinks'] = symlinks
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path, relative in checked:
            if relative in symlinks:
                info = zipfile.ZipInfo('Allowgram-source/' + relative)
                info.create_system = 3
                info.external_attr = (stat.S_IFLNK | 0o777) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(info, symlinks[relative].encode('utf-8', errors='surrogateescape'))
            else:
                archive.write(path, 'Allowgram-source/' + relative)
        archive.writestr(
            'Allowgram-source/SOURCE-MANIFEST.json',
            json.dumps(manifest, indent=2) + '\n',
        )
    print(f'Source archive: {output}')


if __name__ == '__main__':
    main()
