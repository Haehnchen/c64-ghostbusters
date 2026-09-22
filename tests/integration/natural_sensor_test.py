#!/usr/bin/env python3
"""Buy/omit the sensor, reach a natural alarm and check the rendered color buffer."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from natural_campaign_test import frame_count
from natural_playthrough_test import require


def schedule_for(schedule, equipped):
    for action in ('pickup', 'buy'):
        marker = f'2 Space # {action}_sensor'
        require(schedule.count(marker) == 1, f'Missing unique sensor marker: {marker}')
        if not equipped:
            schedule = schedule.replace(marker, f'2 # omitted {action}_sensor')
    return schedule


def check_trace(frames, equipped):
    require([f['frame'] for f in frames] == list(range(len(frames))), 'Missing sensor frames')
    city = next(f for f in frames if f['state'] == 18)
    money = 0x66 if equipped else 0x74
    require(city['vehicle'] == 0 and city['owned'] == (4 if equipped else 0) and
            city['balance'] == [0, money, 0] and city['empty_traps'] == 1 and
            city['purchase_count'] == (2 if equipped else 1), 'Wrong regular sensor/trap purchase')
    alert = next(f for f in frames if f.get('pending_alert'))
    building = alert['pending_alert']
    require(0x5000 <= alert['pk'] < 0x6000 and alert['buildings'][building] == 0xC8,
            'Alarm was not naturally scheduled at PK 5000')
    require(all(not f.get('pending_alert') for f in frames[:alert['frame']]),
            'Unexpected earlier alarm')
    start = next(f for f in frames if f['state'] == 36)
    require(start['frame'] > alert['frame'] and start['buildings'][building] == 0,
            'Natural alarm did not mature into the Marshmallow approach')
    returned = next(f for f in frames if f['state'] == 18 and f['frame'] > start['frame'])
    scene = frames[start['frame']:returned['frame']]
    states = []
    for f in scene:
        if not states or states[-1] != f['state']:
            states.append(f['state'])
    require(states == [36, 37, 39], 'Incomplete Marshmallow attack and return')
    expected_map = city['map_types'].copy()
    expected_map[building] = 0
    require(returned['map_types'] == expected_map and
            returned['balance'] == [0, 0x26 if equipped else 0x34, 0],
            'Wrong attacked building or damage amount')
    require(all(f['city_music'] and not f['speech'] for f in scene) and
            scene[-1]['audio_energy'] > scene[0]['audio_energy'],
            'Marshmallow music missing or unexpected speech')
    require(all(b['audio_samples'] > a['audio_samples'] for a, b in zip(frames, frames[1:])),
            'Sensor route audio stalled')
    last = frames[-1]
    require(last['state'] == 18 and last['player_x'] == 29 and last['player_y'] == 186 and
            last['player_x'] != returned['player_x'] and last['owned'] == city['owned'] and
            last['empty_traps'] == 1 and last['balance'] == returned['balance'],
            'Missing playable return with retained equipment')
    return building, alert


def check_display(equipped, unequipped):
    building, alert = check_trace(equipped, True)
    other_building, other_alert = check_trace(unequipped, False)
    require(building == other_building and alert['frame'] == other_alert['frame'],
            'Counterfactual did not reach the same natural alarm')
    marked = [(a, b) for a, b in zip(equipped, unequipped)
              if a['state'] == b['state'] == 18 and a['pending_alert'] == building and
              a['buildings'][building] & 3 == 2]
    require(len(marked) >= 8, 'Sensor alarm was never selected for a visible interval')
    require(all(a['buildings'][building] & 0xFC == b['buildings'][building] & 0xFC and
                b['buildings'][building] & 3 == 0 and
                (a['player_x'], a['player_y']) == (b['player_x'], b['player_y'])
                for a, b in marked), 'Sensor changed the alarm rather than revealing it')
    # CityFrame refreshes one quarter of the buildings per frame. Inspect
    # actual CharacterFrame color bytes after a complete refresh, not a
    # recomputed expected color derived from the status/owned bits.
    visible = [(a, b) for a, b in marked if a['frame'] >= marked[0][0]['frame'] + 4]
    require(visible and all(a['building_colors'][building] == 9 and
                           b['building_colors'][building] == 13 for a, b in visible),
            'Sensor alarm did not change the displayed building color')


def main():
    executable, source = (Path(arg).resolve() for arg in sys.argv[1:3])
    schedule = source.read_text()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software',
               SDL_AUDIODRIVER='dummy')
    results = []
    with tempfile.TemporaryDirectory(prefix='ghostbusters-sensor-') as directory:
        root = Path(directory)
        for equipped in (True, False):
            plan, trace = root / 'plan.inputs', root / 'trace.jsonl'
            plan.write_text(schedule_for(schedule, equipped))
            subprocess.run([str(executable), '--replay-input', str(plan), str(trace)],
                           cwd=root, env=env, check=True, timeout=480)
            frames = [json.loads(line) for line in trace.read_text().splitlines()]
            require(len(frames) == frame_count(schedule), 'Incomplete sensor replay')
            check_trace(frames, equipped)
            results.append(frames)
        check_display(*results)
        building = check_trace(results[0], True)[0]
        for field, value in (('owned', 0), ('balance', [0, 0x74, 0]),
                             ('pending_alert', 0),
                             ('building_colors', [13] * 20), ('city_music', False)):
            broken = [dict(f, **{field: value}) for f in results[0]]
            try:
                check_display(broken, results[1])
            except (AssertionError, StopIteration):
                pass
            else:
                raise AssertionError(f'Sensor negative control not detected: {field}')
        broken = [dict(f, buildings=[v | 2 if i == building else v
                                    for i, v in enumerate(f['buildings'])])
                  if f['state'] == 18 and f.get('pending_alert') == building else f
                  for f in results[1]]
        try:
            check_display(results[0], broken)
        except (AssertionError, StopIteration):
            pass
        else:
            raise AssertionError('Unpurchased sensor negative control not detected')
        print(f'PASS: natural sensor/counterfactual and displayed alarm; '
              f'{len(results[0])} frames each, six negative controls')


if __name__ == '__main__':
    main()
