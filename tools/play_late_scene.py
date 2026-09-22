#!/usr/bin/env python3
"""Fast-forward a natural game, then hand a late scene to the player."""

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests/integration/fixtures"
SCENES = ("marshmallow", "zuul", "victory", "defeat")


def compose_schedule(scene: str, fixtures: Path = FIXTURES) -> str:
    """Compose the same natural control schedules used by natural_zuul_test."""
    if scene not in SCENES:
        raise ValueError(f"Unknown late scene: {scene}")

    prefix = (fixtures / "natural_marshmallow.inputs").read_text(encoding="utf-8")
    if scene == "marshmallow":
        return prefix

    continuation = "natural_zuul_failure.inputs" if scene == "defeat" else "natural_zuul.inputs"
    return prefix + "\n" + (fixtures / continuation).read_text(encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scene", choices=SCENES, required=True)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    args = parser.parse_args()

    executable = args.build_dir.resolve() / ("ghostbusters.exe" if sys.platform == "win32" else "ghostbusters")
    if not executable.is_file():
        parser.error(f"native game not found: {executable}; build it first")

    print(f"Fast-forwarding a natural playthrough to {args.scene}.", flush=True)
    print("The accelerated replay may take a little while before control becomes live.", flush=True)
    if args.scene == "marshmallow":
        print("When control becomes live, press B to use bait.", flush=True)
    elif args.scene == "zuul":
        print("When control becomes live, use the arrow keys to move; Space is the action button.",
              flush=True)
    else:
        print("Control becomes live at the chosen ending.", flush=True)

    with tempfile.TemporaryDirectory(prefix="ghostbusters-late-scene-") as directory:
        schedule = Path(directory) / "natural.inputs"
        schedule.write_text(compose_schedule(args.scene), encoding="utf-8")
        result = subprocess.run(
            [str(executable), "--play-from", str(schedule), args.scene],
            cwd=ROOT,
        )
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
