#!/usr/bin/env python3
"""Build/test a fresh copy without reference, analysis data or documentation."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]


def main():
    reports = ROOT / 'build/test-results'
    reports.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    report = Path(tempfile.mkdtemp(prefix=f'{stamp}-standalone-', dir=reports))
    print(f'Standalone reports: {report}', flush=True)

    def run(command, **kwargs):
        # Keep build failures and test evidence outside the disposable source tree.
        with (report / 'standalone.log').open('a') as log:
            log.write(f'\nRunning: {command!r}\n')
            log.flush()
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                           check=True, **kwargs)

    with tempfile.TemporaryDirectory(prefix='ghostbusters-standalone-') as directory:
        root = Path(directory)
        source = root / 'source'
        source.mkdir()
        for name in ('CMakeLists.txt', 'Makefile'):
            shutil.copy2(ROOT / name, source / name)
        for name in ('src', 'assets', 'tests', 'tools', 'cmake'):
            shutil.copytree(ROOT / name, source / name,
                            ignore=shutil.ignore_patterns('__pycache__'))
        assert not (source / 'reference').exists() and not (source / 'analysis').exists()
        assert not (source / 'docs').exists()
        # A new SID dependency build is intentional: no original build artifacts
        # or absolute paths into the working repository are reused.
        # Packaging independence is separate from long natural-path acceptance.
        run(['make', 'check', f'TEST_REPORT_DIR={report}'], cwd=source)
        run(['make', 'doctor'], cwd=source)
        empty = root / 'empty'
        empty.mkdir()
        executable = empty / 'ghostbusters'
        shutil.copy2(source / 'build/ghostbusters', executable)
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software',
                   SDL_AUDIODRIVER='dummy')
        run([str(executable), '--smoke-test'], cwd=empty, env=env)
        for scene in ('title', 'city', 'drive-controls', 'catch', 'ending-success'):
            run([str(executable), '--dump-' + scene, str(root / scene)],
                cwd=empty, env=env)
        print('PASS: clean build/tests without reference, analysis or docs; standalone binary and scene exports')


if __name__ == '__main__':
    main()
