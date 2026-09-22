#!/usr/bin/env python3
"""Check late-scene launcher schedule composition without running the game."""

import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "play_late_scene", ROOT / "tools" / "play_late_scene.py"
)
PLAY_LATE_SCENE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(PLAY_LATE_SCENE)


class ScheduleCompositionTest(unittest.TestCase):
    def setUp(self):
        fixtures = ROOT / "tests" / "integration" / "fixtures"
        self.prefix = (fixtures / "natural_marshmallow.inputs").read_text()
        self.victory = (fixtures / "natural_zuul.inputs").read_text()
        self.failure = (fixtures / "natural_zuul_failure.inputs").read_text()

    def test_marshmallow_uses_natural_prefix_only(self):
        self.assertEqual(PLAY_LATE_SCENE.compose_schedule("marshmallow"), self.prefix)

    def test_zuul_and_victory_use_tested_victory_route(self):
        expected = self.prefix + "\n" + self.victory
        self.assertEqual(PLAY_LATE_SCENE.compose_schedule("zuul"), expected)
        self.assertEqual(PLAY_LATE_SCENE.compose_schedule("victory"), expected)

    def test_defeat_uses_tested_failure_route(self):
        expected = self.prefix + "\n" + self.failure
        self.assertEqual(PLAY_LATE_SCENE.compose_schedule("defeat"), expected)


if __name__ == "__main__":
    unittest.main()
