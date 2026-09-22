#!/usr/bin/env python3
"""Exercise the live dispatcher from startup using held host keys only."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def check_trace(frames):
    states = []
    for frame in frames:
        if not states or frame['state'] != states[-1]:
            states.append(frame['state'])
    expected = [255, 0, 1, 2, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
                19, 20, 21, 22, 23, 24, 25, 26, 27, 29, 17, 18]
    cursor = iter(states)
    require(all(any(state == wanted for state in cursor) for wanted in expected),
            f'Missing connected startup transition: {states}')
    require(any(f['name'][:3] == [65, 66, 0] for f in frames), 'Name AB was not entered')
    city = next(f for f in frames if f['state'] == 18)
    require(city['vehicle'] == 0 and city['owned'] == 1, 'Wrong vehicle/detector purchase')
    require(city['balance'] == [0, 0x70, 0], 'Vehicle/equipment charges must leave $7000')
    require(city['traps'] == city['empty_traps'] == 1, 'Exactly one trap must be bought')
    expected_buildings = [0] * 20
    expected_buildings[7], expected_buildings[8], expected_buildings[15] = 31, 28, 20
    require(city['buildings'] == expected_buildings, 'Startup hauntings changed')
    require(city['city_music'] and not city['speech'], 'City audio handoff failed')
    placement = next(f for f in frames if f['state'] == 24)
    require(placement['building'] == 7 and placement['sprite_pointers'][4] != 0,
            'The natural haunted building was not reached')
    beams = next(f for f in frames if f['state'] == 27)
    require(beams['sprite_pointers'][5:7] == [0x16, 0x17], 'Busters must face inward')
    # Crossed streams clears post_failure_ea7a; unlike a missed trap, this
    # failure returns without speech 2. Do not invent an extra audio trigger.
    failure = next(f for f in frames if f['state'] == 29)
    returned = next(f for f in frames if f['frame'] > failure['frame'] and f['state'] == 18)
    require(not any(f['speech'] for f in frames if failure['frame'] <= f['frame'] <= returned['frame']),
            'Crossed streams incorrectly triggered missed-trap speech')
    last = frames[-1]
    require(last['state'] == 18 and not last['speech'] and last['city_music'],
            'Failure did not return to the playable city')
    require(last['balance'] == city['balance'] and last['empty_traps'] == 1,
            'Crossed streams must not award money or consume a trap')
    require(returned['buildings'][7] == 0, 'Failed haunting must be cleared on return')
    require(last['player_x'] == 111 and last['player_y'] == 66,
            'Returned city did not accept the final Left input')
    require(any(f['speech'] for f in frames[:300]), 'Missing title speech')
    require(any(f['effect'] and f['state'] == 15 for f in frames), 'Missing shop motor effect')
    for state in (15, 18, 21, 27):
        section = [f for f in frames if f['state'] == state]
        require(section[-1]['audio_energy'] > section[0]['audio_energy'],
                f'No audio energy during scene {state}')
    for before, after in zip(frames, frames[1:]):
        require(after['audio_samples'] > before['audio_samples'],
                f'Audio stopped at frame {after["frame"]}')
        require(after['sid_writes'] >= before['sid_writes'], 'SID trace went backwards')
    require(frames[-1]['audio_energy'] > 1 and frames[-1]['sid_writes'] > 1000,
            'Synthesized audio is empty')
    return states


def main():
    executable = Path(sys.argv[1]).resolve()
    schedule = Path(sys.argv[2]).resolve()
    expected_frames = sum(int(line.split('#', 1)[0].split()[0])
                          for line in schedule.read_text().splitlines()
                          if line.split('#', 1)[0].strip())
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software',
               SDL_AUDIODRIVER='dummy')
    with tempfile.TemporaryDirectory(prefix='ghostbusters-natural-') as directory:
        root = Path(directory)
        trace = root / 'trace.jsonl'
        subprocess.run([str(executable), '--replay-input', str(schedule), str(trace)],
                       cwd=root, env=env, check=True, timeout=240)
        frames = [json.loads(line) for line in trace.read_text().splitlines()]
        require(len(frames) == expected_frames, 'Replay ended early')
        states = check_trace(frames)
        invalid_cli = subprocess.run([str(executable), '--unknown-option'],
                                     cwd=root, env=env, capture_output=True, timeout=5)
        require(invalid_cli.returncode == 1, 'Invalid CLI must fail instead of restarting forever')
        # Malformed schedules must not be silently accepted.
        for invalid in ('0', '1 UnknownReplayKey', '1 Left Left', '# empty'):
            path = root / 'invalid.inputs'
            path.write_text(invalid + '\n')
            result = subprocess.run([str(executable), '--replay-input', str(path), str(trace)],
                                    cwd=root, env=env, capture_output=True, timeout=10)
            require(result.returncode != 0, f'Invalid schedule accepted: {invalid}')
        # The checker itself must reject missing transitions and incorrect accounting.
        for broken in ([f for f in frames if f['state'] != 16],
                       [f for f in frames if f['state'] != 29],
                       [dict(f, balance=[0, 0, 0]) for f in frames]):
            try:
                check_trace(broken)
            except AssertionError:
                pass
            else:
                raise AssertionError('Negative control was not detected')
        print(f'PASS: {len(frames)} live input frames; connected states {states}; audio synthesized')


if __name__ == '__main__':
    main()
