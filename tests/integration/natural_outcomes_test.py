#!/usr/bin/env python3
"""Natural trap outcome, spoken result, city return, and HQ replenishment."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from natural_playthrough_test import require


VEHICLES = {
    'compact': (0, 0x70, 0x74, 1, 1, 0, 0x60),
    'hearse': (1, 0x36, 0x40, 2, 1, 0, 0x70),
    'wagon': (2, 0x26, 0x30, 1, 9, 5, 0x80),
    'high_performance': (3, 0, 0x05, 1, 0, 0, 0xA0),
    'laser': (0, 0x50, 0x53, 1, 0x40, 0, 0x60),
}


def check_trace(frames, outcome, vehicle='compact'):
    vehicle_id, initial_money, capture_money, traps, owned, bait, speed = VEHICLES[vehicle]
    states = []
    for frame in frames:
        if not states or states[-1] != frame['state']:
            states.append(frame['state'])
    capture = outcome == 'capture'
    middle = [30, 31, 32, 33] if capture else [29]
    expected = [255, 0, 1, 2, 6, 15, 16, 17, 18, 19, 20, 21, 22, 23,
                24, 25, 26, 27, 28] + middle + [17, 18, 19, 20, 21, 22, 23, 34, 35, 17, 18]
    cursor = iter(states)
    require(all(any(s == wanted for s in cursor) for wanted in expected),
            f'Missing natural {outcome}/HQ transition: {states}')
    city = next(f for f in frames if f['state'] == 18)
    require(city['name'][:3] == [65, 66, 0] and city['vehicle'] == vehicle_id and city['owned'] == owned,
            'Natural account/vehicle/equipment input failed')
    require(city['balance'] == [0, initial_money, 0] and city['empty_traps'] == traps and city['bait'] == bait,
            'Wrong initial inventory or purchase costs')
    require(city['backup_men'] == 3 and city['backpack_charge'] == 0x99,
            'Wrong initial personnel or charge')
    require(city['buildings'][7] == 31 and city['buildings'][8] == 28 and city['buildings'][15] == 20,
            'Natural startup hauntings missing')
    trap = next(f for f in frames if f['state'] == 28)
    require(trap['building'] == 7, 'Wrong capture building')
    returned = next(f for f in frames if f['frame'] > trap['frame'] and f['state'] == 18)
    balance = [0, capture_money if capture else initial_money, 0]
    consumed_trap = capture and not (owned & 0x40)
    require(returned['balance'] == balance and returned['empty_traps'] == traps - int(consumed_trap),
            'Capture award or trap consumption incorrect')
    require(returned['backup_men'] == (3 if capture else 2), 'Wrong personnel loss')
    if not capture:
        require(returned['backpack_charge'] < city['backpack_charge'],
                'Missed-trap route must consume charge before HQ refills it')
    require(returned['buildings'][7] == 0, 'Haunting survived completed attempt')
    speech = [f for f in frames if trap['frame'] <= f['frame'] < returned['frame'] and f['speech']]
    require(bool(speech), 'Missing spoken capture result')
    require(all(f['speech_command'] == (1 if capture else 2) for f in speech), 'Wrong speech asset')
    require(speech[-1]['frame'] - speech[0]['frame'] + 1 == len(speech), 'Repeated/interrupted result speech')
    require(any(f['effect'] for f in frames if trap['frame'] <= f['frame'] < speech[0]['frame']),
            'Missing active voice-3 effect between beam shutdown and speech')
    if capture:
        require(all(f['sprite_pointers'][4] == 0 for f in frames
                    if speech[0]['frame'] <= f['frame'] and f['state'] in (31, 32, 33)),
                'Captured ghost reappeared during return')
    hq = next(f for f in frames if f['state'] == 35)
    require(hq['building'] == 17 and hq['empty_traps'] == traps and hq['backup_men'] == 3
            and hq['backpack_charge'] == 0x99, 'HQ failed to replenish traps/personnel/charge')
    last = frames[-1]
    require(last['state'] == 18 and last['balance'] == balance and last['empty_traps'] == traps,
            'HQ did not return to city with preserved balance')
    require(last['player_x'] == 55 and last['player_y'] == 186, 'City did not accept movement after HQ')
    require(last['city_music'] and not last['speech'], 'Audio did not resume after HQ')
    for before, after in zip(frames, frames[1:]):
        require(after['audio_samples'] > before['audio_samples'], 'Audio synthesis stalled')
    require(speech[-1]['audio_energy'] > speech[0]['audio_energy'], 'Silent result speech')
    if vehicle != 'compact':
        require([f['frame'] for f in frames] == list(range(len(frames))), 'Missing vehicle replay frames')
        require(all(f['owned'] == owned and f['bait'] == bait for f in (returned, hq, last)),
                'Capture/HQ changed unrelated purchased equipment')
        drives = [f for f in frames if f['state'] == 21]
        require(drives and all(f['drive_vehicle'] == vehicle_id for f in drives), 'Driving changed vehicle')
        require(max(f['drive_speed'] for f in drives) == speed and
                all(0 <= f['drive_speed'] <= speed for f in drives), 'Wrong vehicle speed limit')
        require(any(f['drive_distance'] > 0 for f in drives), 'No actual drive distance travelled')
        require(all(f['city_music'] for f in drives) and drives[-1]['audio_energy'] > drives[0]['audio_energy'],
                'Driving lost its music')
    return states


def main():
    executable, schedule = (Path(arg).resolve() for arg in sys.argv[1:3])
    outcome = sys.argv[3]
    vehicle = sys.argv[4] if len(sys.argv) > 4 else 'compact'
    expected_frames = sum(int(line.split('#', 1)[0].split()[0])
                          for line in schedule.read_text().splitlines()
                          if line.split('#', 1)[0].strip())
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', SDL_AUDIODRIVER='dummy')
    with tempfile.TemporaryDirectory(prefix='ghostbusters-outcome-') as directory:
        trace = Path(directory) / 'trace.jsonl'
        subprocess.run([str(executable), '--replay-input', str(schedule), str(trace)],
                       cwd=directory, env=env, check=True, timeout=300)
        frames = [json.loads(line) for line in trace.read_text().splitlines()]
        require(len(frames) == expected_frames, 'Input sequence ended early')
        states = check_trace(frames, outcome, vehicle)
        broken_traces = [[f for f in frames if f['state'] != 35],
                       [dict(f, speech_command=-1) for f in frames],
                       [dict(f, empty_traps=0) for f in frames]]
        if vehicle != 'compact':
            broken_traces += [[dict(f, drive_speed=0) for f in frames],
                              [dict(f, drive_vehicle=255) for f in frames],
                              [dict(f, owned=255) if f['state'] == 35 else f for f in frames]]
        for broken in broken_traces:
            try:
                check_trace(broken, outcome, vehicle)
            except AssertionError:
                pass
            else:
                raise AssertionError('Negative control not detected')
        print(f'PASS: natural {vehicle}/{outcome}, result speech, HQ and city control; {len(frames)} frames; {states}')


if __name__ == '__main__':
    main()
