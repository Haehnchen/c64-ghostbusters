#!/usr/bin/env python3
"""Short connected title: text, two Space repeats, release/hold and F1/F3."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def check(rows):
    title = [row for row in rows if row['state'] == 255]
    blocks = []
    for index, row in enumerate(title):
        if not row['speech']:
            continue
        if not blocks or blocks[-1][-1][0] != index - 1 or \
                blocks[-1][-1][1]['speech_command'] != row['speech_command']:
            blocks.append([])
        blocks[-1].append((index, row))
    require([block[0][1]['speech_command'] for block in blocks] == [1, 3, 1, 1],
            'Startup pair and exactly two Space repeats must play; holding must not repeat')
    require(len({tuple(row['title_rows']) for row in title}) > 3,
            'Title text never rotates')
    require(all(row['title_footer_pixels'] == 0 for row in title if row['speech']),
            'Title speech must hide both credits and rainbow')
    for block in blocks[2:]:
        frames = [row for _, row in block]
        require(frames[0]['title_gate'] == 0, 'Space speech escaped its text gate')
        require(len({row['title_scroller'] for row in frames}) == 1,
                'Footer scroll must stop during title speech')
        require(len({row['title_ball_y'] for row in frames}) > 1,
                'Title ball must keep moving during speech')
        require(len({tuple(row['title_rows']) for row in frames}) == 1,
                'Foreground text must wait during speech')
        require(frames[-1]['audio_energy'] > frames[0]['audio_energy'],
                'Title repeat produces no audio')
    after = title[blocks[-1][-1][0] + 1:]
    require(len(after) > 20 and len({row['title_scroller'] for row in after}) > 1 and
            after[-1]['sid_writes'] > after[0]['sid_writes'] and
            after[-1]['audio_energy'] > after[0]['audio_energy'],
            'Title scroll/music did not resume after speech')
    require(any(row['title_footer_pixels'] > 0 for row in after),
            'Title footer failed to return after speech')
    require(all(b['audio_samples'] > a['audio_samples'] for a, b in zip(rows, rows[1:])),
            'PCM stopped across title/dialogue handoff')
    require(rows[-1]['state'] == 1 and rows[-1]['name'][:3] == [65, 66, 0],
            'F1/F3 did not reach a usable fresh name dialogue')


def main():
    binary = Path(sys.argv[1]).resolve()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               SDL_RENDER_DRIVER='software')
    with tempfile.TemporaryDirectory(prefix='ghostbusters-title-') as directory:
        root = Path(directory)
        for key in ('F1', 'F3'):
            plan = root / 'title.inputs'
            # The opening music has five counter-wrap markers before the
            # next lyric; let that intro and the name prompt finish naturally.
            plan.write_text('300\n400 Space\n20\n400 Space\n1200\n2 ' + key +
                            '\n2000\n2 A\n20\n2 B\n20\n')
            trace = root / 'title.jsonl'
            subprocess.run([str(binary), '--replay-input', str(plan), str(trace)],
                           cwd=root, env=env, check=True, timeout=60)
            rows = [json.loads(line) for line in trace.read_text().splitlines()]
            check(rows)
            print(f'PASS: title text/Space hold-release/audio/{key}, {len(rows)} frames')


if __name__ == '__main__':
    main()
