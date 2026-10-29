#!/usr/bin/env python3
from __future__ import annotations

from config_core import *  # noqa: F401,F403
from config_generate import *  # noqa: F401,F403

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--config", type=Path, default=Path("config/app.json"))
    parser.add_argument("--output", type=Path, default=Path("generated"))
    parser.add_argument("--github-output", type=Path)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    config_path = args.config if args.config.is_absolute() else root / args.config
    out = args.output if args.output.is_absolute() else root / args.output
    try:
        config = load_configuration(config_path)
        generate(root, out, config, config_dir=config_path.parent)
        if args.github_output:
            with args.github_output.open("a", encoding="utf-8") as stream:
                outputs = {
                    "app_name": config["app"]["name"],
                    "app_slug": config["app"]["slug"],
                    "app_version": config["app"]["version"],
                    "artifact_name": f"{config['app']['slug']}-{config['app']['version']}",
                    "snap_name": config["snap"]["name"],
                    "windows_architecture": config["windows"]["architecture"],
                }
                for key, value in outputs.items():
                    stream.write(f"{key}={value}\n")
    except (ConfigurationError, OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"Configuration error: {exc}", file=sys.stderr)
        return 2
    print(
        f"Configured {config['app']['name']} {config['app']['version']} for "
        f"{config['app']['url']} (Snap: {config['snap']['name']})."
    )
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
