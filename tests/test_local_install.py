"""Exercise Linux CLI installation with real metadata and a fake native build."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]


class LocalInstallTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="desktop local install ")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.root = self.directory / "template"
        self.root.mkdir()
        for name in ("tools", "scripts", "branding", "config"):
            shutil.copytree(REPOSITORY / name, self.root / name)
        self.home = self.directory / "home with spaces $cash%"
        self.home.mkdir()
        self.data = self.directory / "custom data"
        self.cache = self.directory / "custom cache"
        self.build = self.directory / "external build"
        self.build.mkdir()
        self.config = self.directory / "profiles" / "alpha.json"
        self.config.parent.mkdir()
        profile = json.loads((self.root / "config/app.json").read_text())
        profile["app"].update({"name": "Alpha", "slug": "alpha-app", "id": "org.example.alpha"})
        self.config.write_text(json.dumps(profile))
        self.bin_dir = self.home / ".local/bin/images"
        self.binary = self.bin_dir / "alpha-app"
        self.launcher = self.data / "applications/org.example.alpha.desktop"
        self.mock_dir = self.directory / "mock commands"
        self.mock_dir.mkdir()
        self.cmake_log = self.directory / "cmake.jsonl"
        fake_cmake = self.mock_dir / "cmake"
        fake_cmake.write_text(f"#!{sys.executable}\n" + r'''
import json, os, sys
from pathlib import Path
with open(os.environ['TEST_CMAKE_LOG'], 'a') as output:
    output.write(json.dumps(sys.argv[1:]) + '\n')
if sys.argv[1] == '--build':
    config = json.loads((Path(os.environ['TEST_ROOT']) / 'generated/config.resolved.json').read_text())
    binary = Path(sys.argv[2]) / config['app']['slug']
    binary.parent.mkdir(parents=True, exist_ok=True)
    binary.write_text('#!/bin/sh\nprintf "fixture binary\\n"\n')
    binary.chmod(0o755)
''')
        fake_cmake.chmod(0o755)
        (self.root / "scripts/bootstrap.sh").write_text("#!/bin/sh\nexit 0\n")
        self.environment = {key: value for key, value in os.environ.items() if not key.startswith("WEBAPP_")}
        self.environment.update({
            "HOME": str(self.home), "XDG_DATA_HOME": str(self.data), "XDG_CACHE_HOME": str(self.cache),
            "BUILD_DIR": str(self.build), "PYTHON_BIN": sys.executable,
            "WEBAPP_CONFIG_FILE": str(self.config),
            "PATH": str(self.mock_dir) + os.pathsep + os.environ.get("PATH", ""),
            "TEST_CMAKE_LOG": str(self.cmake_log), "TEST_ROOT": str(self.root),
        })

    def run_script(self, name: str, *arguments: str, check: bool = True) -> subprocess.CompletedProcess:
        result = subprocess.run(
            ["bash", str(self.root / "scripts" / name), *arguments],
            cwd=self.directory, env=self.environment, capture_output=True, text=True,
        )
        if check:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def seed_binary(self) -> None:
        source = self.build / "alpha-app"
        source.write_text("#!/bin/sh\nprintf 'existing binary\\n'\n")
        source.chmod(0o755)

    def test_local_install_and_uninstall_preserve_profiles(self) -> None:
        self.seed_binary()
        self.run_script("install-linux.sh", "--local")
        self.assertEqual(self.binary.read_bytes(), (self.build / "alpha-app").read_bytes())
        self.assertTrue(os.access(self.binary, os.X_OK))
        launcher = self.launcher.read_text()
        # Exec is a quoted Desktop Entry argument, not a shell command.
        self.assertIn('Exec="', launcher)
        self.assertIn('home with spaces \\\\$cash%%/.local/bin/images/alpha-app"', launcher)
        self.assertIn(f"Icon={self.data}/icons/hicolor/256x256/apps/org.example.alpha.png", launcher)
        self.assertFalse(self.cmake_log.exists(), "Installing an existing binary must not invoke CMake")
        for base, name in ((self.data, "cookies.sqlite"), (self.cache, "cached-resource")):
            directory = base / "org.example.alpha"
            directory.mkdir(parents=True)
            (directory / name).write_text("keep")
        unrelated = self.bin_dir / "other-app"
        unrelated.write_text("keep")
        # Removal does not need icons, generated output or the compiled build.
        shutil.rmtree(self.root / "generated")
        shutil.rmtree(self.build)
        self.run_script("uninstall-linux.sh", "--local")
        self.assertFalse(self.binary.exists())
        self.assertFalse(self.launcher.exists())
        self.assertFalse((self.data / "icons/hicolor/256x256/apps/org.example.alpha.png").exists())
        self.assertEqual((self.data / "org.example.alpha/cookies.sqlite").read_text(), "keep")
        self.assertEqual((self.cache / "org.example.alpha/cached-resource").read_text(), "keep")
        self.assertEqual(unrelated.read_text(), "keep")

    def test_build_install_flag_uses_explicit_external_config_and_build_directory(self) -> None:
        # The explicit CLI profile must win over an inherited config selection.
        self.environment["WEBAPP_CONFIG_FILE"] = str(self.root / "config/app.json")
        selected_build = self.directory / "different external build"
        self.run_script(
            "build-linux.sh", "--config", "profiles/alpha.json",
            "--build-dir", str(selected_build), "--install",
        )
        self.assertTrue(self.binary.is_file())
        self.assertTrue((selected_build / "alpha-app").is_file())
        self.assertFalse((self.root / "build").exists())
        invocations = [json.loads(line) for line in self.cmake_log.read_text().splitlines()]
        self.assertEqual(len(invocations), 2)
        self.assertEqual(invocations[1], ["--build", str(selected_build), "--parallel"])

    def test_system_install_preserves_prefix_behavior(self) -> None:
        self.seed_binary()
        prefix = self.directory / "system prefix"
        self.environment["PREFIX"] = str(prefix)
        self.run_script("install-linux.sh")
        invocations = [json.loads(line) for line in self.cmake_log.read_text().splitlines()]
        self.assertEqual(invocations, [["--install", str(self.build), "--prefix", str(prefix)]])
        self.assertFalse(self.binary.exists())

    def test_unknown_flags_and_missing_config_fail_before_build(self) -> None:
        for script in ("build-linux.sh", "install-linux.sh", "uninstall-linux.sh"):
            with self.subTest(script=script):
                self.assertEqual(self.run_script(script, "--unknown", check=False).returncode, 2)
                self.assertEqual(self.run_script(script, "--config", "missing.json", check=False).returncode, 2)
        self.assertFalse(self.cmake_log.exists())
        self.assertFalse(self.binary.exists())


if __name__ == "__main__":
    unittest.main()
