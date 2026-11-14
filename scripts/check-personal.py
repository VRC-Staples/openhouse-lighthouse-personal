#!/usr/bin/env python3
"""Run hardware-free Windows checks with an existing Zig 0.15.2 installation."""
from pathlib import Path
import ast
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'build-personal-checks'


def run(command, label, expected=0):
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                            encoding='utf-8', errors='replace', timeout=300)
    output = result.stdout + result.stderr
    (OUT / (label + '.log')).write_text(output, encoding='utf-8')
    if result.returncode != expected:
        raise RuntimeError(f'{label} exited {result.returncode}:\n{output}')
    print(f'PASS: {label}')
    if output.strip():
        print(output.strip())


def main():
    if os.name != 'nt':
        raise RuntimeError('These checks exercise Windows file locks and peer authorization.')
    zig = shutil.which('zig')
    if not zig:
        raise RuntimeError('Zig is not installed. This script does not download tools.')
    version = subprocess.check_output([zig, 'version'], text=True).strip()
    if version != '0.15.2':
        raise RuntimeError(f'Expected existing Zig 0.15.2, found {version}')
    OUT.mkdir(exist_ok=True)
    run([zig, 'cc', '-target', 'x86_64-windows-gnu', '-std=c11', '-Wall', '-Wextra',
         '-Ibasestation_firmware/src', 'tests/personal/test_firmware_numbers.c',
         '-o', str(OUT / 'firmware-numbers.exe')], 'compile-firmware-numbers')
    run([str(OUT / 'firmware-numbers.exe')], 'test-firmware-numbers')
    run([zig, 'cc', '-target', 'x86_64-windows-gnu', '-std=c11', '-S', '-emit-llvm',
         '-Ibasestation_firmware/src', 'basestation_firmware/src/console.c',
         '-o', str(OUT / 'console-host.ll')], 'compile-console-host-ir')
    compiler = [zig, 'c++', '-target', 'x86_64-windows-gnu', '-std=c++20', '-O0',
                '-g', '-Wall', '-Wextra', '-Isrc', '-Iapps/openhouse-installer']
    run(compiler + ['tests/personal/test_safety.cpp',
                    'src/transport/usb/wireless_decoder.cpp', 'src/common/net.cpp',
                    '-lws2_32', '-liphlpapi', '-ladvapi32',
                    '-o', str(OUT / 'personal-safety.exe')], 'compile-safety')
    run([str(OUT / 'personal-safety.exe')], 'test-safety')
    viewer = ['-Iinclude', '-Iapps/openhouse-viewer', '-Ithird_party/imgui']
    run(compiler + viewer + ['tests/personal/test_viewer_math.cpp',
        'apps/openhouse-viewer/log_shape.cpp', '-o', str(OUT / 'viewer-math.exe')], 'compile-viewer-math')
    run([str(OUT / 'viewer-math.exe')], 'test-viewer-math')
    probe = OUT / 'openhouse-loader-probe.dll'
    run([zig, 'cc', '-target', 'x86_64-windows-gnu', '-shared', '-O0',
         'tests/personal/dll_probe.c', '-o', str(probe)], 'compile-loader-probe')
    run(compiler + viewer + ['tests/personal/test_dll_search.cpp',
        '-o', str(OUT / 'dll-search.exe')], 'compile-dll-search')
    run([str(OUT / 'dll-search.exe'), str(probe)], 'test-dll-search')
    shutdown = ['apps/openhouse-installer/process_shutdown.cpp', '-ladvapi32', '-luser32']
    run(compiler + ['tests/personal/test_shutdown.cpp'] + shutdown +
        ['-o', str(OUT / 'shutdown-tests.exe')], 'compile-shutdown')
    run([str(OUT / 'shutdown-tests.exe')], 'test-shutdown')
    actions = ['apps/openhouse-installer/installer_actions.cpp']
    run(compiler + ['tests/personal/test_installer_actions.cpp'] + actions + shutdown +
        ['-o', str(OUT / 'installer-actions.exe')], 'compile-installer-actions')
    run([str(OUT / 'installer-actions.exe')], 'test-installer-actions')
    discovery = ['apps/openhouse-installer/steam_metadata.cpp',
                 'apps/openhouse-installer/steamvr_discovery.cpp']
    run(compiler + ['tests/personal/test_steamvr_discovery.cpp'] + discovery + shutdown +
        ['-o', str(OUT / 'installer-discovery.exe')], 'compile-installer-discovery')
    run([str(OUT / 'installer-discovery.exe')], 'test-installer-discovery')
    menu = ['apps/openhouse-installer/menu.cpp', 'apps/openhouse-installer/menu_dialogs.cpp',
            '-lgdi32', '-lole32', '-luuid']
    run(compiler + ['-municode', 'apps/openhouse-installer/main.cpp'] +
        actions + discovery + menu + shutdown +
        ['-o', str(OUT / 'openhouse-installer.exe')], 'compile-installer')
    run([str(OUT / 'openhouse-installer.exe'), '--unknown'], 'installer-refuses-unknown-option', 1)
    for name in ['scripts/check-personal.py', 'scripts/package-windows.py']:
        ast.parse((ROOT / name).read_text(encoding='utf-8'), filename=name)
    print('PASS: Python syntax')
    print('NOT RUN: full driver/viewer build, GoogleTest integration tests, or VR hardware.')


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, subprocess.SubprocessError) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(1)
