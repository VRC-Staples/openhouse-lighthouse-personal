#!/usr/bin/env python3
"""Check replay exit status using temporary synthetic captures, never live input."""
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable = str(Path(sys.argv[1]).resolve())
    cases = [('complete', b'LHRAW001', 0, ''),
             ('bad-record', b'LHRAW001\xff', 1, 'corrupt or unreadable'),
             ('bad-header', b'bad-header', 1, 'cannot open capture')]
    with tempfile.TemporaryDirectory(prefix='openhouse-replay-test-') as temporary:
        for name, content, code, message in cases:
            capture = Path(temporary) / (name + '.lhraw')
            capture.write_bytes(content)
            result = subprocess.run(
                [executable, 'session', str(capture), '--steam-db', '-', '--preinit', '-'],
                cwd=temporary, capture_output=True, text=True, timeout=15)
            if result.returncode != code or message not in result.stderr:
                raise RuntimeError(f'{name}: exit {result.returncode}: {result.stdout}{result.stderr}')
            print(f'PASS: {name}, exit {code}')


if __name__ == '__main__':
    main()
