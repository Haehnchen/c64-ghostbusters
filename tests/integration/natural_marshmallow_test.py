#!/usr/bin/env python3
"""Natural shop, failed catch/HQ, alarm maturation and both Marshmallow outcomes."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from natural_playthrough_test import require
from natural_campaign_test import frame_count


def check_trace(frames, baited, check_final_movement=True):
    require([f['frame'] for f in frames] == list(range(len(frames))), 'Lost/reordered trace frames')
    city = next(f for f in frames if f['state'] == 18)
    require(city['balance'] == [0, 0x66, 0] and city['owned'] == 9 and city['bait'] == 5,
            'Detector, bait and trap were not bought through the shop')
    require(city['empty_traps'] == 1 and city['vehicle'] == 0, 'Wrong vehicle/trap purchase')
    require(city['buildings'][7] == 31 and city['buildings'][8] == 28 and city['buildings'][15] == 20,
            'Missing natural startup hauntings')
    hq = next(f for f in frames if f['state'] == 35)
    miss = [f for f in frames[:hq['frame']] if f['state'] == 29 and f['speech']]
    require(bool(miss) and all(f['speech_command'] == 2 and f['building'] == 7 for f in miss),
            'Missing natural missed-catch speech at building 7')
    require(miss[-1]['frame'] - miss[0]['frame'] + 1 == len(miss), 'Interrupted/repeated missed-catch speech')
    miss_return = next(f for f in frames[miss[-1]['frame']:] if f['state'] == 18)
    require(miss_return['buildings'][7] == 0 and miss_return['backup_men'] == 2,
            'Missed catch did not clear building 7 or lose one person')
    require(hq['backup_men'] == 3 and hq['empty_traps'] == 1 and hq['bait'] == 5,
            'HQ did not restore personnel/trap or retained the wrong bait stock')
    alert = next(f for f in frames if f.get('pending_alert', 0))
    building = alert['pending_alert']
    require(building == 5 and 0x5000 <= alert['pk'] < 0x6000 and alert['buildings'][building] == 0xC8,
            'First alert was not naturally scheduled at PK 5000')
    start = next(f for f in frames if f['state'] == 36)
    maturation = frames[alert['frame']:start['frame']]
    ages = []
    for f in maturation:
        point = (f['pending_alert'], f['buildings'][building])
        if not ages or ages[-1] != point:
            ages.append(point)
    require(ages == [(building, 0xC8), (building, 0xD8), (building, 0xE8)],
            f'Alarm was replaced or aged out of order: {ages}')
    require(any(f['state'] == 35 for f in maturation), 'Alarm route did not actually visit HQ')
    require(start['frame09'] == 0 and start['buildings'][building] == 0,
            'Mature alarm was not consumed at its natural city update')
    returned = next(f for f in frames if f['frame'] > start['frame'] and f['state'] == 18)
    scene = frames[start['frame']:returned['frame']]
    states = []
    for f in scene:
        if not states or states[-1] != f['state']:
            states.append(f['state'])
    require(states == [36, 38 if baited else 37, 39], f'Wrong complete Marshmallow path: {states}')
    require(returned['balance'] == [0, 0x86 if baited else 0x26, 0], 'Wrong Marshmallow reward/damage')
    require(returned['bait'] == (4 if baited else 5) and returned['bait_active'] == 0,
            'Wrong bait consumption or unfinished bait state')
    expected_map = city['map_types'].copy()
    if not baited:
        expected_map[building] = 0
    require(returned['map_types'] == expected_map, 'Wrong destroyed/preserved city buildings')
    used = [f for f in scene if f['bait_active']]
    require(bool(used) == baited, 'Missing/unexpected active bait during approach')
    if baited:
        require(used[0]['sampled_key17'] == 0x42 and used[0]['state'] == 36,
                'Bait must come from a real B key during the approach')
    require(all(f['city_music'] and not f['speech'] for f in scene),
            'Marshmallow interrupted music or added unprescribed speech')
    require(scene[-1]['audio_energy'] > scene[0]['audio_energy'] and
            scene[-1]['sid_writes'] > scene[0]['sid_writes'], 'Silent Marshmallow music')
    require(all(b['audio_samples'] > a['audio_samples'] for a, b in zip(frames, frames[1:])),
            'Audio synthesis stalled')
    last = frames[-1]
    if check_final_movement:
        require(last['state'] == 18 and last['player_x'] == 55 and last['player_y'] == 186,
                'City control did not resume after Marshmallow')
    return building, states


def main():
    executable, inputs = (Path(arg).resolve() for arg in sys.argv[1:3])
    baited = sys.argv[3] == 'bait'
    schedule = inputs.read_text() + ('\n2 B\n' if baited else '\n2\n') + '1200\n16 Left\n2\n'
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', SDL_AUDIODRIVER='dummy')
    with tempfile.TemporaryDirectory(prefix='ghostbusters-marshmallow-') as directory:
        root = Path(directory)
        plan, trace = root / 'plan.inputs', root / 'trace.jsonl'
        plan.write_text(schedule)
        subprocess.run([str(executable), '--replay-input', str(plan), str(trace)],
                       cwd=root, env=env, check=True, timeout=480)
        frames = [json.loads(line) for line in trace.read_text().splitlines()]
        require(len(frames) == frame_count(schedule), 'Incomplete natural Marshmallow replay')
        result = check_trace(frames, baited)
        start_frame = next(f['frame'] for f in frames if f['state'] == 36)
        mutations = [('pending_alert', 0), ('bait', 0), ('city_music', False),
                     ('map_types', []), ('balance', [0, 0, 0]), ('state', 18)]
        if baited:
            mutations.append(('sampled_key17', 0))
        for field, value in mutations:
            try:
                check_trace([dict(f, **{field: value}) if field == 'pending_alert' or f['frame'] >= start_frame
                             else f for f in frames], baited)
            except (AssertionError, StopIteration):
                pass
            else:
                raise AssertionError(f'Negative control not detected: {field}')
        print(f'PASS: natural Marshmallow {sys.argv[3]}, building/path {result}, {len(frames)} frames')


if __name__ == '__main__':
    main()
