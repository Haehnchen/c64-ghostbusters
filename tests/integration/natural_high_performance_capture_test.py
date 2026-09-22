#!/usr/bin/env python3
"""Earn the expensive car, then buy a trap and play a natural capture/HQ route."""
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
    check_earned_purchase(frames[:purchase_frames], 'victory', 3)
    fresh = [f for f in frames if f['generation'] == 1]
    start = fresh[0]['frame']
    check_capture([dict(f, frame=f['frame'] - start) for f in fresh], 'capture', 'high_performance')
    city = next(f for f in fresh if f['state'] == 18)
    require(city['purchase_count'] == 1 and city['traps'] == 1,
            'The remaining $600 did not buy exactly one trap')
    awards = [(a, b) for a, b in zip(fresh, fresh[1:])
              if a['state'] == 31 and a['balance'] != b['balance']]
    require(len(awards) == 1, 'Missing or repeated capture reward')
    before, after = awards[0]
    # Haunt age B indexes the original award table at B: BCD 05 ($500).
    # This faster route catches it earlier than the $400 vehicle fixtures.
    require(before['buildings'][7] == 0xBF and before['balance'] == [0, 0, 0] and
            after['buildings'][7] == 0 and after['balance'] == [0, 5, 0],
            'Capture reward does not match the naturally aged haunting')
    require(all(b['frame'] == a['frame'] + 1 for a, b in zip(frames, frames[1:])),
            'Missing frames across account reuse and capture')


def main():
    executable, prefix, purchase, capture = (Path(arg).resolve() for arg in sys.argv[1:5])
    first = prefix.read_text() + '\n' + purchase.read_text()
    schedule = first + '\n' + capture.read_text()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', SDL_AUDIODRIVER='dummy')
    with tempfile.TemporaryDirectory(prefix='ghostbusters-highperf-') as directory:
        root = Path(directory)
        plan, trace = root / 'plan.inputs', root / 'trace.jsonl'
        plan.write_text(schedule)
        subprocess.run([str(executable), '--replay-input', str(plan), str(trace)],
                       cwd=root, env=env, check=True, timeout=720)
        frames = [json.loads(line) for line in trace.read_text().splitlines()]
        require(len(frames) == frame_count(schedule), 'Incomplete high-performance campaign')
        check_trace(frames, frame_count(first))
        for mutate in (
            lambda f: dict(f, balance=[0, 0, 0]) if f['generation'] == 1 and f['state'] == 6 else f,
            lambda f: dict(f, owned=1) if f['generation'] == 1 and f['state'] == 18 else f,
            lambda f: dict(f, drive_speed=0) if f['generation'] == 1 else f,
            lambda f: dict(f, speech_command=-1) if f['generation'] == 1 and f['state'] in (30, 31) else f,
            lambda f: dict(f, empty_traps=0) if f['generation'] == 1 and f['state'] == 35 else f,
            lambda f: dict(f, buildings=[0] * 20) if f['generation'] == 1 and f['state'] == 31 else f,
        ):
            try:
                check_trace([mutate(f) for f in frames], frame_count(first))
            except (AssertionError, StopIteration):
                pass
            else:
                raise AssertionError('High-performance negative control not detected')
        print(f'PASS: earned High-Performance, zero-budget trap purchase, capture and HQ; {len(frames)} frames')


if __name__ == '__main__':
    main()
