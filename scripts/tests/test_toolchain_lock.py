#!/usr/bin/env python3
"""Check that local dependency changes cannot bypass the reviewed lock."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from toolchain_lock import read_lock


class ToolchainLockTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        for name in ('vcpkg.json', 'vcpkg-configuration.json', 'scripts/ci/toolchain-lock.json'):
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)
        shutil.copytree(ROOT / 'cmake/ports', self.root / 'cmake/ports')

    def change_lock(self, change):
        path = self.root / 'scripts/ci/toolchain-lock.json'
        lock = json.loads(path.read_text(encoding='utf-8'))
        change(lock)
        path.write_text(json.dumps(lock), encoding='utf-8')

    def test_accepts_reviewed_overlay(self):
        self.assertEqual(read_lock(self.root)['ports']['zlib']['version'], '1.4')

    def test_accepts_windows_line_endings(self):
        for path in self.root.rglob('*'):
            if path.is_file():
                path.write_bytes(path.read_bytes().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))
        read_lock(self.root)

    def test_rejects_changed_manifest_configuration_and_every_overlay_file(self):
        paths = [self.root / 'vcpkg.json', self.root / 'vcpkg-configuration.json']
        paths += list((self.root / 'cmake/ports/zlib').iterdir())
        for path in paths:
            with self.subTest(path=path.name):
                original = path.read_bytes()
                path.write_bytes(original + b'\n')
                with self.assertRaises(ValueError):
                    read_lock(self.root)
                path.write_bytes(original)

    def test_rejects_added_overlay_file(self):
        (self.root / 'cmake/ports/zlib/extra.cmake').write_text('')
        with self.assertRaises(ValueError):
            read_lock(self.root)

    def test_rejects_another_overlay_port(self):
        (self.root / 'cmake/ports/another-port').mkdir()
        with self.assertRaises(ValueError):
            read_lock(self.root)

    def test_rejects_missing_recipe(self):
        (self.root / 'cmake/ports/zlib/portfile.cmake').unlink()
        with self.assertRaises(ValueError):
            read_lock(self.root)

    def test_rejects_source_or_version_lock_mismatch(self):
        for field, value in [('source_sha512', '0' * 128), ('version', '1.3.2')]:
            with self.subTest(field=field):
                original = (self.root / 'scripts/ci/toolchain-lock.json').read_bytes()
                self.change_lock(lambda lock: lock['ports']['zlib'].update({field: value}))
                with self.assertRaises(ValueError):
                    read_lock(self.root)
                (self.root / 'scripts/ci/toolchain-lock.json').write_bytes(original)

    def test_rejects_manifest_baseline_mismatch(self):
        self.change_lock(lambda lock: lock.update(vcpkg_commit='0' * 40))
        with self.assertRaises(ValueError):
            read_lock(self.root)


if __name__ == '__main__':
    unittest.main()
