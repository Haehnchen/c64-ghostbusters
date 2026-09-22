#!/usr/bin/env python3
"""Play-from hands a naturally reached scene to the live runtime."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def stop(process):
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=1)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=1)


def expect_failure(command, cwd, env, description):
    result = subprocess.run(command, cwd=cwd, env=env, stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE, text=True, timeout=2)
    require(result.returncode != 0, description)


def check_handoff(executable, schedule_text, scene, expected_state, timeout=7):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software',
               SDL_AUDIODRIVER='dummy')
    with tempfile.TemporaryDirectory(prefix='ghostbusters-play-from-') as directory:
        root = Path(directory)
        plan = root / 'route.inputs'
        trace = root / 'trace.jsonl'
        plan.write_text(schedule_text)
        command = [str(executable), '--play-from', str(plan), scene,
                   '--trace-input', str(trace)]
        process = subprocess.Popen(command, cwd=root, env=env, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL)
        reader = None
        pending = ''
        saw_replay = False
        last_replay = None
        replay_returned = False
        live = []

        def read_new_rows():
            nonlocal reader, pending, saw_replay, last_replay, replay_returned
            if reader is None:
                if not trace.exists():
                    return
                reader = trace.open()
            pending += reader.read()
            complete = pending.split('\n')
            pending = complete.pop()
            for line in complete:
                row = json.loads(line)
                if row.get('replay_active') is True:
                    if live:
                        replay_returned = True
                    saw_replay = True
                    last_replay = row
                elif row.get('replay_active') is False:
                    live.append(row)

        try:
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                require(process.poll() is None,
                        f'Play-from exited before handing off {scene}')
                read_new_rows()
                if len(live) >= 6:
                    break
                time.sleep(0.01)
            else:
                raise AssertionError(f'Play-from did not produce six live {scene} frames')

            require(process.poll() is None, 'Play-from was not alive at the live checkpoint')
            first_live_count = len(live)
            first_checkpoint = time.monotonic()
            while time.monotonic() < deadline:
                read_new_rows()
                if len(live) > first_live_count:
                    break
                require(process.poll() is None, 'Play-from stopped during live rendering')
                time.sleep(0.01)
            else:
                raise AssertionError('Live trace stopped advancing in wall time')

            require(time.monotonic() > first_checkpoint and process.poll() is None,
                    'Play-from did not remain alive after the schedule')
            require(saw_replay,
                    'Play-from did not trace its accelerated natural route')
            require(last_replay['state'] == expected_state,
                    f'{scene} handoff did not occur in the requested scene')
            require(not replay_returned, 'Input replay resumed after the live handoff')
            require(all(after['audio_samples'] > before['audio_samples']
                        for before, after in zip(live, live[1:])),
                    'Audio samples stopped advancing after replay handoff')
        finally:
            if reader is not None:
                reader.close()
            stop(process)
        return live


def main():
    project = Path(__file__).resolve().parents[2]
    executable = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else \
        project / 'build/ghostbusters'
    require(len(sys.argv) <= 2, 'Usage: play_from_test.py [EXECUTABLE]')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software',
               SDL_AUDIODRIVER='dummy')

    live = check_handoff(executable, '1\n', 'title', 255)
    require(all(row['state'] == 255 for row in live),
            'Title play-from left the requested scene')

    with tempfile.TemporaryDirectory(prefix='ghostbusters-play-from-errors-') as directory:
        root = Path(directory)
        plan = root / 'one-frame.inputs'
        plan.write_text('1\n')
        expect_failure([str(executable), '--play-from', str(plan), 'unknown'], root, env,
                       'An unknown play-from scene was accepted')
        expect_failure([str(executable), '--play-from', str(plan), 'dialog'], root, env,
                       'A route ending before the requested scene was accepted')

    print(f'PASS: play-from title stayed live for {len(live)} traced frames')


if __name__ == '__main__':
    main()
