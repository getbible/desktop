#!/usr/bin/env python3
"""Install one Linux desktop profile for the current user without root access."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from config_core import ConfigurationError, load_configuration


ICON_SIZES = (16, 32, 48, 64, 128, 256, 512)


def data_home() -> Path:
    value = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share")
    if not value.is_absolute():
        raise ConfigurationError("XDG_DATA_HOME must be an absolute path")
    return value


def desktop_string(value: str) -> str:
    """Escape a Desktop Entry value, including the separate Exec escape layer."""
    return value.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")


def exec_argument(value: str) -> str:
    quoted = '"' + "".join(
        "\\" + character if character in '\\`"$' else "%%" if character == "%" else character
        for character in value
    ) + '"'
    return desktop_string(quoted)


def atomic_copy(source: Path, destination: Path, mode: int) -> None:
    """Replace even a running binary by rename, without following target links."""
    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{destination.name}.", dir=destination.parent)
    temporary = Path(temporary_name)
    os.close(descriptor)
    try:
        shutil.copyfile(source, temporary)
        temporary.chmod(mode)
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)


def refresh_desktop(base: Path) -> None:
    commands = (
        ("update-desktop-database", [str(base / "applications")], base / "applications"),
        ("gtk-update-icon-cache", ["--force", "--ignore-theme-index", str(base / "icons/hicolor")], base / "icons/hicolor"),
    )
    for name, arguments, directory in commands:
        executable = shutil.which(name)
        if executable and directory.is_dir():
            subprocess.run([executable, *arguments], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("install", "uninstall"))
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--config", type=Path, default=Path("config/app.json"))
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args(argv)
    try:
        root = args.root.resolve()
        config_path = args.config if args.config.is_absolute() else root / args.config
        if not config_path.is_file():
            raise ConfigurationError(f"Configuration file does not exist: {config_path}")
        app = load_configuration(config_path)["app"]
        base = data_home()
        binary = Path.home() / ".local/bin/images" / app["slug"]
        launcher = base / "applications" / f"{app['id']}.desktop"
        metainfo = base / "metainfo" / f"{app['id']}.metainfo.xml"
        icons = {size: base / "icons/hicolor" / f"{size}x{size}/apps/{app['id']}.png" for size in ICON_SIZES}
        if args.action == "uninstall":
            # Deliberately no directory removal: profile data and caches survive.
            for target in (binary, launcher, metainfo, *icons.values()):
                target.unlink(missing_ok=True)
            refresh_desktop(base)
            print(f"Removed {app['name']}; saved sessions, website data and cache were kept.")
            return 0

        build_dir = args.build_dir if args.build_dir is not None else root / "build"
        source_binary = build_dir / app["slug"]
        generated = root / "generated"
        source_launcher = generated / "packaging/linux" / launcher.name
        source_metainfo = generated / "packaging/linux" / metainfo.name
        source_icons = {size: generated / "assets" / f"icon-{size}.png" for size in ICON_SIZES}
        for source in (source_binary, source_launcher, source_metainfo, *source_icons.values()):
            if not source.is_file():
                raise ConfigurationError(f"Build output is missing: {source}")
        if not os.access(source_binary, os.X_OK):
            raise ConfigurationError(f"Built application is not executable: {source_binary}")

        # Construct a launcher that works immediately without changing PATH.
        lines = source_launcher.read_text(encoding="utf-8").splitlines()
        lines = [
            f"Exec={exec_argument(str(binary))}" if line.startswith("Exec=")
            else f"Icon={desktop_string(str(icons[256]))}" if line.startswith("Icon=")
            else line for line in lines
        ]
        with tempfile.TemporaryDirectory(prefix="webapp-launcher-") as temporary_directory:
            prepared_launcher = Path(temporary_directory) / launcher.name
            prepared_launcher.write_text("\n".join(lines) + "\n", encoding="utf-8")
            for size, destination in icons.items():
                atomic_copy(source_icons[size], destination, 0o644)
            atomic_copy(source_metainfo, metainfo, 0o644)
            atomic_copy(source_binary, binary, 0o755)
            atomic_copy(prepared_launcher, launcher, 0o644)
        refresh_desktop(base)
        print(f"Installed {app['name']}: {binary}")
        return 0
    except (ConfigurationError, OSError, ValueError) as error:
        print(f"Local installation error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
