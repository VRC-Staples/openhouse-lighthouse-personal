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
    compiler = [zig, 'c++', '-target', 'x86_64-windows-gnu', '-std=c++20', '-O0',
                '-g', '-Wall', '-Wextra', '-Isrc', '-Iapps/openhouse-installer']
    run(compiler + ['tests/personal/test_safety.cpp',
                    'src/transport/usb/wireless_decoder.cpp', 'src/common/net.cpp',
                    '-lws2_32', '-liphlpapi', '-ladvapi32',
                    '-o', str(OUT / 'personal-safety.exe')], 'compile-safety')
    run([str(OUT / 'personal-safety.exe')], 'test-safety')
    run(compiler + ['-municode', 'apps/openhouse-installer/main.cpp',
                    '-o', str(OUT / 'openhouse-installer.exe')], 'compile-installer')
    run([str(OUT / 'openhouse-installer.exe')], 'installer-refuses-missing-arguments', 1)
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
