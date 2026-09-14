from pathlib import Path
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile


SCRIPT = Path('Telegram/build/allowgram/archive_source.py')
PREFIX = 'Allowgram-source/'
DIRECTORY_LINK = 'Telegram/ThirdParty/fcitx5-qt/qt6/dbusaddons/interfaces'
DIRECTORY_TARGET = '../../qt5/dbusaddons/interfaces/'


class ArchiveSourceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='allowgram-archive-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.repository = self.root / 'source'
        self.environment = dict(os.environ, GIT_CONFIG_NOSYSTEM='1',
                                GIT_CONFIG_GLOBAL=os.devnull)
        self.init_repository(self.repository)
        self.write(SCRIPT, Path(__file__).with_name('archive_source.py').read_bytes())

    def git(self, *arguments, repository=None, input=None):
        result = subprocess.run(
            ['git', *arguments], cwd=repository or self.repository,
            env=self.environment, input=input, capture_output=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        return result.stdout

    def init_repository(self, repository):
        repository.mkdir(parents=True)
        self.git('init', repository=repository)
        for key, value in (
            ('user.name', 'Archive Fixture'),
            ('user.email', 'archive-fixture@example.invalid'),
            ('commit.gpgsign', 'false'),
            ('core.autocrlf', 'false'),
            ('core.symlinks', 'true'),
            ('core.hooksPath', str(self.root / 'no-hooks')),
        ):
            self.git('config', '--local', key, value, repository=repository)

    def write(self, relative, data, repository=None):
        path = (repository or self.repository) / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def commit(self, repository=None):
        self.git('add', '.', repository=repository)
        self.git('commit', '-m', 'Archive fixture', repository=repository)

    def symlink(self, relative, target, directory=False, repository=None):
        path = (repository or self.repository) / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        try:
            path.symlink_to(target, target_is_directory=directory)
        except OSError as error:
            if os.name == 'nt' and error.winerror == 1314:
                self.skipTest('Windows symlink privilege is unavailable')
            raise

    def placeholder(self, relative, target, repository=None):
        self.git('config', '--local', 'core.symlinks', 'false', repository=repository)
        data = target.encode('utf-8')
        self.write(relative, data, repository=repository)
        object_id = self.git('hash-object', '-w', '--stdin', input=data,
                             repository=repository).decode().strip()
        self.git('update-index', '--add', '--cacheinfo', f'120000,{object_id},{relative}',
                 repository=repository)

    def link_fixture(self):
        self.write('plain.txt', b'regular source\n')
        self.write('Telegram/ThirdParty/fcitx5-qt/qt5/dbusaddons/interfaces/api.xml',
                   b'<interface/>\n')
        self.placeholder(DIRECTORY_LINK, DIRECTORY_TARGET)
        self.placeholder('file-link', 'plain.txt')
        self.placeholder('chain-link', 'file-link')
        self.placeholder('interface-link', DIRECTORY_LINK + '/api.xml')
        self.commit()

    def manifest_export(self, files, symlinks=None):
        repository = self.root / 'export'
        self.write(SCRIPT, (self.repository / SCRIPT).read_bytes(), repository=repository)
        manifest = {
            'product': 'Allowgram',
            'sourceCommit': '1' * 40,
            'upstream': 'https://github.com/telegramdesktop/tdesktop',
            'files': [SCRIPT.as_posix(), *files],
            'extra': 'preserved',
        }
        if symlinks is not None:
            manifest['symlinks'] = symlinks
        self.write('SOURCE-MANIFEST.json', json.dumps(manifest).encode('utf-8'),
                   repository=repository)
        return repository, manifest

    def archive(self, repository=None, name='source.zip', success=True):
        repository = repository or self.repository
        output = self.root / name
        result = subprocess.run(
            [sys.executable, '-B', str(repository / SCRIPT), str(output)],
            cwd=repository, env=self.environment, capture_output=True, text=True, timeout=30,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse(output.exists())
        return output, result

    def assert_link(self, archive, relative, target):
        info = archive.getinfo(PREFIX + relative)
        self.assertEqual(info.create_system, 3)
        self.assertEqual(info.external_attr >> 16, stat.S_IFLNK | 0o777)
        self.assertEqual(archive.read(info), target.encode('utf-8'))

    def test_directory_symlink(self):
        self.write('Telegram/ThirdParty/fcitx5-qt/qt5/dbusaddons/interfaces/api.xml',
                   b'<interface/>\n')
        self.symlink(DIRECTORY_LINK, DIRECTORY_TARGET, directory=True)
        self.commit()
        self.assertTrue(self.git('ls-files', '--stage', DIRECTORY_LINK).startswith(b'120000 '))
        self.assertTrue((self.repository / DIRECTORY_LINK).is_symlink())
        self.assertEqual(self.git('show', 'HEAD:' + DIRECTORY_LINK), DIRECTORY_TARGET.encode())
        output, _ = self.archive()
        with zipfile.ZipFile(output) as archive:
            self.assert_link(archive, DIRECTORY_LINK, DIRECTORY_TARGET)
            manifest = json.loads(archive.read(PREFIX + 'SOURCE-MANIFEST.json'))
            self.assertEqual(manifest['symlinks'][DIRECTORY_LINK], DIRECTORY_TARGET)

    def test_regular_file_and_real_file_symlink(self):
        self.write('plain.txt', b'regular source\n')
        self.symlink('file-link', 'plain.txt')
        self.commit()
        self.assertTrue(self.git('ls-files', '--stage', 'file-link').startswith(b'120000 '))
        self.assertEqual(self.git('show', 'HEAD:file-link'), b'plain.txt')
        output, _ = self.archive()
        with zipfile.ZipFile(output) as archive:
            self.assertEqual(archive.read(PREFIX + 'plain.txt'), b'regular source\n')
            self.assertTrue(stat.S_ISREG(archive.getinfo(PREFIX + 'plain.txt').external_attr >> 16))
            self.assert_link(archive, 'file-link', 'plain.txt')
            manifest = json.loads(archive.read(PREFIX + 'SOURCE-MANIFEST.json'))
            self.assertEqual(manifest['sourceCommit'], self.git('rev-parse', 'HEAD').decode().strip())
            self.assertEqual(manifest['product'], 'Allowgram')
            self.assertEqual(manifest['upstream'], 'https://github.com/telegramdesktop/tdesktop')
            self.assertEqual(manifest['files'], sorted([SCRIPT.as_posix(), 'plain.txt', 'file-link']))

    def test_core_symlinks_false_clone(self):
        self.link_fixture()
        clone = self.root / 'clone'
        self.git('clone', '--no-local', '--config', 'core.symlinks=false',
                 str(self.repository), str(clone))
        self.assertEqual(self.git('config', '--local', 'core.symlinks', repository=clone), b'false\n')
        self.assertFalse((clone / DIRECTORY_LINK).is_symlink())
        self.assertEqual((clone / DIRECTORY_LINK).read_bytes(), DIRECTORY_TARGET.encode())
        self.assertTrue(self.git('ls-files', '--stage', DIRECTORY_LINK,
                                 repository=clone).startswith(b'120000 '))
        output, _ = self.archive(repository=clone)
        with zipfile.ZipFile(output) as archive:
            self.assert_link(archive, DIRECTORY_LINK, DIRECTORY_TARGET)
            self.assert_link(archive, 'file-link', 'plain.txt')
            self.assert_link(archive, 'chain-link', 'file-link')
            self.assert_link(archive, 'interface-link', DIRECTORY_LINK + '/api.xml')

    def test_recursive_submodule_link_prefix(self):
        leaf = self.root / 'leaf'
        middle = self.root / 'middle'
        self.init_repository(leaf)
        self.init_repository(middle)
        self.write('qt5/dbusaddons/interfaces/api.xml', b'<interface/>\n', repository=leaf)
        self.placeholder('qt6/dbusaddons/interfaces', DIRECTORY_TARGET, repository=leaf)
        self.commit(repository=leaf)
        for repository, source, destination in (
            (middle, leaf, 'deps/fcitx5-qt'),
            (self.repository, middle, 'vendor/middle'),
        ):
            self.git('-c', 'protocol.file.allow=always', '-c', 'core.symlinks=false',
                     'submodule', 'add', '--', str(source), destination, repository=repository)
            self.git('config', '--local', 'core.symlinks', 'false',
                     repository=repository / destination)
            self.commit(repository=repository)
        self.git('-c', 'protocol.file.allow=always', '-c', 'core.symlinks=false',
                 'submodule', 'update', '--init', '--recursive')
        self.git('config', '--local', 'core.symlinks', 'false',
                 repository=self.repository / 'vendor/middle/deps/fcitx5-qt')
        self.assertEqual(self.git('status', '--porcelain', '--ignore-submodules=none'), b'')
        output, _ = self.archive()
        relative = 'vendor/middle/deps/fcitx5-qt/qt6/dbusaddons/interfaces'
        with zipfile.ZipFile(output) as archive:
            self.assert_link(archive, relative, DIRECTORY_TARGET)
            manifest = json.loads(archive.read(PREFIX + 'SOURCE-MANIFEST.json'))
            self.assertEqual(manifest['symlinks'], {relative: DIRECTORY_TARGET})
            self.assertNotIn('vendor/middle', manifest['files'])
            self.assertFalse(any('.git' in name.split('/') for name in archive.namelist()))

    def test_manifest_only_roundtrip(self):
        self.link_fixture()
        output, _ = self.archive()
        with zipfile.ZipFile(output) as archive:
            archive.extractall(self.root / 'extracted')
            expected = {info.filename: archive.read(info) for info in archive.infolist()}
        extracted = self.root / 'extracted' / 'Allowgram-source'
        self.assertFalse((extracted / '.git').exists())
        self.assertFalse((extracted / DIRECTORY_LINK).is_symlink())
        self.assertEqual((extracted / DIRECTORY_LINK).read_bytes(), DIRECTORY_TARGET.encode())
        self.write('unlisted.txt', b'not source\n', repository=extracted)
        second, _ = self.archive(repository=extracted, name='roundtrip.zip')
        with zipfile.ZipFile(second) as archive:
            self.assertEqual({info.filename: archive.read(info) for info in archive.infolist()}, expected)
            self.assert_link(archive, DIRECTORY_LINK, DIRECTORY_TARGET)
            self.assert_link(archive, 'file-link', 'plain.txt')

    def test_old_manifest_flattened_link_remains_regular(self):
        repository, manifest = self.manifest_export(['old-file-link', 'ordinary.txt'])
        self.write('old-file-link', b'previously dereferenced contents\n', repository=repository)
        self.write('ordinary.txt', b'ordinary\n', repository=repository)
        output, _ = self.archive(repository=repository)
        with zipfile.ZipFile(output) as archive:
            self.assertEqual(json.loads(archive.read(PREFIX + 'SOURCE-MANIFEST.json')), manifest)
            self.assertEqual(archive.read(PREFIX + 'old-file-link'), b'previously dereferenced contents\n')
            self.assertTrue(stat.S_ISREG(archive.getinfo(PREFIX + 'old-file-link').external_attr >> 16))

    def test_outside_root_link_rejected(self):
        self.write('outside.txt', b'outside source\n', repository=self.root)
        self.placeholder('outside-link', '../outside.txt')
        self.commit()
        _, result = self.archive(success=False)
        self.assertIn('Source symlink is outside the repository', result.stderr)

    def test_real_outside_root_link_rejected(self):
        self.write('outside.txt', b'outside source\n', repository=self.root)
        self.symlink('outside-link', '../outside.txt')
        self.commit()
        _, result = self.archive(success=False)
        self.assertIn('Source symlink is outside the repository', result.stderr)

    def test_absolute_link_targets_rejected(self):
        for target in ('/outside.txt', 'C:/outside.txt', 'C:outside.txt', '\\\\server\\share\\file'):
            with self.subTest(target=target):
                repository, _ = self.manifest_export(['link'], {'link': target})
                self.write('link', target.encode(), repository=repository)
                _, result = self.archive(repository=repository, success=False)
                self.assertIn('Invalid source symlink target', result.stderr)

    def test_dangling_link_rejected(self):
        self.placeholder('link', 'missing.txt')
        self.commit()
        _, result = self.archive(success=False)
        self.assertIn('Source symlink target is missing', result.stderr)

    def test_cyclic_links_rejected(self):
        self.placeholder('first', 'second')
        self.placeholder('second', 'first')
        self.commit()
        _, result = self.archive(success=False)
        self.assertIn('Cyclic or excessive source symlink chain', result.stderr)

    def test_link_chain_length_is_bounded(self):
        symlinks = {f'link-{index}': f'link-{index + 1}' for index in range(41)}
        symlinks['link-40'] = SCRIPT.as_posix()
        repository, _ = self.manifest_export(list(symlinks), symlinks)
        for relative, target in symlinks.items():
            self.write(relative, target.encode(), repository=repository)
        _, result = self.archive(repository=repository, success=False)
        self.assertIn('Cyclic or excessive source symlink chain', result.stderr)

    def test_parent_traversal_follows_intermediate_links(self):
        self.write('directory/file.txt', b'source\n')
        self.placeholder('directory/root-link', '..')
        self.placeholder('escape-link', 'directory/root-link/../outside.txt')
        self.commit()
        _, result = self.archive(success=False)
        self.assertIn('Source symlink is outside the repository', result.stderr)

    def test_ignored_untracked_files_do_not_leak(self):
        self.link_fixture()
        self.write('.git/info/exclude', b'ignored.txt\n')
        self.write('ignored.txt', b'not source\n')
        self.write('Telegram/ThirdParty/fcitx5-qt/qt5/dbusaddons/interfaces/ignored.txt',
                   b'not source either\n')
        output, _ = self.archive()
        with zipfile.ZipFile(output) as archive:
            self.assertFalse(any(name.endswith('/ignored.txt') for name in archive.namelist()))
            self.assertFalse(any(name.startswith(PREFIX + DIRECTORY_LINK + '/')
                                 for name in archive.namelist()))

    def test_link_to_untracked_target_rejected(self):
        self.placeholder('link', 'ignored.txt')
        self.commit()
        self.write('.git/info/exclude', b'ignored.txt\n')
        self.write('ignored.txt', b'not source\n')
        _, result = self.archive(success=False)
        self.assertIn('Source symlink target is missing', result.stderr)

    def test_dirty_checkout_rejected(self):
        self.commit()
        self.write('untracked.txt', b'not source\n')
        _, result = self.archive(success=False)
        self.assertIn('Commit source changes', result.stderr)

    def test_missing_manifest_source_rejected(self):
        for symlinks in (None, {'missing.txt': SCRIPT.as_posix()}):
            with self.subTest(symlinks=symlinks):
                repository, _ = self.manifest_export(['missing.txt'], symlinks)
                _, result = self.archive(repository=repository, success=False)
                self.assertIn('Source file is missing or outside the repository', result.stderr)

    def test_unsafe_manifest_members_rejected(self):
        for relative in ('', '../outside.txt', '/outside.txt', 'C:/outside.txt',
                         'C:outside.txt', 'dir/../file', './file', 'dir//file',
                         'dir\\file', 'file:stream', '\\\\server\\share\\file'):
            with self.subTest(relative=relative):
                repository, _ = self.manifest_export([relative])
                _, result = self.archive(repository=repository, success=False)
                self.assertIn('Invalid source archive member', result.stderr)

    def test_manifest_symlink_mismatch_rejected(self):
        repository, _ = self.manifest_export(['link'], {'link': 'expected.txt'})
        self.write('link', b'changed.txt', repository=repository)
        _, result = self.archive(repository=repository, success=False)
        self.assertIn('Source symlink disagrees with manifest', result.stderr)

    def test_physical_parent_symlink_rejected(self):
        repository, _ = self.manifest_export(['alias/file.txt'])
        outside = self.root / 'outside'
        self.write('file.txt', b'outside source\n', repository=outside)
        self.symlink('alias', str(outside), directory=True, repository=repository)
        _, result = self.archive(repository=repository, success=False)
        self.assertIn('Source directory is missing or outside the repository', result.stderr)

    def test_uninitialized_gitlink_rejected(self):
        leaf = self.root / 'leaf'
        self.init_repository(leaf)
        self.write('file.txt', b'submodule source\n', repository=leaf)
        self.commit(repository=leaf)
        self.git('-c', 'protocol.file.allow=always', 'submodule', 'add', '--', str(leaf), 'vendor/leaf')
        self.commit()
        self.git('submodule', 'deinit', '--force', '--', 'vendor/leaf')
        _, result = self.archive(success=False)
        self.assertIn('Unsupported or uninitialized source entry (160000): vendor/leaf', result.stderr)

    def test_gitlink_without_submodule_definition_rejected(self):
        self.commit()
        commit = self.git('rev-parse', 'HEAD').decode().strip()
        self.git('update-index', '--add', '--cacheinfo', f'160000,{commit},unsupported')
        self.git('commit', '-m', 'Unsupported gitlink fixture')
        (self.repository / 'unsupported').mkdir()
        self.assertEqual(self.git('status', '--porcelain', '--ignore-submodules=none'), b'')
        _, result = self.archive(success=False)
        self.assertIn('Unsupported or uninitialized source entry (160000): unsupported', result.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
