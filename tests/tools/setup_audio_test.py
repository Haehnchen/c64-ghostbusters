#!/usr/bin/env python3
"""Check native-Windows command adaptation and SID license installation."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    'setup_audio', Path(__file__).resolve().parents[2] / 'tools/setup_audio.py')
setup_audio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup_audio)


class SetupAudio(unittest.TestCase):
    def test_windows_commands_run_through_msys2_bash(self):
        with patch.object(setup_audio.sys, 'platform', 'win32'), \
                patch.object(setup_audio.shutil, 'which', return_value='C:/msys64/usr/bin/bash.exe'), \
                patch.object(setup_audio.subprocess, 'run') as run:
            setup_audio.run('./configure', '--disable-shared', cwd=Path('source'))
        self.assertEqual(run.call_args.args[0], [
            'C:/msys64/usr/bin/bash.exe', '-c', 'exec "$@"', 'bash',
            './configure', '--disable-shared'])
        self.assertEqual(run.call_args.kwargs['cwd'], Path('source'))
        self.assertTrue(run.call_args.kwargs['check'])

    def test_windows_prefix_uses_cygpath(self):
        path = Path('C:/cache/sid')
        with patch.object(setup_audio.sys, 'platform', 'win32'), \
                patch.object(setup_audio.subprocess, 'check_output', return_value='/c/cache/sid\n') as output:
            self.assertEqual(setup_audio.shell_path(path), '/c/cache/sid')
        self.assertEqual(output.call_args.args[0],
                         ['cygpath', '-u', str(path)])

    def test_license_is_copied_into_install_prefix(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source'
            prefix = root / 'prefix'
            source.mkdir()
            (source / 'COPYING').write_text('license text\n')
            setup_audio.copy_license(source, prefix)
            installed = prefix / 'share/licenses/libresidfp/COPYING'
            self.assertEqual(installed.read_text(), 'license text\n')


if __name__ == '__main__':
    unittest.main()
