#!/usr/bin/env python3
"""Compare isolated GLFW startup with and without the viewer's DLL restrictions."""
import subprocess
import sys

NO_OPENGL = 'GLFW_CONTEXT_UNAVAILABLE:65542:WGL: The driver does not appear to support OpenGL'


def assess(baseline, hardened):
    if baseline.returncode == hardened.returncode == 0:
        return 'PASS: baseline and hardened GLFW windows and OpenGL rendering'
    if (baseline.returncode == hardened.returncode == 77 and
            baseline.stderr.strip() == hardened.stderr.strip() == NO_OPENGL):
        return ('PASS: baseline and hardened native GLFW windows. '
                'RENDERING NOT VALIDATED: both report that the runner lacks OpenGL support.')
    raise RuntimeError(f'GLFW startup differs or failed unexpectedly:\n'
                       f'baseline={baseline.returncode}: {baseline.stdout}{baseline.stderr}\n'
                       f'hardened={hardened.returncode}: {hardened.stdout}{hardened.stderr}')


def self_test():
    def result(code, error=''):
        return subprocess.CompletedProcess([], code, stdout='', stderr=error)
    assert 'rendering' in assess(result(0), result(0))
    assert 'NOT VALIDATED' in assess(result(77, NO_OPENGL), result(77, NO_OPENGL))
    for baseline, hardened in [(result(0), result(77, NO_OPENGL)),
                               (result(1), result(1)),
                               (result(77, 'other'), result(77, 'other')),
                               (result(77, NO_OPENGL), result(0))]:
        try:
            assess(baseline, hardened)
        except RuntimeError:
            continue
        raise AssertionError('startup failure was incorrectly accepted')
    print('PASS: startup comparison rejects regressions and unknown failures')


def main():
    if sys.argv[1:] == ['--self-test']:
        self_test()
        return
    if len(sys.argv) != 3:
        raise SystemExit('Usage: check-viewer-startup.py BASELINE_EXE HARDENED_EXE')
    results = [subprocess.run([exe], capture_output=True, text=True, timeout=15)
               for exe in sys.argv[1:]]
    print(assess(*results))
    for label, result in zip(('baseline', 'hardened'), results):
        print(f'{label}: {(result.stdout + result.stderr).strip()}')


if __name__ == '__main__':
    main()
