#!/usr/bin/env python3
"""Build the pinned SID chip library locally; no alternate game data is used."""
from pathlib import Path
import argparse
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
URL = 'https://github.com/libsidplayfp/libresidfp.git'
REVISION = '7c54a5988f9f1918439ee1180316a78c7a7729bb'
# v1.2.2, GPL-2.0-or-later; retain the upstream COPYING for distribution.
# Only the chip library is fetched. Music/speech are checked-in game assets;
# direct CMake builds require an existing installation via SID_PREFIX.


def run(*args, cwd=ROOT):
    command = [str(arg) for arg in args]
    if sys.platform == 'win32':
        # Native MinGW Python cannot execute an Autoconf script directly.
        # Resolve bash explicitly so Windows cannot select its WSL launcher.
        bash = shutil.which('bash')
        if bash is None:
            raise RuntimeError('MSYS2 bash is required on PATH for the SID build')
        command = [bash, '-c', 'exec "$@"', 'bash', *command]
    subprocess.run(command, cwd=cwd, check=True)


def shell_path(path):
    if sys.platform == 'win32':
        return subprocess.check_output(
            ['cygpath', '-u', str(path)], text=True).strip()
    return str(path)


def copy_license(source, prefix):
    destination = prefix / 'share/licenses/libresidfp/COPYING'
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source / 'COPYING', destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', type=Path, default=ROOT / 'build/deps/libresidfp')
    args = parser.parse_args()
    PREFIX = args.prefix.resolve()
    SOURCE = PREFIX.parent / 'libresidfp-src'
    marker = f'{URL}\nv1.2.2\n{REVISION}\nGPL-2.0-or-later; see source COPYING\n'
    installed = ((PREFIX / 'lib/libresidfp.a').is_file() and
                 (PREFIX / 'include/residfp/residfp.h').is_file() and
                 (PREFIX / 'SOURCE.txt').is_file() and
                 (PREFIX / 'SOURCE.txt').read_text() == marker)
    license_file = PREFIX / 'share/licenses/libresidfp/COPYING'
    if installed and license_file.is_file():
        print(f'Pinned SID library ready: {PREFIX}')
        return
    SOURCE.parent.mkdir(parents=True, exist_ok=True)
    if not SOURCE.exists():
        run('git', 'clone', '--config', 'core.autocrlf=false', '--depth', '1',
            '--branch', 'v1.2.2', URL, shell_path(SOURCE))
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=SOURCE, text=True).strip()
    if revision != REVISION:
        raise RuntimeError('Unexpected SID library revision; refusing to build a different source')
    run('git', 'diff', '--exit-code', 'HEAD', cwd=SOURCE)
    if installed:
        copy_license(SOURCE, PREFIX)
        print(f'Pinned SID library ready: {PREFIX}')
        return
    run('autoreconf', '-fi', cwd=SOURCE)
    run('./configure', '--disable-tests', '--disable-shared',
        f'--prefix={shell_path(PREFIX)}', cwd=SOURCE)
    run('make', '-j4', cwd=SOURCE)
    run('make', 'install', cwd=SOURCE)
    copy_license(SOURCE, PREFIX)
    (PREFIX / 'SOURCE.txt').write_text(marker)
    print(f'Installed pinned SID chip library to {PREFIX}')


if __name__ == '__main__':
    main()
