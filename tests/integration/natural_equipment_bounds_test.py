#!/usr/bin/env python3
"""Regular shop input proves trap requirement, price rejection and full cargo."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from natural_campaign_test import frame_count
from natural_playthrough_test import require


def check_trace(frames):
    require([f['frame'] for f in frames] == list(range(len(frames))), 'Missing shop frames')
    shop = [f for f in frames if f['state'] == 15]
    require(shop and shop[0]['vehicle'] == 0 and shop[0]['name'][:3] == [65, 66, 0]
            and shop[0]['balance'] == [0, 0x80, 0], 'Regular Compact purchase failed')
    no_trap_exit = [f for f in shop if f['sampled_key17'] == 0x45 and f['traps'] == 0]
    require(len(no_trap_exit) == 1, 'Missing real E attempt without a trap')
    start = no_trap_exit[0]['frame']
    require(all(f['state'] == 15 and f['traps'] == 0 and f['balance'] == [0, 0x80, 0]
                for f in frames[start:start + 60]), 'Shop allowed exit without a trap')

    changes = [shop[0]] + [b for a, b in zip(shop, shop[1:]) if a['purchase_count'] != b['purchase_count']]
    require([f['purchase_count'] for f in changes] == list(range(6)), 'Wrong purchase count or cargo overflow')
    require([f['traps'] for f in changes] == list(range(6)), 'Repeated trap purchase failed')
    require([f['balance'] for f in changes] == [[0, value, 0] for value in (0x80, 0x74, 0x68, 0x62, 0x56, 0x50)],
            'Wrong trap prices or money changed on a rejected purchase')
    require(all((f['joystick33'] & 0x10) == 0 for f in changes[1:]), 'Purchase lacked actual fire input')
    require(all(f['owned'] == 0 for f in shop), 'Rejected laser became owned')

    laser = [f for f in shop if f['shop_category'] == 2]
    attempts = [f for f in laser if f['shop_position'] == 1 and f['carried'] == 3
                and (f['joystick33'] & 0x10) == 0]
    require(attempts, 'Missing actual $8000 laser purchase attempt at the car')
    require(all(f['balance'] == [0, 0x74, 0] and f['purchase_count'] == 1 and
                f['traps'] == 1 and f['capacity_notice_bytes'] == 0 for f in laser),
            'Unaffordable laser changed funds, inventory or raised a capacity notice')
    after = frames[attempts[-1]['frame'] + 1:attempts[-1]['frame'] + 61]
    require(len(after) == 60 and all(f['state'] == 15 and f['carried'] == 3 for f in after),
            'Rejected unaffordable item was removed from the forklift')

    full_attempts = [f for f in shop if f['purchase_count'] == 5 and f['shop_position'] == 5
                     and f['carried'] == 4 and (f['joystick33'] & 0x10) == 0]
    require(full_attempts and all(f['balance'] == [0, 0x50, 0] and f['traps'] == 5 and
                                 f['capacity_notice_bytes'] == 32 for f in full_attempts),
            'Sixth trap was not rejected with the capacity notice despite sufficient money')
    tail = [f for f in shop if f['frame'] >= full_attempts[0]['frame']]
    require(all(f['balance'] == [0, 0x50, 0] and f['purchase_count'] == 5 and f['traps'] == 5
                and f['carried'] == 4 for f in tail), 'Full cargo changed after the rejected purchase')
    exit_frames = [f for f in frames if f['sampled_key17'] == 0x45 and f['state'] == 16]
    require(len(exit_frames) == 1 and exit_frames[0]['traps'] == 5, 'Full car could not exit normally')
    city = [f for f in frames if f['state'] == 18]
    require(city and city[0]['notice_position'] == 1 and city[0]['notice_length'] == 32,
            'Capacity notice was not handed to the city scroller')
    require(any(f['notice_position'] > 1 for f in city) and city[-1]['notice_position'] == 0,
            'Capacity notice did not scroll to completion')
    require(all(f['empty_traps'] == 5 and f['balance'] == [0, 0x50, 0] and f['owned'] == 0 for f in city),
            'City did not retain full purchased cargo and money')
    require(city[-1]['player_x'] == 55 and city[-1]['player_y'] == 186 and city[-1]['city_music'],
            'City did not accept normal movement/music after the shop')
    require(any(f['effect'] for f in shop), 'Shop motor effect missing')
    require(all(b['audio_samples'] > a['audio_samples'] for a, b in zip(frames, frames[1:])) and
            frames[-1]['audio_energy'] > shop[0]['audio_energy'], 'Audio stalled')


def main():
    executable, schedule = (Path(arg).resolve() for arg in sys.argv[1:3])
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', SDL_AUDIODRIVER='dummy')
    with tempfile.TemporaryDirectory(prefix='ghostbusters-shop-bounds-') as directory:
        trace = Path(directory) / 'trace.jsonl'
        subprocess.run([str(executable), '--replay-input', str(schedule), str(trace)],
                       cwd=directory, env=env, check=True, timeout=300)
        frames = [json.loads(line) for line in trace.read_text().splitlines()]
        require(len(frames) == frame_count(schedule.read_text()), 'Incomplete shop replay')
        check_trace(frames)
        for mutate in (
            lambda f: dict(f, sampled_key17=0) if f['traps'] == 0 else f,
            lambda f: dict(f, balance=[0, 0, 0]) if f['shop_category'] == 2 else f,
            lambda f: dict(f, owned=0x40) if f['shop_category'] == 2 else f,
            lambda f: dict(f, purchase_count=6) if f['purchase_count'] == 5 else f,
            lambda f: dict(f, capacity_notice_bytes=0),
            lambda f: dict(f, notice_position=0) if f['state'] == 18 else f,
        ):
            try:
                check_trace([mutate(f) for f in frames])
            except AssertionError:
                pass
            else:
                raise AssertionError('Shop negative control not detected')
        print(f'PASS: no-trap exit, unaffordable laser, five-trap limit and city notice; {len(frames)} frames')


if __name__ == '__main__':
    main()
