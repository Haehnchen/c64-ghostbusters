#!/usr/bin/env python3
"""Run bounded CTest profiles and retain independent reports for every invocation."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

IS_WINDOWS = os.name == 'nt'


def process_options():
    if IS_WINDOWS:
        return {'creationflags': getattr(subprocess, 'CREATE_NEW_PROCESS_GROUP', 0x00000200)}
    return {'start_new_session': True}


def stop_process_tree(process):
    if IS_WINDOWS:
        subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       check=False)
    else:
        os.killpg(process.pid, signal.SIGKILL)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build'))
    parser.add_argument('--reports', type=Path, default=Path('build/test-results'))
    parser.add_argument('--suite', choices=('quick', 'extended', 'all'), default='quick')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--match', help='Run only test names matching this CTest regular expression')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    args.reports.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    report = Path(tempfile.mkdtemp(prefix=f'{stamp}-{args.suite}-', dir=args.reports)).resolve()
    command = ['ctest', '--test-dir', str(args.build_dir.resolve()),
               '--output-on-failure', '--no-tests=error', '--parallel', str(args.jobs)]
    if args.suite != 'all':
        command += ['--label-regex', f'^{args.suite}$']
    if args.match:
        command += ['--tests-regex', args.match]
    # This budget excludes configuration/compilation. A process group also stops
    # replay children on timeout; otherwise they could keep consuming CI workers.
    budget = {'quick': 120, 'extended': 900, 'all': 1800}[args.suite]
    started = time.monotonic()
    code = 1
    timed_out = False
    print(f'Running {args.suite} tests; budget {budget}s; reports: {report}', flush=True)
    try:
        with (report / 'ctest.log').open('w') as log:
            with subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                  **process_options()) as process:
                try:
                    code = process.wait(timeout=budget)
                except (subprocess.TimeoutExpired, KeyboardInterrupt) as error:
                    stop_process_tree(process)
                    process.wait()
                    timed_out = isinstance(error, subprocess.TimeoutExpired)
                    code = 124 if timed_out else 130
    finally:
        elapsed = round(time.monotonic() - started, 2)
        (report / 'summary.json').write_text(json.dumps({
            'suite': args.suite, 'command': command, 'exit_code': code,
            'elapsed_seconds': elapsed, 'budget_seconds': budget,
            'timed_out': timed_out,
        }, indent=2) + '\n')
    print((report / 'ctest.log').read_text(), end='')
    print(f'Test result: exit {code}, {elapsed}s; reports: {report}')
    return code


if __name__ == '__main__':
    raise SystemExit(main())
