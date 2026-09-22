#!/usr/bin/env python3
"""Bundle Ghostbusters runtime libraries while retaining the host glibc."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile


_ADDRESS = r"\(0x[0-9a-fA-F]+\)"
_DEPENDENCY = re.compile(
    rf"^(?P<name>[^\s/]+)\s+=>\s+(?P<path>/[^\s]+)\s+{_ADDRESS}$"
)
_ABSOLUTE = re.compile(rf"^(?P<path>/[^\s]+)\s+{_ADDRESS}$")
_PSEUDO = re.compile(rf"^(?:linux-vdso|linux-gate)[^\s/]*\s+{_ADDRESS}$")
# Desktop plugins and graphics drivers must use their host's glibc and loader.
_HOST_GLIBC = re.compile(
    r"^(?:ld-linux[^/]*|ld64\.so\.\d+|"
    r"lib(?:c|m|mvec|pthread|dl|rt|resolv|util|anl|BrokenLocale|"
    r"thread_db|nss_[^/]+)\.so(?:\.\d+)*)$"
)


class PackagingError(RuntimeError):
    """An input could not be turned into a runnable archive."""


def _run(command: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            env={**os.environ, "LC_ALL": "C"},
        )
    except OSError as error:
        raise PackagingError(f"could not run {command[0]!r}: {error}") from error
    if check and result.returncode:
        detail = result.stderr.strip() or result.stdout.strip()
        raise PackagingError(
            f"{command[0]} failed with exit status {result.returncode}"
            + (f": {detail}" if detail else "")
        )
    return result


def _parse_ldd(output: str) -> tuple[dict[str, Path], list[Path]]:
    """Return SONAME-to-path entries and absolute loader candidates."""
    dependencies: dict[str, Path] = {}
    loaders: list[Path] = []
    for number, raw_line in enumerate(output.splitlines(), 1):
        line = raw_line.strip()
        if not line:
            continue
        if line in {"statically linked", "not a dynamic executable"}:
            continue

        dependency = _DEPENDENCY.fullmatch(line)
        if dependency:
            name = dependency.group("name")
            source = Path(dependency.group("path"))
            if name in dependencies and dependencies[name] != source:
                raise PackagingError(
                    f"ldd reports SONAME {name!r} at two paths: "
                    f"{dependencies[name]} and {source}"
                )
            dependencies[name] = source
            continue

        if re.fullmatch(r"[^\s/]+\s+=>\s+not\s+found", line):
            raise PackagingError(f"missing library reported by ldd: {line}")

        absolute = _ABSOLUTE.fullmatch(line)
        if absolute:
            loaders.append(Path(absolute.group("path")))
            continue

        if _PSEUDO.fullmatch(line):
            continue

        raise PackagingError(
            f"unrecognized ldd output on line {number}: {raw_line!r}"
        )

    return dependencies, loaders


def _resolved_file(path: Path, description: str) -> Path:
    try:
        resolved = path.resolve(strict=True)
    except OSError as error:
        raise PackagingError(f"{description} does not exist: {path}") from error
    if not resolved.is_file():
        raise PackagingError(f"{description} is not a regular file: {path}")
    return resolved


def _write_launcher(path: Path) -> None:
    path.write_text(
        "#!/bin/sh\n"
        "set -eu\n"
        'root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
        'export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
        'exec "$root/bin/ghostbusters" "$@"\n',
        encoding="utf-8",
    )
    path.chmod(0o755)


def _zip_tree(stage: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(path for path in stage.rglob("*") if path.is_file()):
            archive.write(path, path.relative_to(stage))


def package(
    executable: Path,
    output: Path,
    strip_tool: str,
    notices: tuple[tuple[Path, str], ...],
) -> None:
    executable = _resolved_file(executable, "executable")
    checked_notices = tuple(
        (_resolved_file(source, f"notice {name!r}"), name)
        for source, name in notices
    )
    if not checked_notices:
        raise PackagingError("at least one dependency notice is required")
    if any(Path(name).name != name or not name for _, name in checked_notices):
        raise PackagingError("notice archive names must be plain file names")

    ldd = _run(["ldd", str(executable)])
    dependencies, _ = _parse_ldd(ldd.stdout)
    dependencies = {
        name: source
        for name, source in dependencies.items()
        if not _HOST_GLIBC.fullmatch(name)
    }

    with tempfile.TemporaryDirectory(prefix="ghostbusters-linux-") as temporary:
        stage = Path(temporary)
        bin_dir = stage / "bin"
        lib_dir = stage / "lib"
        bin_dir.mkdir()
        lib_dir.mkdir()

        game = bin_dir / "ghostbusters"
        shutil.copy2(executable, game)
        _run([strip_tool, str(game)])

        for soname, source in sorted(dependencies.items()):
            destination = lib_dir / soname
            shutil.copy2(_resolved_file(source, f"library {soname!r}"), destination)

        for notice, name in checked_notices:
            shutil.copy2(notice, stage / name)

        _write_launcher(stage / "ghostbusters")
        (stage / "README.txt").write_text(
            "Ghostbusters bundled Linux runtime\n"
            "\n"
            "Run the game from this directory:\n"
            "    ./ghostbusters\n"
            "\n"
            "F1 starts a game; F11 toggles fullscreen; Escape exits.\n"
            "\n"
            "A compatible desktop session and the system graphics and audio "
            "drivers are still required.\n",
            encoding="utf-8",
        )
        _zip_tree(stage, output)


def _arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--strip", dest="strip_tool", default="strip")
    parser.add_argument(
        "--notice",
        nargs=2,
        action="append",
        default=[],
        metavar=("SOURCE", "ARCHIVE_NAME"),
        help="dependency notice to copy to the archive root (repeatable)",
    )
    return parser.parse_args()


def main() -> int:
    args = _arguments()
    notices = tuple((Path(source), name) for source, name in args.notice)
    try:
        package(args.executable, args.output, args.strip_tool, notices)
    except PackagingError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
