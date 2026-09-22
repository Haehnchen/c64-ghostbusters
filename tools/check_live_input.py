#!/usr/bin/env python3
"""Exercise real SDL held-key input in a private X server, without game-state injection."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


class Keyboard:
    def __init__(self, display, *, focus=True):
        self.x = ctypes.CDLL('libX11.so.6')
        self.t = ctypes.CDLL('libXtst.so.6')
        ptr, ulong = ctypes.c_void_p, ctypes.c_ulong
        self.x.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x.XOpenDisplay.restype = ptr
        self.x.XDefaultRootWindow.argtypes = [ptr]
        self.x.XDefaultRootWindow.restype = ulong
        self.x.XQueryTree.argtypes = [ptr, ulong, ctypes.POINTER(ulong), ctypes.POINTER(ulong),
                                     ctypes.POINTER(ctypes.POINTER(ulong)), ctypes.POINTER(ctypes.c_uint)]
        self.x.XSetInputFocus.argtypes = [ptr, ulong, ctypes.c_int, ulong]
        self.x.XFree.argtypes = [ptr]
        self.x.XFlush.argtypes = [ptr]
        self.x.XCloseDisplay.argtypes = [ptr]
        self.x.XAutoRepeatOff.argtypes = [ptr]
        self.x.XStringToKeysym.argtypes = [ctypes.c_char_p]
        self.x.XStringToKeysym.restype = ulong
        self.x.XKeysymToKeycode.argtypes = [ptr, ulong]
        self.t.XTestFakeKeyEvent.argtypes = [ptr, ctypes.c_uint, ctypes.c_int, ulong]
        self.d = self.x.XOpenDisplay(display.encode())
        if not self.d:
            raise RuntimeError('Cannot open private X server')
        root = self.x.XDefaultRootWindow(self.d)
        rr, parent, count = ulong(), ulong(), ctypes.c_uint()
        children = ctypes.POINTER(ulong)()
        if not self.x.XQueryTree(self.d, root, ctypes.byref(rr), ctypes.byref(parent),
                                ctypes.byref(children), ctypes.byref(count)) or not count.value:
            raise RuntimeError('SDL has no window')
        if focus:
            self.x.XSetInputFocus(self.d, children[count.value - 1], 1, 0)
        self.x.XFree(children)
        self.x.XAutoRepeatOff(self.d)
        self.x.XFlush(self.d)

    def key(self, name, down):
        code = self.x.XKeysymToKeycode(self.d, self.x.XStringToKeysym(name.encode()))
        if not code or not self.t.XTestFakeKeyEvent(self.d, code, int(down), 0):
            raise RuntimeError('XTest key failed: ' + name)
        self.x.XFlush(self.d)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--start-key', choices=('F1', 'F3'), default='F1')
    parser.add_argument('--lead-in', action='store_true',
                        help='Check physical input after a one-frame play-from handoff')
    args = parser.parse_args()
    suffix = ('-f3' if args.start_key == 'F3' else '') + ('-handoff' if args.lead_in else '')
    out = ROOT / 'build/test-results/live-input' / (args.start_key.lower() + ('-handoff' if args.lead_in else ''))
    out.mkdir(parents=True, exist_ok=True)
    trace = out / 'trace.jsonl'
    trace.unlink(missing_ok=True)
    # -displayfd allocates a free server atomically; no existing display is touched.
    readfd, writefd = os.pipe()
    server = subprocess.Popen(['Xvfb', '-displayfd', str(writefd), '-screen', '0', '1024x768x24'],
                              pass_fds=(writefd,), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.close(writefd)
    with os.fdopen(readfd) as fd:
        display = ':' + fd.readline().strip()
    app = keyboard = None
    binary = args.build_dir.resolve() / 'ghostbusters'
    rows = []
    try:
        env = {**os.environ, 'DISPLAY': display, 'SDL_VIDEO_DRIVER': 'x11', 'SDL_AUDIO_DRIVER': 'dummy'}
        command = [str(binary), '--trace-input', str(trace)]
        if args.lead_in:
            schedule = out / 'lead-in.inputs'
            schedule.write_text('1\n')
            command = [str(binary), '--play-from', str(schedule), 'title', '--trace-input', str(trace)]
        with (out / 'app.log').open('w') as log:
            app = subprocess.Popen(command, cwd=ROOT, env=env,
                                   stdout=log, stderr=subprocess.STDOUT)
            time.sleep(1)
            keyboard = Keyboard(display)

            def read():
                if app.poll() is not None:
                    raise RuntimeError('SDL exited; see ' + str(out / 'app.log'))
                if not trace.exists(): return []
                lines = trace.read_text().splitlines(keepends=True)
                return [row for line in lines if line.endswith('\n')
                        if (row := json.loads(line))['state'] != 255]

            def wait_for(predicate, timeout=30):
                end = time.monotonic() + timeout
                while time.monotonic() < end:
                    current = read()
                    if current and predicate(current): return current
                    time.sleep(0.03)
                raise RuntimeError('SDL input condition timed out')

            def trailing_rows(current, predicate):
                """Return the contiguous matching foreground rows at the end."""
                result = []
                for row in reversed(current):
                    if not predicate(row): break
                    result.append(row)
                return list(reversed(result))

            # Startup speech must finish before F1 is accepted. Repeated host
            # taps start the ordinary dialog; no state or event-byte imports.
            for _ in range(100):
                keyboard.key(args.start_key, True); time.sleep(0.06)
                keyboard.key(args.start_key, False); time.sleep(0.06)
                if read(): break
            else: raise RuntimeError('Title did not accept F1')
            rows = wait_for(lambda r: r[-1]['state'] == 1)
            assert rows[0]['state'] == 0 and rows[0]['reset'] == 1 and rows[0]['frame09'] == 0, 'Missing initial full-reset frame'
            assert rows[1]['state'] == 1 and rows[1]['reset'] == 0 and rows[1]['frame09'] == 1, 'State0 did not own its following frame'
            keyboard.key('a', True)
            rows = wait_for(lambda r: r[-1]['raw19'] == 17 and r[-1]['key17'] == 65)
            assert rows[-1]['name'][0] == 0, 'A was not pressed during name-script output'
            rows = wait_for(lambda r: r[-1]['name'][0] == 65)
            consumed = rows[-1]['frame']
            rows = wait_for(lambda r: r[-1]['frame'] >= consumed + 20)
            assert all(r['name'][1] == 0 for r in rows), 'Held key repeated in name'
            keyboard.key('a', False)
            rows = wait_for(lambda r: r[-1]['raw19'] == 255 and r[-1]['latch18'] == 0)
            keyboard.key('b', True)
            rows = wait_for(lambda r: r[-1]['name'][:2] == [65, 66])
            keyboard.key('b', False)

            rows = wait_for(lambda r: r[-1]['raw19'] == 255 and
                            r[-1]['fullscreen_requested'] is False)
            assert rows[-1]['name'][:3] == [65, 66, 0], 'Fullscreen baseline changed name'

            # F11 is handled by SDL rather than the game scanner. Hold the real
            # host key long enough to prove stable foreground rendering and
            # PCM production, then release it before the second toggle.
            f11_down = False
            try:
                keyboard.key('F11', True)
                f11_down = True
                fullscreen_rows = wait_for(
                    lambda r: len(trailing_rows(r, lambda row: row['fullscreen_requested'] is True)) >= 12)
            finally:
                if f11_down:
                    keyboard.key('F11', False)
            fullscreen_rows = trailing_rows(fullscreen_rows,
                                             lambda row: row['fullscreen_requested'] is True)
            assert len(fullscreen_rows) >= 12, 'Fullscreen hold did not span 12 foreground frames'
            assert all(row['name'][:3] == [65, 66, 0] and row['raw19'] == 255
                       for row in fullscreen_rows), 'F11 changed dialog input or name'
            assert all(b['audio_samples'] > a['audio_samples']
                       for a, b in zip(fullscreen_rows, fullscreen_rows[1:])), \
                'PCM sample production did not advance during fullscreen hold'

            held_last_frame = fullscreen_rows[-1]['frame']
            rows = wait_for(lambda r: r[-1]['frame'] > held_last_frame and
                            r[-1]['fullscreen_requested'] is True and r[-1]['raw19'] == 255)
            released_last_frame = rows[-1]['frame']

            # A second real key-down must leave fullscreen, and the key-up must
            # be sent before continuing to the game's Return transition.
            f11_down = False
            try:
                keyboard.key('F11', True)
                f11_down = True
                toggled_off = wait_for(
                    lambda r: r[-1]['frame'] > released_last_frame and
                    r[-1]['fullscreen_requested'] is False and r[-1]['raw19'] == 255)
            finally:
                if f11_down:
                    keyboard.key('F11', False)
            toggled_off_last_frame = toggled_off[-1]['frame']
            wait_for(lambda r: r[-1]['frame'] > toggled_off_last_frame and
                     r[-1]['fullscreen_requested'] is False and r[-1]['raw19'] == 255)

            keyboard.key('Return', True)
            rows = wait_for(lambda r: r[-1]['state'] >= 2)
            keyboard.key('Return', False)
            rows = wait_for(lambda r: r[-1]['raw19'] == 255)
            assert rows[-1]['name'][:3] == [65, 66, 0]
            keyboard.key('Escape', True)
            app.wait(timeout=5)
            if app.returncode: raise RuntimeError('SDL exit failed')
        result = {'status': 'pass', 'frames': len(rows), 'name': 'AB', 'start_key': args.start_key,
                  'play_from_handoff': args.lead_in,
                  'checks': ['full reset then State0 in separate frames', 'early held A survives printing', 'held A consumed once',
                             'release clears repeat latch', 'new B accepted',
                             'held F11 keeps fullscreen requested for 12 foreground frames',
                             'fullscreen hold preserves AB and raw19=255 while PCM samples advance',
                             'released F11 then second press requests windowed mode',
                             'Return advances dialog'],
                  'scope': 'Real XTest -> SDL held contacts -> native scanner -> dialog, normal startup. No imported scene state.',
                  'full_game_parity': False,
                  # Xvfb without a window manager need not apply fullscreen requests.
                  # Observe both states; never turn request acceptance into desktop sign-off.
                  'fullscreen_applied_observed': any(row['fullscreen'] for row in fullscreen_rows),
                  'desktop_fullscreen_acceptance': False,
                  'native_binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                  'trace_sha256': hashlib.sha256(trace.read_bytes()).hexdigest()}
        (ROOT / ('build/test-results/live-input' + suffix + '.json')).write_text(json.dumps(result, indent=2) + '\n')
        print('Live SDL input: pass (' + str(len(rows)) + ' foreground frames)')
    finally:
        if app and app.poll() is None:
            app.terminate(); app.wait(timeout=5)
        if keyboard: keyboard.x.XCloseDisplay(keyboard.d)
        server.terminate(); server.wait(timeout=5)


if __name__ == '__main__':
    main()
