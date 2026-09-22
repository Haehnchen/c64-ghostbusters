#!/usr/bin/env python3
"""Natural profitable campaign, both Zuul endings and account reuse after restart."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from natural_playthrough_test import require
from natural_campaign_test import frame_count
from natural_marshmallow_test import check_trace as check_first_marshmallow


def check_trace(frames, outcome='victory', purchased_vehicle=0):
    require(outcome in ('victory', 'failure', 'mixed_victory', 'mixed_failure'), 'Unknown Zuul outcome')
    victory = outcome.endswith('victory')
    require(purchased_vehicle in (0, 3) and (victory or purchased_vehicle == 0),
            'Unsupported new-game vehicle purchase')
    mixed = outcome.startswith('mixed_')
    gate_events = (['collision', 'passage', 'passage'] if victory else
                   ['passage', 'collision', 'collision']) if mixed else (
                       ['passage', 'passage'] if victory else ['collision', 'collision'])
    remaining_men = 3 - len(gate_events)
    final_balance = [1, 0x56 if victory else 6, 0]
    account = [0x26, 0x75, 0x26, 0] if victory else [0x06, 0x34, 0x30, 0]
    # The mixed victory fixture deliberately exercises F1 -> title F3.  That
    # is the one connected-session path; all other outcomes retain the
    # ordinary fresh-title/manual-account flow.
    resume = outcome == 'mixed_victory'
    require([f['frame'] for f in frames] == list(range(len(frames))), 'Missing campaign frames')
    approaches = [f for i, f in enumerate(frames) if f['state'] == 36 and (i == 0 or frames[i - 1]['state'] != 36)]
    require(len(approaches) == 2, 'Campaign must resolve two natural Marshmallow incidents')
    second = approaches[1]
    check_first_marshmallow(frames[:second['frame']], True, check_final_movement=False)
    require(second['pending_alert'] == 11 and second['bait'] == 4 and second['balance'] == [0, 0x86, 0],
            'Second naturally matured alarm did not retain the first reward/inventory')
    alert = next(f for f in frames if f.get('pending_alert') == 11)
    require(0x6000 <= alert['pk'] < 0x7000 and alert['buildings'][11] == 0xC8,
            'Second alarm was not naturally created at PK 6000')
    require(any(f['state'] == 21 for f in frames[alert['frame']:second['frame']]),
            'Second alarm did not mature during an actual drive')
    ages = []
    for f in frames[alert['frame']:second['frame']]:
        point = (f['pending_alert'], f['buildings'][11])
        if not ages or ages[-1] != point:
            ages.append(point)
    require(ages == [(11, 0xC8), (11, 0xD8), (11, 0xE8)], 'Second alarm was replaced or aged out of order')
    returned = next(f for f in frames[second['frame']:] if f['state'] == 18)
    scene = frames[second['frame']:returned['frame']]
    used = next(f for f in scene if f['bait_active'])
    require(used['state'] == 36 and used['sampled_key17'] == 0x42, 'Second bait did not come from a real B input')
    require(all(f['city_music'] and not f['speech'] for f in scene) and
            scene[-1]['audio_energy'] > scene[0]['audio_energy'], 'Second incident lost its music')
    require(returned['balance'] == [1, 6, 0] and returned['bait'] == 3 and returned['bait_active'] == 0,
            'Second bait must award $2000, reaching $10600 with three bait remaining')
    require(any(f['state'] == 38 for f in frames[second['frame']:returned['frame']]) and
            all(f['state'] != 37 for f in frames[:returned['frame']]), 'Second bait took the attack path')
    require(returned['map_types'] == approaches[0]['map_types'], 'Baited incidents destroyed buildings')
    zuul = next(f for f in frames if f['state'] == 40)
    require(zuul['balance'] == [1, 6, 0] and zuul['building'] == 10 and zuul['finale_active'] == 1,
            'Profitable natural rendezvous did not enter Zuul')
    require(not any(f['state'] in ((45, 46) if victory else (42, 43, 44, 45, 54))
                    for f in frames[zuul['frame']:] if f['generation'] == 0), 'Zuul route took the wrong ending branch')
    states = []
    for f in frames[zuul['frame']:]:
        point = (f['generation'], f['state'])
        if not states or states[-1] != point:
            states.append(point)
    branch = (42, 43, 44, 54, 55, 56) if victory else (46, 47, 48)
    expected = [(0, s) for s in (40, 41, *branch, 49, 50, 51, 52, 53, 57, 58)]
    expected += [(1, s) for s in ((255, 0, 6, 15) if resume else
                                  (255, 0, 1, 2, 3, 6, 15))]
    cursor = iter(states)
    require(all(any(actual == wanted for actual in cursor) for wanted in expected),
            f'Missing connected {outcome}/account/restart transition: {states}')
    gate = [f for f in frames if f['generation'] == 0 and f['state'] == 41]
    require({f['backup_men'] for f in gate} == set(range(remaining_men, 4)),
            'Wrong personnel consumption at the gate')
    consumed = [b for a, b in zip(gate, gate[1:]) if b['backup_men'] != a['backup_men']]
    require([f['backup_men'] for f in consumed] == list(range(2, remaining_men - 1, -1)),
            'Each gate event must consume exactly one person without reset or underflow')
    require(['collision' if f['speech'] else 'passage' for f in consumed] == gate_events,
            'Wrong order of real collisions and successful gate passages')
    speech = [f for f in frames if f['generation'] == 0 and f['frame'] >= zuul['frame'] and f['speech']]
    require(bool(speech) and all((f['speech_command'], f['state']) in ((3, 41), (4, 44)) for f in speech),
            'Missing or unexpected Zuul speech')
    blocks = []
    for f in speech:
        if not blocks or f['frame'] != blocks[-1][-1]['frame'] + 1:
            blocks.append([])
        blocks[-1].append(f)
    expected_speech = [3 for event in gate_events if event == 'collision'] + ([4] if victory else [])
    require([b[0]['speech_command'] for b in blocks] == expected_speech, 'Wrong order or number of Zuul speech calls')
    for block in blocks:
        require(len({f['speech_command'] for f in block}) == 1, 'Speech command changed within a call')
        require(block[-1]['audio_energy'] > block[0]['audio_energy'], 'Silent Zuul speech')
        require(len({f['frame09'] for f in block}) == 1, 'Gameplay advanced during blocking speech')
        frozen = {(f['state'], tuple(f['sprites_x']), tuple(f['sprites_y']),
                   f['backup_men'], tuple(f['balance'])) for f in block}
        require(len(frozen) == 1, 'Gameplay changed while a speech call was blocking')
    collision_blocks = [b for b in blocks if b[0]['speech_command'] == 3]
    require([b[0]['backup_men'] for b in collision_blocks] ==
            [2 - i for i, event in enumerate(gate_events) if event == 'collision'],
            'Collisions did not consume the expected personnel')
    require(all((f['joystick33'] & 1) == 0 for f in consumed), 'Gate event did not follow a real Up input')
    for block in collision_blocks:
        later_events = [f for f in consumed if f['frame'] > block[-1]['frame']]
        if not later_events:
            continue
        require(any(not f['speech'] and f['sprites_x'][5] == 96 and f['sprites_y'][5] == 192
                    for f in frames[block[-1]['frame'] + 1:later_events[0]['frame']]),
                'Buster did not recover to the starting position before the next gate event')
    ending = next(f for f in frames if f['state'] == (54 if victory else 46))
    require(ending['balance'] == final_balance, 'Wrong ending reward or retained failure balance')
    require(ending['backup_men'] == remaining_men, 'Ending lost the final gate personnel count')
    require(ending['owned'] == 9 and ending['empty_traps'] == 1 and
            ending['bait'] == 3 and ending['backpack_charge'] == (1 if victory else 0x99),
            'Zuul ending has incorrect equipment or backpack charge')
    ready = next(f for f in frames if f['generation'] == 0 and f['state'] == 58 and not f['script_active'])
    require(ready['ending_account'] == account and ready['balance'] == final_balance,
            'Wrong displayed account or final balance')
    require(ready['backup_men'] == remaining_men, 'Completed ending changed personnel')
    require(ready['owned'] == 9 and ready['empty_traps'] == 1 and
            ready['bait'] == 3 and ready['backpack_charge'] == (1 if victory else 0x99),
            'Completed ending changed inventory or backpack charge')
    restart = [f for f in frames if f['restart_requested']]
    require(len(restart) == 1 and restart[0]['state'] == 58 and
            restart[0]['raw19'] == (0x20 if resume else (0x28 if victory else 0x20)) and
            not restart[0]['script_active'],
            'Expected one real restart from the completed ending')
    fresh = [f for f in frames if f['generation'] == 1]
    require(fresh[0]['state'] == 255 and fresh[0]['owned'] == 0 and fresh[0]['name'][:3] == [0, 0, 0],
            'Full restart retained previous game data')
    if resume:
        title_states = []
        for f in fresh:
            if not title_states or title_states[-1] != f['state']:
                title_states.append(f['state'])
        require(title_states[:3] == [255, 0, 6] and
                not any(f['state'] in (1, 2, 3, 4, 5) for f in fresh),
                f'F3 resume reopened the account dialogue: {title_states}')
        resumed = next(f for f in fresh if f['state'] == 6)
        require(resumed['name'][:3] == [65, 66, 0] and
                resumed['balance'] == final_balance and resumed['owned'] == 0 and
                resumed['vehicle'] == -1,
                'F3 resume did not retain AB/$15600 without old vehicle or equipment')
        require(all(f['owned'] == 0 for f in fresh),
                'F3 resume retained old equipment in the new shop')
    else:
        accepted = next(f for f in fresh if f['state'] == 6)
        digits = [f['sampled_key17'] for f in fresh if f['state'] == 3 and 0x30 <= f['sampled_key17'] <= 0x39]
        require(digits == list(map(ord, '26752600' if victory else '06343000')),
                'Account digits were not entered exactly, including leading zeros')
        require(accepted['name'][:3] == [65, 66, 0] and accepted['balance'] == final_balance,
                'Displayed account was not accepted for AB after restart')
        require(not any(f['state'] == 4 for f in fresh), 'Reused account was rejected')
    last = frames[-1]
    purchase_balance = [0, 6, 0] if purchased_vehicle == 3 else ([1, 0x36, 0] if victory else [0, 0x86, 0])
    require(last['generation'] == 1 and last['state'] == 15 and last['vehicle'] == purchased_vehicle and
            last['balance'] == purchase_balance and last['owned'] == 0 and last['traps'] == 0,
            'New game did not purchase its own car using the restored balance')
    require(all(b['audio_samples'] > a['audio_samples'] for a, b in zip(frames, frames[1:])),
            'Audio stalled during the full campaign')


def main():
    executable, prefix, continuation = (Path(arg).resolve() for arg in sys.argv[1:4])
    outcome = sys.argv[4] if len(sys.argv) > 4 else 'victory'
    require(outcome in ('victory', 'failure', 'mixed_victory', 'mixed_failure'), 'Unknown Zuul outcome')
    victory = outcome.endswith('victory')
    resume = outcome == 'mixed_victory'
    purchased_vehicle = int(sys.argv[5]) if len(sys.argv) > 5 else 0
    schedule = prefix.read_text() + '\n' + continuation.read_text()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', SDL_AUDIODRIVER='dummy')
    with tempfile.TemporaryDirectory(prefix='ghostbusters-zuul-') as directory:
        root = Path(directory)
        plan, trace = root / 'plan.inputs', root / 'trace.jsonl'
        plan.write_text(schedule)
        subprocess.run([str(executable), '--replay-input', str(plan), str(trace)],
                       cwd=root, env=env, check=True, timeout=600)
        frames = [json.loads(line) for line in trace.read_text().splitlines()]
        require(len(frames) == frame_count(schedule), 'Incomplete Zuul replay')
        check_trace(frames, outcome, purchased_vehicle)
        mutations = [
            lambda f: dict(f, balance=[0, 0, 0]) if f['state'] == 40 else f,
            lambda f: dict(f, speech_command=-1) if f['state'] == (44 if victory else 41) else f,
            lambda f: dict(f, backup_men=3) if f['state'] == 41 else f,
            lambda f: dict(f, balance=[0, 0, 0]) if f['state'] == (54 if victory else 46) else f,
            lambda f: dict(f, backpack_charge=0) if f['state'] == (54 if victory else 46) else f,
            lambda f: dict(f, sampled_key17=0) if f['state'] == 36 and f['bait'] == 3 else f,
            lambda f: dict(f, ending_account=[0, 0, 0, 0]),
            lambda f: dict(f, restart_requested=False),
            lambda f: dict(f, balance=[1, 0, 0]) if f['generation'] == 1 and f['state'] == 6 else f,
            lambda f: dict(f, vehicle=-1) if f['generation'] == 1 and f['state'] == 15 else f,
        ]
        if resume:
            # These controls are specific to the connected F3 title path:
            # the account identity and balance must be carried over, the
            # dialogue states must not run, and no old inventory may leak.
            mutations.extend([
                lambda f: dict(f, name=[0, 0, 0])
                if f['generation'] == 1 and f['state'] == 6 else f,
                lambda f: dict(f, state=1)
                if f['generation'] == 1 and f['state'] == 6 else f,
                lambda f: dict(f, owned=9)
                if f['generation'] == 1 and f['state'] == 6 else f,
            ])
        else:
            mutations.append(lambda f: dict(f, sampled_key17=0)
                             if f['generation'] == 1 and f['state'] == 3 and f['sampled_key17'] == 0x30 else f)
        if outcome.startswith('mixed_'):
            mutations.append(lambda f: dict(f, backup_men=255)
                             if f.get('backup_men') == 0 and f['state'] in (41, 46, 54, 58) else f)
        if outcome != 'victory':
            mutations.append(lambda f: dict(f, speech=False) if f['state'] == 41 else f)
        if not victory:
            mutations.append(lambda f: dict(f, owned=0) if f['state'] == 46 else f)
        for mutate in mutations:
            try:
                check_trace([mutate(f) for f in frames], outcome, purchased_vehicle)
            except (AssertionError, StopIteration):
                pass
            else:
                raise AssertionError('Zuul negative control not detected')
        print(f'PASS: natural profitable Zuul {outcome}, speech, account and restart/reuse; {len(frames)} frames')


if __name__ == '__main__':
    main()
