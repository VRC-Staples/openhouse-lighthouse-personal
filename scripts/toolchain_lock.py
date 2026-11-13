#!/usr/bin/env python3
"""Check local manifest and overlay integrity without downloading or installing anything."""
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent


def text_hash(path):
    return hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest()


def check_overlay(root, lock):
    overlays = lock['overlay_ports']
    if set(overlays) != {'zlib'}:
        raise ValueError('The lock must select only the reviewed zlib overlay.')
    overlay = overlays['zlib']
    if overlay['path'] != 'cmake/ports/zlib':
        raise ValueError('The zlib overlay path changed.')
    directory = root / overlay['path']
    paths = list(directory.rglob('*'))
    if directory.is_symlink() or any(path.is_symlink() for path in paths):
        raise ValueError('The zlib overlay must not contain symbolic links.')
    actual = {path.relative_to(directory).as_posix() for path in paths if path.is_file()}
    if actual != set(overlay['files_sha256']):
        raise ValueError('The zlib overlay file list changed.')
    for name, expected in overlay['files_sha256'].items():
        if text_hash(directory / name) != expected:
            raise ValueError(f'The zlib overlay integrity check failed: {name}')
    metadata = json.loads((directory / 'vcpkg.json').read_text(encoding='utf-8'))
    port = lock['ports']['zlib']
    if (metadata['version'] != port['version'] or
            metadata.get('port-version', 0) != port['port_version']):
        raise ValueError('The zlib overlay version does not match the lock.')
    recipe = (directory / 'portfile.cmake').read_text(encoding='utf-8')
    if f'SHA512 {port["source_sha512"]}' not in recipe or port['source_url'] not in recipe:
        raise ValueError('The zlib overlay source does not match the lock.')


def read_lock(root=ROOT):
    root = Path(root)
    lock = json.loads((root / 'scripts/ci/toolchain-lock.json').read_text(encoding='utf-8'))
    manifest_path = root / 'vcpkg.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if (manifest['builtin-baseline'] != lock['vcpkg_commit'] or
            text_hash(manifest_path) != lock['manifest_sha256']):
        raise ValueError('The dependency manifest does not match the lock.')
    configuration = root / 'vcpkg-configuration.json'
    if text_hash(configuration) != lock['configuration_sha256']:
        raise ValueError('The vcpkg configuration does not match the lock.')
    if json.loads(configuration.read_text(encoding='utf-8')) != {'overlay-ports': ['cmake/ports']}:
        raise ValueError('The vcpkg configuration must select the reviewed overlay directory.')
    if {path.name for path in (root / 'cmake/ports').iterdir()} != {'zlib'}:
        raise ValueError('The overlay directory must contain only zlib.')
    check_overlay(root, lock)
    return lock


if __name__ == '__main__':
    try:
        read_lock()
    except (OSError, KeyError, TypeError, ValueError) as error:
        print(f'Dependency lock check failed: {error}', file=sys.stderr)
        sys.exit(1)
    print('Dependency manifest, configuration, and zlib overlay match the lock.')
