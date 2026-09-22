#!/usr/bin/env python3
"""Earn a laser, capture a natural haunting into storage, then empty it at HQ."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from natural_campaign_test import frame_count
from natural_outcomes_test import check_trace as check_capture
from natural_playthrough_test import require
from natural_zuul_test import check_trace as check_earned_purchase


def check_trace(frames, purchase_frames):
    check_earned_purchase(frames[:purchase_frames], 'victory')
    fresh = [f for f in frames if f['generation'] == 1]
    start = fresh[0]['frame']
    check_capture([dict(f, frame=f['frame'] - start) for f in fresh], 'capture', 'laser')
    city = next(f for f in fresh if f['state'] == 18)
    require(city['purchase_count'] == 2 and city['traps'] == 1 and city['full_traps'] == 0,
            'Fresh laser/trap purchase had wrong count or stale storage')
    transitions = [(a, b) for a, b in zip(fresh, fresh[1:]) if a['full_traps'] != b['full_traps']]
    require(len(transitions) == 2, 'Laser storage changed more or less than capture and HQ')
    before, after = transitions[0]
    require(before['state'] == after['state'] == 31 and
            before['full_traps'] == 0 and after['full_traps'] == 1 and
            before['empty_traps'] == after['empty_traps'] == 1,
            'Laser failed to store the ghost without consuming the regular trap')
    # Independently pinned award table, indexed by the naturally aged haunt.
    awards = (10, 10, 9, 9, 8, 8, 7, 7, 6, 6, 5, 5, 4, 4, 3, 3)
    require(before['buildings'][7] & 15 == 15 and awards[before['buildings'][7] >> 4] == 3 and
            after['buildings'][7] == 0 and before['balance'] == [0, 0x50, 0] and
            after['balance'] == [0, 0x53, 0], 'Storage capture did not preserve the normal $300 award')
    before_hq, hq = transitions[1]
    require(before_hq['state'] == 34 and hq['state'] == 35 and
            before_hq['full_traps'] == 1 and hq['full_traps'] == 0 and
            hq['empty_traps'] == 1 and hq['balance'] == [0, 0x53, 0],
            'HQ did not empty the laser while preserving trap and earnings')
    require(frames[-1]['full_traps'] == 0 and
            all(b['frame'] == a['frame'] + 1 for a, b in zip(frames, frames[1:])),
            'Storage leaked after HQ or campaign trace has gaps')


def main():
    executable, prefix, purchase, tail = (Path(arg).resolve() for arg in sys.argv[1:5])
    first = prefix.read_text() + '\n' + purchase.read_text()
    schedule = first + '\n' + tail.read_text()
    count = frame_count(first)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', SDL_AUDIODRIVER='dummy')
    with tempfile.TemporaryDirectory(prefix='ghostbusters-laser-') as directory:
        root = Path(directory)
        plan, trace = root / 'plan.inputs', root / 'trace.jsonl'
        plan.write_text(schedule)
        subprocess.run([str(executable), '--replay-input', str(plan), str(trace)],
                       cwd=root, env=env, check=True, timeout=720)
        frames = [json.loads(line) for line in trace.read_text().splitlines()]
        require(len(frames) == frame_count(schedule), 'Incomplete earned-laser campaign')
        check_trace(frames, count)
        for mutate in (
            lambda f: dict(f, full_traps=0),
            lambda f: dict(f, empty_traps=0) if f['generation'] == 1 and f['state'] == 31 else f,
            lambda f: dict(f, full_traps=1) if f['generation'] == 1 and f['state'] == 35 else f,
            lambda f: dict(f, speech_command=-1) if f['generation'] == 1 else f,
            lambda f: dict(f, owned=0) if f['generation'] == 1 else f,
            lambda f: dict(f, balance=[0, 0x54, 0]) if f['generation'] == 1 and f['state'] == 31 else f,
        ):
            try:
                check_trace([mutate(f) for f in frames], count)
            except (AssertionError, StopIteration):
                pass
            else:
                raise AssertionError('Laser negative control not detected')
        print(f'PASS: earned laser, natural capture/storage and HQ emptying; {len(frames)} frames')


if __name__ == '__main__':
    main()
