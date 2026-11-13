#!/usr/bin/env python3
"""Strip and package the cross-built Windows DLL and standalone apps.
Reject unexpected third-party DLL dependencies rather than ship a broken bundle.
"""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from windows_pe import require_system_dll_search
from toolchain_lock import read_lock

repo = Path(__file__).resolve().parent.parent
lock = read_lock(repo)
build = Path(sys.argv[1]).resolve()
prefix = Path(sys.argv[2]).resolve()
out = repo / 'dist/openhouse-lighthouse-personal-windows-x64'
if subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain'], text=True).strip():
    sys.exit('Refusing a release package from a dirty source tree; commit reviewed changes first.')
# Fail before touching an existing output if the compiler omitted loader hardening.
require_system_dll_search((build / 'openhouse-viewer.exe').read_bytes())
shutil.rmtree(out, ignore_errors=True)
(out / 'apps').mkdir(parents=True)
files = {build / 'openvr/lighthouse/bin/win64/driver_lighthouse.dll': out / 'driver_lighthouse.dll',
         build / 'openhouse-installer.exe': out / 'openhouse-installer.exe'}
files[build / 'openhouse-viewer.exe'] = out / 'apps/openhouse-viewer.exe'
system = {'advapi32.dll', 'iphlpapi.dll', 'kernel32.dll', 'shlwapi.dll', 'user32.dll', 'gdi32.dll',
          'shell32.dll', 'winmm.dll', 'ole32.dll', 'comdlg32.dll', 'msvcrt.dll',
          'ucrtbase.dll', 'ntdll.dll', 'imm32.dll', 'version.dll', 'oleaut32.dll', 'setupapi.dll', 'opengl32.dll', 'hid.dll', 'ws2_32.dll'}
artifacts = []
for source, target in files.items():
    if not source.is_file():
        sys.exit(f'Missing build artifact: {source}')
    subprocess.run(['x86_64-w64-mingw32-strip', '--strip-unneeded', '-o', str(target), str(source)], check=True)
    if source.name == 'openhouse-viewer.exe':
        require_system_dll_search(target.read_bytes())
    pe = subprocess.check_output(['x86_64-w64-mingw32-objdump', '-p', str(target)], text=True)
    imports = re.findall(r'DLL Name:\s*(\S+)', pe)
    for name in imports:
        if name.lower() not in system and not name.lower().startswith('api-ms-win-') and not (target.parent / name).is_file() and name.lower() != 'liblh.dll':
            sys.exit(f'Unbundled Windows dependency in {target}: {name}')
        if name.lower() == 'liblh.dll' and target.parent != out / 'apps':
            sys.exit('Driver must not depend on liblh.dll')
    artifacts.append({'file': str(target.relative_to(out)), 'imports': imports,
                      'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})
licenses = out / 'licenses'
licenses.mkdir(exist_ok=True)
for copyright in (prefix / 'share').glob('*/copyright'):
    shutil.copy2(copyright, licenses / (copyright.parent.name + '.txt'))
shutil.copy2(repo / 'third_party/imgui/LICENSE.txt', licenses / 'imgui.txt')
shutil.copy2(repo / 'LICENSE', out / 'LICENSE.txt')
if Path('/usr/share/licenses/openvr').is_dir():
    shutil.copytree('/usr/share/licenses/openvr', licenses / 'openvr', dirs_exist_ok=True)
for package in ['mingw-w64-crt', 'mingw-w64-headers', 'mingw-w64-winpthreads', 'gcc']:
    notice = Path('/usr/share/licenses') / package
    if notice.is_dir():
        shutil.copytree(notice, licenses / package, dirs_exist_ok=True)
gpl = Path('/usr/share/licenses/spdx/GPL-3.0-only.txt')
if gpl.is_file():
    shutil.copy2(gpl, licenses / 'GCC-GPL-3.0.txt')
shutil.copy2(repo / 'README.md', out / 'README.md')
(out / 'BUILD.json').write_text(json.dumps({
    'source_commit': subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip(),
    'source_dirty': False,
    'profile': 'personal-lighthouse-trackers-and-controllers-no-headset',
    'compiler': subprocess.check_output(['x86_64-w64-mingw32-g++', '-dumpfullversion'], text=True).strip(),
    'vcpkg_baseline': lock['vcpkg_commit'],
    'toolchain': lock,
    'artifacts': artifacts,
}, indent=2) + '\n')
archive = shutil.make_archive(str(out), 'zip', out.parent, out.name)
print(f'Packaged {archive}')
for item in artifacts:
    print(f"  {item['file']}: {(out / item['file']).stat().st_size / 1048576:.1f} MiB")
