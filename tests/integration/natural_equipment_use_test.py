#!/usr/bin/env python3
"""Buy equipment normally; compare street capture and monitoring with non-purchases."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from natural_campaign_test import frame_count
from natural_playthrough_test import require


PROFILES = {'equipped': (0x27, 0x49, 5), 'no_vacuum': (7, 0x54, 4),
            'no_monitors': (0x20, 0x69, 2)}


def schedule_for(schedule, profile):
    skipped = ('vacuum',) if profile == 'no_vacuum' else (
        ('detector', 'intensifier', 'sensor') if profile == 'no_monitors' else ())
    for item in skipped:
        for action in ('pickup', 'buy'):
            marker = f'2 Space # {action}_{item}'
            require(schedule.count(marker) == 1, f'Missing unique input marker: {marker}')
            schedule = schedule.replace(marker, f'2 # omitted {action}_{item}')
    return schedule


def check_trace(frames, profile):
    owned, money, purchases = PROFILES[profile]
    require([f['frame'] for f in frames] == list(range(len(frames))), 'Missing equipment-use frames')
    city = next(f for f in frames if f['state'] == 18)
    require(city['name'][:3] == [65, 66, 0] and city['vehicle'] == 0 and
            city['owned'] == owned and city['balance'] == [0, money, 0] and
            city['purchase_count'] == purchases and city['empty_traps'] == 1,
            'Wrong regular equipment purchases, prices or trap')
    contact = [f for f in frames if f['state'] == 18 and any(f['roamer_contacts'])]
    require(contact, 'No naturally contacted street ghost before driving')
    drives = [f for f in frames if f['state'] == 21 and 'drive_capture_timers' in f]
    street = [f for f in drives if 0 < f['sprites_y'][0] < 224]
    require(street, 'No natural street ghost spawned on the route')
    acquired = [f for f in street if f['sprite_targets_y'][0] != 240]
    captured = [f for f in drives if f['drive_capture_timers'][0]]
    if profile == 'no_vacuum':
        require(not acquired and not captured, 'Unpurchased vacuum captured a street ghost')
        require(any((f['joystick33'] & 16) == 0 for f in street), 'Control never attempted vacuum fire')
    else:
        require(acquired and (acquired[0]['joystick33'] & 16) == 0,
                'Vacuum acquisition lacked actual fire input')
        require([f['drive_capture_timers'][0] for f in captured] == list(range(31, 0, -1)),
                'Street capture animation did not complete exactly once')
        require(any(f['effect'] for f in captured) and
                captured[-1]['audio_energy'] > captured[0]['audio_energy'], 'Silent vacuum capture')
        require(frames[captured[-1]['frame'] + 1]['sprites_y'][0] == 0,
                'Captured street ghost did not disappear')
    attempt = next(f for f in frames if f['state'] == 28)
    returned = next(f for f in frames if f['state'] == 18 and f['frame'] > attempt['frame'])
    require(attempt['building'] == 7 and any(f['state'] == 29 for f in frames),
            'Missing natural building attempt and missed-trap path')
    speech = [f for f in frames if attempt['frame'] <= f['frame'] < returned['frame'] and f['speech']]
    require(speech and all(f['speech_command'] == 2 for f in speech), 'Missing missed-trap speech 2')
    require(returned['empty_traps'] == 1 and returned['backup_men'] == 2 and
            returned['buildings'][7] == 0 and returned['balance'] == [0, money, 0],
            'Missed trap changed the wrong resources')
    # The intensifier affects positioning/beams. The trap-opening handler
    # deliberately brings all sprites to the foreground, even without it.
    ghost_frames = [f for f in frames if f['state'] in (24, 25, 26, 27)]
    require(ghost_frames and all(bool(f['sprite_priority'] & 16) == (profile == 'no_monitors')
                                for f in ghost_frames[1:]), 'Image intensifier did not change ghost visibility priority')
    hq = next(f for f in frames if f['state'] == 35)
    last = frames[-1]
    require(hq['building'] == 17 and hq['empty_traps'] == 1 and hq['backup_men'] == 3 and
            hq['backpack_charge'] == 0x99 and hq['full_traps'] == 0, 'HQ replenishment failed')
    require(last['state'] == 18 and last['player_x'] == 55 and last['player_y'] == 186 and
            last['city_music'] and not last['speech'], 'Missing playable city after HQ')
    require(all(f['owned'] == owned and f['balance'] == [0, money, 0]
                for f in (returned, hq, last)), 'Equipment or money lost between scenes')
    require(all(b['audio_samples'] > a['audio_samples'] for a, b in zip(frames, frames[1:])),
            'Equipment route audio stalled')


def check_detector(equipped, unmonitored):
    observed = [(a, b) for a, b in zip(equipped, unmonitored)
                if a['state'] == b['state'] == 18 and a['buildings'][15] & 3 == 1]
    require(observed and all(a['buildings'][15] & 0xFC == b['buildings'][15] & 0xFC and
                            b['buildings'][15] & 3 == 0 for a, b in observed),
            'Detector did not reveal the same natural early haunting that stays hidden without it')


def main():
    executable, plan = (Path(arg).resolve() for arg in sys.argv[1:3])
    schedule = plan.read_text()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', SDL_AUDIODRIVER='dummy')
    baseline = None
    with tempfile.TemporaryDirectory(prefix='ghostbusters-equipment-use-') as directory:
        root = Path(directory)
        for profile in PROFILES:
            inputs, trace = root / f'{profile}.inputs', root / f'{profile}.jsonl'
            inputs.write_text(schedule_for(schedule, profile))
            subprocess.run([str(executable), '--replay-input', str(inputs), str(trace)],
                           cwd=root, env=env, check=True, timeout=360)
            frames = [json.loads(line) for line in trace.read_text().splitlines()]
            require(len(frames) == frame_count(schedule), 'Incomplete equipment-use replay')
            check_trace(frames, profile)
            if profile == 'equipped':
                baseline = frames
                for mutate in (
                    lambda f: dict(f, owned=0),
                    lambda f: dict(f, drive_capture_timers=[0, 0]),
                    lambda f: dict(f, sprite_priority=16),
                    lambda f: dict(f, speech_command=-1),
                    lambda f: dict(f, backup_men=2) if f['state'] == 35 else f,
                ):
                    try:
                        check_trace([mutate(f) for f in frames], profile)
                    except AssertionError:
                        pass
                    else:
                        raise AssertionError('Equipment-use negative control not detected')
            elif profile == 'no_monitors':
                check_detector(baseline, frames)
                broken = [dict(f, buildings=[value | 1 if i == 15 else value
                                               for i, value in enumerate(f['buildings'])])
                          if f['state'] == 18 else f for f in frames]
                try:
                    check_detector(baseline, broken)
                except AssertionError:
                    pass
                else:
                    raise AssertionError('Detector negative control not detected')
            print(f'PASS: natural {profile}, street ghost, building miss and HQ; {len(frames)} frames', flush=True)


if __name__ == '__main__':
    main()
