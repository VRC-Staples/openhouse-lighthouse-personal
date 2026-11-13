#!/usr/bin/env python3
"""Package the native Windows build only after its required CTest targets pass."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from windows_pe import require_x64, require_system_dll_search
from toolchain_lock import read_lock

ROOT = Path(__file__).resolve().parent.parent
NAME = 'openhouse-lighthouse-personal-windows-x64'
SYSTEM_DLLS = {
    'advapi32.dll', 'bcrypt.dll', 'comctl32.dll', 'comdlg32.dll', 'crypt32.dll',
    'dbghelp.dll', 'dwmapi.dll', 'psapi.dll', 'shcore.dll',
    'gdi32.dll', 'hid.dll', 'imm32.dll', 'iphlpapi.dll', 'kernel32.dll', 'ntdll.dll',
    'ole32.dll', 'oleaut32.dll', 'opengl32.dll', 'setupapi.dll', 'shell32.dll',
    'shlwapi.dll', 'user32.dll', 'version.dll', 'winmm.dll', 'ws2_32.dll',
}
REQUIRED_TESTS = {
    'personal_safety', 'personal_shutdown', 'firmware_numbers', 'unit', 'replay', 'openvr_components',
    'openvr_driver', 'runtime', 'capture_help', 'capture_bad_duration', 'capture_no_overwrite',
    'viewer_math', 'viewer_dll_search', 'viewer_startup', 'replay_errors', 'zlib_compatibility',
    'viewer_connection', 'viewer_log', 'viewer_network',
}


def output(*command):
    return subprocess.check_output(command, cwd=ROOT, text=True, encoding='utf-8',
                                   errors='replace').strip()


def require_tests(report):
    root = ET.parse(report).getroot()
    names = {case.attrib['name'] for case in root.iter('testcase')}
    if not REQUIRED_TESTS <= names:
        raise ValueError(f'Missing required CTest results: {sorted(REQUIRED_TESTS - names)}')
    for suite in root.iter('testsuite'):
        if any(int(suite.attrib.get(key, '0')) for key in ('failures', 'errors', 'skipped', 'disabled')):
            raise ValueError('CTest reports failed, skipped, or disabled targets')
    if any(node.tag in {'failure', 'error', 'skipped'} for node in root.iter()):
        raise ValueError('CTest did not pass every target')
    return sorted(names)


def require_imports(text):
    imports = sorted(set(re.findall(r'^\s+([\w.-]+\.dll)\s*$', text, re.I | re.M)))
    if not imports:
        raise ValueError('dumpbin reported no DLL imports')
    for name in imports:
        if name.lower() not in SYSTEM_DLLS and not name.lower().startswith('api-ms-win-'):
            raise ValueError(f'Unbundled or unexpected dependency: {name}')
    return imports


def require_exports(text):
    names = re.findall(r'^\s+\d+\s+[0-9a-f]+\s+[0-9a-f]+\s+(\S+)', text, re.I | re.M)
    if names != ['HmdDriverFactory']:
        raise ValueError(f'Unexpected driver exports: {names}')


def copy_artifacts(build, destination):
    files = {
        'openvr/lighthouse/bin/win64/driver_lighthouse.dll': 'driver_lighthouse.dll',
        'openhouse-installer.exe': 'openhouse-installer.exe',
        'openhouse-viewer.exe': 'apps/openhouse-viewer.exe',
    }
    artifacts = []
    for source_name, target_name in files.items():
        source, target = build / source_name, destination / target_name
        data = source.read_bytes()
        require_x64(data, source.suffix == '.dll')
        imports = require_imports(output('dumpbin', '/dependents', str(source)))
        if source.name == 'openhouse-viewer.exe':
            require_system_dll_search(data)
            if re.search(r'\bShellExecute(?:Ex)?[AW]\b', output('dumpbin', '/imports', str(source))):
                raise ValueError('The viewer still imports a shell-opening callback')
        if source.suffix == '.dll':
            require_exports(output('dumpbin', '/exports', str(source)))
            if b'openhouse-personal-lighthouse-only' not in data:
                raise ValueError('The driver lacks the personal-fork marker')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        artifacts.append({'file': target_name, 'sha256': hashlib.sha256(data).hexdigest(),
                          'imports': imports})
    return artifacts


def copy_notices(prefix, destination, ports):
    licenses = destination / 'licenses'
    licenses.mkdir()
    for port in ports:
        # Required, not best-effort: a missing dependency notice blocks packaging.
        shutil.copy2(prefix / 'share' / port / 'copyright', licenses / f'{port}.txt')
    shutil.copy2(ROOT / 'third_party/imgui/LICENSE.txt', licenses / 'imgui.txt')
    shutil.copy2(ROOT / 'LICENSE', destination / 'LICENSE.txt')
    shutil.copy2(ROOT / 'README.md', destination / 'README.md')
    shutil.copy2(ROOT / 'docs/VALIDATION.md', destination / 'VALIDATION.md')


def package(build, prefix):
    if output('git', 'status', '--porcelain'):
        raise ValueError('Refusing to package a dirty source tree')
    tests = require_tests(build / 'ctest-results.xml')
    commit = output('git', 'rev-parse', 'HEAD')
    if os.environ.get('GITHUB_SHA', commit) != commit:
        raise ValueError('Source revision does not match the workflow revision')
    lock = read_lock(ROOT)
    dist = ROOT / 'dist'
    dist.mkdir(exist_ok=True)
    archive = dist / f'{NAME}.zip'
    if any(path.exists() for path in (archive, dist / 'BUILD.json', dist / 'SHA256SUMS.txt')):
        raise ValueError('Refusing to overwrite an existing package')
    with tempfile.TemporaryDirectory(prefix='.package-', dir=dist) as temporary:
        folder = Path(temporary) / NAME
        folder.mkdir()
        artifacts = copy_artifacts(build, folder)
        target_ports = [name for name, port in lock['ports'].items() if not port['host_only']]
        copy_notices(prefix, folder, target_ports)
        record = {
            'source_commit': commit, 'source_dirty': False, 'profile': 'personal-lighthouse-only',
            'compiler': 'MSVC ' + os.environ.get('VCToolsVersion', 'unknown'),
            'runner_image': os.environ.get('ImageVersion', 'unknown'), 'toolchain': lock,
            'workflow_run': os.environ.get('GITHUB_RUN_ID'), 'ctest_targets_passed': tests,
            'hardware_validated': False, 'signed': False, 'artifacts': artifacts,
        }
        (folder / 'BUILD.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        built = Path(shutil.make_archive(str(Path(temporary) / NAME), 'zip', temporary, NAME))
        built.rename(archive)
        shutil.copy2(folder / 'BUILD.json', dist / 'BUILD.json')
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    (dist / 'SHA256SUMS.txt').write_text(f'{checksum}  {archive.name}\n', encoding='ascii')
    print(f'Packaged {archive.name}: {checksum}')


if __name__ == '__main__':
    package(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
