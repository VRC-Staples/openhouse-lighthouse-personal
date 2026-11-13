#!/usr/bin/env python3
"""Hardware-free checks for the native Windows package validation gates."""
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest

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


if __name__ == '__main__':
    unittest.main()
