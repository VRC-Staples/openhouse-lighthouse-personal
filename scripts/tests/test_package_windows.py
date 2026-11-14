#!/usr/bin/env python3
"""Hardware-free checks for the native Windows package validation gates."""
from contextlib import contextmanager
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[1] / 'package-windows-msvc.py'
sys.path.insert(0, str(SCRIPT.parent))
SPEC = importlib.util.spec_from_file_location('packager', SCRIPT)
packager = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(packager)


class PackageTests(unittest.TestCase):
    def test_only_system_imports(self):
        self.assertEqual(packager.require_imports('    KERNEL32.dll\n    USER32.dll\n'),
                         ['KERNEL32.dll', 'USER32.dll'])
        for text in ('', '    liblh.dll\n', '    VCRUNTIME140.dll\n', '    unknown.dll\n'):
            with self.assertRaises(ValueError):
                packager.require_imports(text)

    def test_only_factory_export(self):
        good = '    1    0 00012345 HmdDriverFactory\n'
        packager.require_exports(good)
        for text in ('', good + '    2    1 00012380 HIDBridge\n'):
            with self.assertRaises(ValueError):
                packager.require_exports(text)

    def test_image_architecture_and_kind(self):
        data = bytearray(256)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 128)
        data[128:132] = b'PE\0\0'
        struct.pack_into('<H', data, 132, 0x8664)
        struct.pack_into('<H', data, 150, 0x2000)
        struct.pack_into('<H', data, 152, 0x20b)
        packager.require_x64(data, True)
        with self.assertRaises(ValueError):
            packager.require_x64(data, False)
        struct.pack_into('<H', data, 132, 0x14c)
        with self.assertRaises(ValueError):
            packager.require_x64(data, True)
        with self.assertRaises(ValueError):
            packager.require_x64(b'MZ', True)

    def test_viewer_static_dll_search_is_system_only(self):
        data = bytearray(1024)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 128)
        data[128:132] = b'PE\0\0'
        struct.pack_into('<HH', data, 132, 0x8664, 1)
        struct.pack_into('<H', data, 148, 240)
        struct.pack_into('<H', data, 152, 0x20b)
        struct.pack_into('<I', data, 260, 16)
        struct.pack_into('<II', data, 344, 0x1000, 80)
        struct.pack_into('<III', data, 404, 0x1000, 128, 512)
        struct.pack_into('<I', data, 512, 80)
        struct.pack_into('<H', data, 590, 0x800)
        packager.require_system_dll_search(data)
        for flags in (0, 0x900):
            struct.pack_into('<H', data, 590, flags)
            with self.assertRaises(ValueError):
                packager.require_system_dll_search(data)
        struct.pack_into('<H', data, 590, 0x800)
        with self.assertRaises(ValueError):
            packager.require_system_dll_search(data[:550])
        struct.pack_into('<I', data, 348, 79)
        with self.assertRaises(ValueError):
            packager.require_system_dll_search(data)

    def test_requires_each_viewer_regression_target(self):
        viewer_tests = {'viewer_connection', 'viewer_log', 'viewer_network'}
        names = packager.REQUIRED_TESTS | viewer_tests
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / 'tests.xml'
            for missing in viewer_tests:
                with self.subTest(missing=missing):
                    cases = ''.join(f'<testcase name="{name}"/>' for name in names - {missing})
                    report.write_text(f'<testsuite>{cases}</testsuite>')
                    with self.assertRaises(ValueError):
                        packager.require_tests(report)

    def test_requires_installer_regressions(self):
        installer_tests = {'installer_actions', 'installer_discovery'}
        self.assertTrue(installer_tests <= packager.REQUIRED_TESTS)
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / 'tests.xml'
            for missing in installer_tests:
                with self.subTest(missing=missing):
                    cases = ''.join(f'<testcase name="{name}"/>'
                                    for name in packager.REQUIRED_TESTS - {missing})
                    report.write_text(f'<testsuite>{cases}</testsuite>')
                    with self.assertRaises(ValueError):
                        packager.require_tests(report)

    def test_requires_all_test_targets_to_pass(self):
        cases = ''.join(f'<testcase name="{name}"/>' for name in packager.REQUIRED_TESTS)
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / 'tests.xml'
            report.write_text(f'<testsuite failures="0">{cases}</testsuite>')
            self.assertEqual(set(packager.require_tests(report)), packager.REQUIRED_TESTS)
            for body in ('', cases + '<testcase name="extra"><failure/></testcase>',
                         cases + '<testcase name="extra"><skipped/></testcase>'):
                report.write_text(f'<testsuite>{body}</testsuite>')
                with self.assertRaises(ValueError):
                    packager.require_tests(report)
            report.write_text(f'<testsuite disabled="1">{cases}</testsuite>')
            with self.assertRaises(ValueError):
                packager.require_tests(report)


class DiagnosticPackageTests(unittest.TestCase):
    layout = {
        'openvr/lighthouse/bin/win64/driver_lighthouse.dll': 'driver_lighthouse.dll',
        'openhouse-installer.exe': 'openhouse-installer.exe',
        'openhouse-viewer.exe': 'apps/openhouse-viewer.exe',
        'lhtool.exe': 'apps/lhtool.exe',
        'lhcapture.exe': 'apps/lhcapture.exe',
        'lhviz.exe': 'apps/lhviz.exe',
        'openhouse-compare.exe': 'apps/openhouse-compare.exe',
        'lh.dll': 'apps/lh.dll',
    }

    @contextmanager
    def fixture(self, overrides=None):
        with tempfile.TemporaryDirectory() as temporary:
            build, destination = Path(temporary) / 'build', Path(temporary) / 'package'
            for source in [*self.layout, 'openhouse-check.exe', 'example_app.exe']:
                path = build / source
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'openhouse-personal-lighthouse-only ' + source.encode())

            def dumpbin(*command):
                filename = Path(command[-1]).name
                if command[1] == '/dependents':
                    dependencies = (overrides or {}).get(filename, ['KERNEL32.dll'])
                    if overrides is None and filename in {
                        'lhtool.exe', 'lhviz.exe', 'openhouse-compare.exe'
                    }:
                        dependencies = ['KERNEL32.dll', 'lh.dll']
                    return ''.join(f'    {name}\n' for name in dependencies)
                if command[1] == '/imports':
                    return '    KERNEL32.dll\n'
                if command[1] == '/exports' and filename == 'driver_lighthouse.dll':
                    return '    1    0 00012345 HmdDriverFactory\n'
                raise AssertionError(command)

            # PE structure checks have their own tests. Exercise copying and import policy here.
            with mock.patch.object(packager, 'output', side_effect=dumpbin), \
                    mock.patch.object(packager, 'require_x64'), \
                    mock.patch.object(packager, 'require_system_dll_search'):
                yield build, destination

    def test_diagnostic_layout_and_matching_runtime_exclude_checker_and_example(self):
        with self.fixture() as (build, destination):
            records = packager.copy_artifacts(build, destination)
            self.assertEqual({record['file'] for record in records}, set(self.layout.values()))
            for source, target in self.layout.items():
                self.assertEqual((build / source).read_bytes(), (destination / target).read_bytes())
            for record in records:
                self.assertEqual(record['sha256'],
                                 hashlib.sha256((destination / record['file']).read_bytes()).hexdigest())

    def test_missing_matching_runtime_blocks_packaging(self):
        with self.fixture() as (build, destination):
            (build / 'lh.dll').unlink()
            with self.assertRaises(FileNotFoundError):
                packager.copy_artifacts(build, destination)

    def test_driver_and_runtime_library_remain_system_only(self):
        for filename in ['driver_lighthouse.dll', 'lh.dll', 'openhouse-installer.exe']:
            with self.subTest(filename=filename), self.fixture(
                {filename: ['KERNEL32.dll', 'lh.dll']}
            ) as (build, destination):
                with self.assertRaises(ValueError):
                    packager.copy_artifacts(build, destination)

    def test_runtime_exception_does_not_allow_other_unbundled_libraries(self):
        with self.fixture({'lhtool.exe': ['KERNEL32.dll', 'VCRUNTIME140.dll']}) as fixture:
            with self.assertRaises(ValueError):
                packager.copy_artifacts(*fixture)


if __name__ == '__main__':
    unittest.main()
