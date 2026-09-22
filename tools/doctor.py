#!/usr/bin/env python3
"""Read-only checks for the native build; never installs dependencies."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
from embed_game_assets import checked, checked_font

ROOT = Path(__file__).resolve().parents[1]


def main():
    failures = []
    commands = ("cmake", "ninja", "c++", "ctest", "pkg-config")
    for command in commands:
        location = shutil.which(command)
        print(f"{'OK' if location else 'MISSING'} {command}: {location or 'not on PATH'}")
        if not location:
            failures.append(command)
    if shutil.which("pkg-config"):
        result = subprocess.run(["pkg-config", "--modversion", "sdl3"],
                                capture_output=True, text=True)
        print(f"{'OK SDL3:' if result.returncode == 0 else 'MISSING SDL3 pkg-config package:'} "
              f"{(result.stdout or result.stderr).strip()}")
        if result.returncode:
            failures.append("SDL3 development package (or configure SDL3_DIR manually)")
    manifest_path = ROOT / 'assets/manifest.json'
    try:
        manifest = json.loads(manifest_path.read_text())
        for entry in manifest['regions']:
            checked(manifest_path.parent / entry['file'], entry)
        checked_font(manifest_path.parent / manifest['font']['file'], manifest['font'])
        print('OK embedded game assets')
    except (OSError, ValueError, KeyError) as error:
        failures.append(f'embedded game assets: {error}')
    speech_manifest_path = ROOT / 'assets/audio/speech/speech_assets.json'
    try:
        speech_manifest = json.loads(speech_manifest_path.read_text())
        speech_blob = speech_manifest_path.with_name('speech_assets.bin').read_bytes()
        if hashlib.sha256(speech_blob).hexdigest() != speech_manifest['blob_sha256']:
            raise ValueError('speech_assets.bin')
        print('OK embedded speech assets')
    except (OSError, ValueError, KeyError) as error:
        failures.append(f'embedded speech assets: {error}')
    sid_prefix = ROOT / "build/deps/libresidfp"
    sid_ok = (sid_prefix / "lib/libresidfp.a").is_file() and (sid_prefix / "include/residfp/residfp.h").is_file()
    print(f"{'OK' if sid_ok else 'BUILD WILL PREPARE'} pinned SID library (make audio-setup)")
    if failures:
        print("Resolve missing requirements before building: " + ", ".join(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
