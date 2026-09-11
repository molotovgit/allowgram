from pathlib import Path
import json
import subprocess
import sys
import zipfile


def main():
    repository = Path(__file__).resolve().parents[3]
    output = Path(sys.argv[1]).resolve()
    manifest_path = repository / 'SOURCE-MANIFEST.json'
    if not (repository / '.git').exists() and manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
        paths = manifest['files']
    else:
        status = subprocess.check_output(
            ['git', 'status', '--porcelain', '--untracked-files=all'], cwd=repository,
        )
        if status.strip():
            raise RuntimeError('Commit source changes before creating the source archive.')
        commit = subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], cwd=repository,
        ).decode('ascii').strip()
        paths = subprocess.check_output(
            ['git', 'ls-files', '--recurse-submodules', '-z'], cwd=repository,
        ).decode('utf-8').split('\0')
        manifest = {
            'product': 'Allowgram',
            'sourceCommit': commit,
            'upstream': 'https://github.com/telegramdesktop/tdesktop',
            'files': sorted(set(path for path in paths if path)),
        }
    checked = []
    for relative in sorted(set(path for path in paths if path)):
        path = (repository / relative).resolve()
        if not path.is_relative_to(repository) or not path.is_file():
            raise RuntimeError(f'Source file is missing or outside the repository: {relative}')
        checked.append((path, relative))
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path, relative in checked:
            archive.write(path, 'Allowgram-source/' + relative)
        archive.writestr(
            'Allowgram-source/SOURCE-MANIFEST.json',
            json.dumps(manifest, indent=2) + '\n',
        )
    print(f'Source archive: {output}')


if __name__ == '__main__':
    main()
