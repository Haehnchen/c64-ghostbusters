#!/usr/bin/env python3
"""Two natural deployments, financial loss, full restart, and new name input."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from natural_playthrough_test import require
from natural_outcomes_test import check_trace as check_first_deployment


def frame_count(schedule):
    return sum(int(line.split('#', 1)[0].split()[0]) for line in schedule.splitlines()
               if line.split('#', 1)[0].strip())


def check_campaign(frames, first_frames):
    check_first_deployment(frames[:first_frames], 'capture')
    require([f['frame'] for f in frames] == list(range(len(frames))), 'Trace frames were reset or lost')
    states = []
    for frame in frames[first_frames:]:
        point = (frame['generation'], frame['state'])
        if not states or point != states[-1]:
            states.append(point)
    expected = [(0, s) for s in (18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
                                30, 31, 32, 33, 17, 18, 45, 59, 60, 61, 62, 63, 57, 58)]
    expected += [(1, s) for s in (255, 0, 1, 2)]
    cursor = iter(states)
    require(all(any(actual == wanted for actual in cursor) for wanted in expected),
            f'Missing connected campaign transition: {states}')
    second_trap = next(f for f in frames[first_frames:] if f['state'] == 28)
    require(second_trap['building'] == 15 and second_trap['empty_traps'] == 1,
            'Second deployment did not use the HQ trap at building 15')
    second_return = next(f for f in frames if f['frame'] > second_trap['frame'] and f['state'] == 18)
    require(second_return['balance'] == [0, 0x80, 0] and second_return['empty_traps'] == 0,
            'Second capture must award 600 and consume the replenished trap')
    require(second_return['buildings'][15] == 0 and second_return['backup_men'] == 3,
            'Second capture left a haunting or lost personnel')
    second_speech = [f for f in frames if second_trap['frame'] <= f['frame'] < second_return['frame'] and f['speech']]
    require(bool(second_speech) and all(f['speech_command'] == 1 for f in second_speech),
            'Missing second success speech')
    require(second_speech[-1]['frame'] - second_speech[0]['frame'] + 1 == len(second_speech),
            'Second capture speech was repeated or interrupted')
    require(second_speech[-1]['audio_energy'] > second_speech[0]['audio_energy'], 'Silent second capture speech')
    require(any(f['effect'] for f in frames if second_trap['frame'] <= f['frame'] < second_speech[0]['frame']),
            'Missing active effect before second capture speech')
    loss = next(f for f in frames if f['generation'] == 0 and f['state'] == 45)
    require(loss['finale_active'] == 1 and loss['balance'] == [0, 0x80, 0],
            'Financial loss must come from the natural rendezvous with insufficient earnings')
    loss_speech = [f for f in frames if f['generation'] == 0 and f['state'] == 57 and f['speech']]
    require(bool(loss_speech) and all(f['speech_command'] == 3 for f in loss_speech),
            'Missing financial-loss speech')
    require(loss_speech[-1]['frame'] - loss_speech[0]['frame'] + 1 == len(loss_speech),
            'Financial-loss speech was repeated or interrupted')
    restarts = [f for f in frames if f['restart_requested']]
    require(len(restarts) == 1 and restarts[0]['state'] == 58 and restarts[0]['raw19'] == 0x20
            and not restarts[0]['script_active'],
            'Restart must consume exactly one F1 input in the real ending')
    fresh = [f for f in frames if f['generation'] == 1]
    require(fresh[0]['state'] == 255 and fresh[0]['vehicle'] == -1 and fresh[0]['owned'] == 0,
            'New startup retained old gameplay owners')
    require(fresh[0]['name'][:3] == [0, 0, 0], 'Restart retained the previous account name')
    require({f['speech_command'] for f in fresh if f['state'] == 255 and f['speech']} == {1, 3},
            'Restart did not play both startup clips')
    require(frames[-1]['generation'] == 1 and frames[-1]['state'] == 2 and
            frames[-1]['name'][:3] == [67, 68, 0], 'New name CD was not entered after restart')
    for before, after in zip(frames, frames[1:]):
        require(after['audio_samples'] > before['audio_samples'], 'Audio stopped across campaign/restart')
    require(loss_speech[-1]['audio_energy'] > loss_speech[0]['audio_energy'], 'Silent loss speech')
    return states


def main():
    executable, base, continuation = (Path(arg).resolve() for arg in sys.argv[1:4])
    prefix = base.read_text()
    schedule = prefix + '\n' + continuation.read_text()
    expected_frames = frame_count(schedule)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software', SDL_AUDIODRIVER='dummy')
    with tempfile.TemporaryDirectory(prefix='ghostbusters-campaign-') as directory:
        root = Path(directory)
        inputs, trace = root / 'campaign.inputs', root / 'trace.jsonl'
        inputs.write_text(schedule)
        subprocess.run([str(executable), '--replay-input', str(inputs), str(trace)],
                       cwd=root, env=env, check=True, timeout=540)
        frames = [json.loads(line) for line in trace.read_text().splitlines()]
        require(len(frames) == expected_frames, 'Campaign ended early or restarted the input plan')
        first_frames = frame_count(prefix)
        states = check_campaign(frames, first_frames)
        mutations = (
            lambda f: dict(f, restart_requested=False),
            lambda f: dict(f, generation=0),
            lambda f: dict(f, speech_command=-1) if f['frame'] >= first_frames and f['state'] == 31 else f,
            lambda f: dict(f, speech_command=-1) if f['state'] == 57 else f,
        )
        for mutate in mutations:
            try:
                check_campaign([mutate(f) for f in frames], first_frames)
            except AssertionError:
                pass
            else:
                raise AssertionError('Campaign negative control not detected')
        print(f'PASS: two captures, HQ, financial loss and playable restart; {len(frames)} frames; {states}')


if __name__ == '__main__':
    main()
