"""Regression tests for Linux runtime isolation and launcher behavior."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "package_linux", ROOT / "tools/package_linux.py"
)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("Cannot import Linux packager")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class LinuxPackageTests(unittest.TestCase):
    @unittest.skipIf(os.name == "nt", "Archive permissions and ldd paths are POSIX")
    def test_archive_keeps_runtime_libraries_not_glibc_and_copies_notices(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "ghostbusters"
            executable.write_bytes(b"game")
            host = [
                "libc.so.6",
                "libm.so.6",
                "libmvec.so.1",
                "libpthread.so.0",
                "libdl.so.2",
                "librt.so.1",
                "libresolv.so.2",
                "libutil.so.1",
                "libnss_dns.so.2",
                "ld-linux-x86-64.so.2",
            ]
            runtime = ["libstdc++.so.6", "libgcc_s.so.1", "libSDL3.so.0"]
            lines = []
            for name in host + runtime:
                library = root / name
                library.write_bytes(name.encode())
                lines.append(f"{name} => {library} (0x1234)")
            lines.append(f"{root}/ld-linux-x86-64.so.2 (0x5678)")

            sid_notice = root / "sid-copying"
            sid_notice.write_text("SID license\n", encoding="utf-8")
            sdl_notice = root / "sdl-license"
            sdl_notice.write_text("SDL license\n", encoding="utf-8")
            result = subprocess.CompletedProcess([], 0, "\n".join(lines), "")
            archive = root / "release.zip"
            notices = (
                (sid_notice, "COPYING.libresidfp"),
                (sdl_notice, "COPYING.SDL3"),
            )
            with patch.object(PACKAGE, "_run", return_value=result):
                PACKAGE.package(executable, archive, "strip", notices)

            with zipfile.ZipFile(archive) as bundled:
                self.assertEqual(
                    set(bundled.namelist()),
                    {
                        "ghostbusters",
                        "bin/ghostbusters",
                        "README.txt",
                        "COPYING.libresidfp",
                        "COPYING.SDL3",
                        *(f"lib/{name}" for name in runtime),
                    },
                )
                mode = bundled.getinfo("ghostbusters").external_attr >> 16
                self.assertTrue(mode & 0o111)
                self.assertEqual(
                    bundled.read("COPYING.libresidfp"), b"SID license\n"
                )
                self.assertEqual(bundled.read("COPYING.SDL3"), b"SDL license\n")
                launcher = bundled.read("ghostbusters").decode()
                self.assertNotIn("ld-linux", launcher)
                self.assertNotIn("--library-path", launcher)

    @unittest.skipIf(os.name == "nt", "Launcher requires a POSIX shell")
    def test_launcher_handles_spaces_arguments_and_existing_library_path(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(prefix="ghostbusters bundle ") as temporary:
            root = Path(temporary)
            (root / "bin").mkdir()
            game = root / "bin/ghostbusters"
            game.write_text(
                '#!/bin/sh\nprintf "%s\\n" "$LD_LIBRARY_PATH" "$@"\n',
                encoding="utf-8",
            )
            game.chmod(0o755)
            launcher = root / "ghostbusters"
            PACKAGE._write_launcher(launcher)
            for previous in (None, "", "/custom/lib:/another/lib"):
                with self.subTest(previous=previous):
                    env = dict(os.environ)
                    env.pop("LD_LIBRARY_PATH", None)
                    if previous is not None:
                        env["LD_LIBRARY_PATH"] = previous
                    result = subprocess.run(
                        [str(launcher), "argument with spaces", "--smoke-test"],
                        env=env,
                        cwd="/",
                        check=True,
                        capture_output=True,
                        text=True,
                    )
                    expected = str(root / "lib") + (
                        f":{previous}" if previous else ""
                    )
                    self.assertEqual(
                        result.stdout.splitlines(),
                        [expected, "argument with spaces", "--smoke-test"],
                    )

    def test_missing_dependencies_are_reported(self) -> None:
        with self.assertRaisesRegex(PACKAGE.PackagingError, "missing library"):
            PACKAGE._parse_ldd("libSDL3.so.0 => not found")

    def test_dependency_notices_are_required_and_must_exist(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "ghostbusters"
            executable.write_bytes(b"game")
            with self.assertRaisesRegex(PACKAGE.PackagingError, "notice is required"):
                PACKAGE.package(executable, root / "empty.zip", "strip", ())
            with self.assertRaisesRegex(PACKAGE.PackagingError, "does not exist"):
                PACKAGE.package(
                    executable,
                    root / "missing.zip",
                    "strip",
                    ((root / "missing-license", "COPYING.missing"),),
                )

    def test_notice_names_cannot_escape_archive_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "ghostbusters"
            executable.write_bytes(b"game")
            notice = root / "COPYING"
            notice.write_text("license\n", encoding="utf-8")
            with self.assertRaisesRegex(PACKAGE.PackagingError, "plain file names"):
                PACKAGE.package(
                    executable,
                    root / "release.zip",
                    "strip",
                    ((notice, "../COPYING"),),
                )


if __name__ == "__main__":
    unittest.main()
