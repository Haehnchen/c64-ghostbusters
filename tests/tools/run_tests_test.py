#!/usr/bin/env python3
"""Check test profile selection, failure reporting and timeout cleanup without replays."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch, MagicMock

spec = importlib.util.spec_from_file_location(
    'run_tests', Path(__file__).resolve().parents[2] / 'tools/run_tests.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class TestRunner(unittest.TestCase):
    def check_run(self, suite, exit_code, timeout=False, match=None, windows=False):
        with tempfile.TemporaryDirectory() as directory:
            process = MagicMock()
            process.__enter__.return_value = process
            process.pid = 12345
            process.wait.side_effect = [subprocess.TimeoutExpired('ctest', 120), -9] if timeout else [exit_code]
            argv = ['run_tests.py', '--reports', directory, '--suite', suite]
            if match:
                argv += ['--match', match]
            with patch('sys.argv', argv), \
                    patch.object(runner, 'IS_WINDOWS', windows), \
                    patch.object(runner.subprocess, 'Popen', return_value=process) as popen, \
                    patch.object(runner.subprocess, 'run') as run, \
                    patch.object(runner.os, 'killpg', create=True) as kill, \
                    patch.object(runner.signal, 'SIGKILL', 9, create=True), \
                    patch('builtins.print'):
                result = runner.main()
            report = next(Path(directory).glob('*/summary.json'))
            summary = json.loads(report.read_text())
            self.assertEqual(result, 124 if timeout else exit_code)
            self.assertEqual(summary['exit_code'], result)
            self.assertEqual(summary['timed_out'], timeout)
            self.assertEqual(summary['budget_seconds'],
                             {'quick': 120, 'extended': 900, 'all': 1800}[suite])
            self.assertTrue(report.with_name('ctest.log').exists())
            command = popen.call_args.args[0]
            options = popen.call_args.kwargs
            if windows:
                self.assertIn('creationflags', options)
                self.assertNotIn('start_new_session', options)
            else:
                self.assertTrue(options['start_new_session'])
                self.assertNotIn('creationflags', options)
            self.assertIn('--no-tests=error', command)
            if suite == 'all':
                self.assertNotIn('--label-regex', command)
            else:
                self.assertEqual(command[command.index('--label-regex') + 1], f'^{suite}$')
            if match:
                self.assertEqual(command[-2:], ['--tests-regex', match])
            else:
                self.assertNotIn('--tests-regex', command)
            self.assertEqual(kill.call_count, int(timeout and not windows))
            self.assertEqual(run.call_count, int(timeout and windows))
            if timeout and windows:
                self.assertEqual(run.call_args.args[0],
                                 ['taskkill', '/PID', '12345', '/T', '/F'])

    def test_profiles_and_failures(self):
        for suite in ('quick', 'extended', 'all'):
            for code in (0, 8):
                with self.subTest(suite=suite, code=code):
                    self.check_run(suite, code)

    def test_timeout_stops_children_and_retains_failure(self):
        with self.subTest(platform='posix'):
            self.check_run('quick', 124, timeout=True)
        with self.subTest(platform='windows'):
            self.check_run('quick', 124, timeout=True, windows=True)

    def test_focused_selection_retains_suite_and_failure_handling(self):
        self.check_run('quick', 0, match='zuul')
        self.check_run('extended', 0, match='^natural_campaign$')
        self.check_run('quick', 8, match='missing-test')


if __name__ == '__main__':
    unittest.main()
