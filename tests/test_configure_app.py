#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
import sys
sys.path.insert(0, str(ROOT / "tools"))

import config_generate  # noqa: E402
import configure_app  # noqa: E402


class ConfigureAppTests(unittest.TestCase):
    def test_missing_configuration_file_uses_getbible_defaults(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config_path = Path(directory) / "missing.json"
            with patch.dict(os.environ, {}, clear=True):
                config = configure_app.load_configuration(config_path)

        self.assertEqual("getBible.Life", config["app"]["name"])
        self.assertEqual("getbible-life", config["app"]["slug"])
        self.assertEqual("live.getbible.app", config["app"]["id"])
        self.assertTrue(config["app"]["persistent"])
        self.assertEqual("1.0.0", config["app"]["version"])
        self.assertEqual("https://app.getbible.life/", config["app"]["url"])
        self.assertEqual("https://app.getbible.life/", config["app"]["homepage_url"])
        self.assertEqual(["https://app.getbible.life"], config["app"]["allowed_origins"])
        self.assertFalse(config["window"]["show_app_menu"])
        self.assertEqual("getbible-life", config["snap"]["name"])
        self.assertEqual("x64", config["windows"]["architecture"])
        self.assertEqual("11.0", config["macos"]["minimum_system_version"])

    def test_missing_values_use_getbible_defaults(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config_path = Path(directory) / "partial.json"
            config_path.write_text(json.dumps({"app": {"version": "2.1.0"}}), encoding="utf-8")
            with patch.dict(os.environ, {}, clear=True):
                config = configure_app.load_configuration(config_path)

        self.assertEqual("getBible.Life", config["app"]["name"])
        self.assertEqual("live.getbible.app", config["app"]["id"])
        self.assertEqual("https://app.getbible.life/", config["app"]["url"])
        self.assertEqual("https://app.getbible.life/", config["app"]["homepage_url"])
        self.assertFalse(config["window"]["show_app_menu"])
        self.assertEqual("x64", config["windows"]["architecture"])
        self.assertEqual("11.0", config["macos"]["minimum_system_version"])
        self.assertEqual("2.1.0", config["app"]["version"])

    def test_empty_environment_values_do_not_override_file_or_defaults(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config_path = Path(directory) / "partial.json"
            config_path.write_text(json.dumps({"app": {"version": "2.2.0"}}), encoding="utf-8")
            environment = {
                "WEBAPP_CONFIG_JSON": "",
                "WEBAPP_NAME": "",
                "WEBAPP_VERSION": "",
                "WEBAPP_URL": "",
                "WEBAPP_HOMEPAGE_URL": "",
                "WEBAPP_ALLOWED_ORIGINS": "",
                "WEBAPP_PERSISTENT": "",
                "WEBAPP_SNAP_NAME": "",
            }
            with patch.dict(os.environ, environment, clear=True):
                config = configure_app.load_configuration(config_path)

        self.assertEqual("getBible.Life", config["app"]["name"])
        self.assertEqual("2.2.0", config["app"]["version"])
        self.assertEqual("https://app.getbible.life/", config["app"]["url"])
        self.assertEqual("https://app.getbible.life/", config["app"]["homepage_url"])
        self.assertEqual(["https://app.getbible.life"], config["app"]["allowed_origins"])
        self.assertEqual("getbible-life", config["snap"]["name"])

    def test_persistent_flag_accepts_json_booleans(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config_path = Path(directory) / "app.json"
            for value in (True, False):
                with self.subTest(persistent=value):
                    config_path.write_text(json.dumps({"app": {"persistent": value}}), encoding="utf-8")
                    with patch.dict(os.environ, {}, clear=True):
                        config = configure_app.load_configuration(config_path)
                    self.assertIs(value, config["app"]["persistent"])

    def test_persistent_flag_rejects_non_boolean_json_values(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config_path = Path(directory) / "app.json"
            for value in ("true", "false", 0, 1, None, [], {}):
                with self.subTest(persistent=value):
                    config_path.write_text(json.dumps({"app": {"persistent": value}}), encoding="utf-8")
                    with patch.dict(os.environ, {}, clear=True):
                        with self.assertRaisesRegex(configure_app.ConfigurationError, "app.persistent must be boolean"):
                            configure_app.load_configuration(config_path)

    def test_persistent_environment_override_takes_precedence(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config_path = Path(directory) / "app.json"
            config_path.write_text(json.dumps({"app": {"persistent": True}}), encoding="utf-8")
            for override, expected in (("true", True), ("false", False), ("0", False), ("", True)):
                with self.subTest(override=override):
                    environment = {
                        "WEBAPP_CONFIG_JSON": json.dumps({"app": {"persistent": True}}),
                        "WEBAPP_PERSISTENT": override,
                    }
                    with patch.dict(os.environ, environment, clear=True):
                        config = configure_app.load_configuration(config_path)
                    self.assertIs(expected, config["app"]["persistent"])

            with patch.dict(os.environ, {"WEBAPP_PERSISTENT": "sometimes"}, clear=True):
                with self.assertRaisesRegex(configure_app.ConfigurationError, "Boolean override"):
                    configure_app.load_configuration(config_path)

    def test_generated_persistence_and_profile_identity_match_configuration(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config_path = root / "app.json"
            for enabled, app_id in ((True, "com.example.First"), (False, "org.example.Second")):
                with self.subTest(persistent=enabled, app_id=app_id):
                    config_path.write_text(json.dumps({"app": {"id": app_id, "persistent": enabled}}), encoding="utf-8")
                    with patch.dict(os.environ, {}, clear=True):
                        config = configure_app.load_configuration(config_path)
                        with patch.object(config_generate, "generate_icons"), patch("shutil.copy2"):
                            config_generate.generate(root, root / "generated", config)
                    header = (root / "generated/app_config.hpp").read_text(encoding="utf-8")
                    self.assertIn(f'inline constexpr std::string_view appID = "{app_id}";', header)
                    self.assertIn(f"inline constexpr bool persistent = {str(enabled).lower()};", header)
                    resolved = json.loads((root / "generated/config.resolved.json").read_text(encoding="utf-8"))
                    self.assertEqual(app_id, resolved["app"]["id"])
                    self.assertIs(enabled, resolved["app"]["persistent"])

    def test_environment_values_override_file_and_defaults(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config_path = Path(directory) / "app.json"
            config_path.write_text(
                json.dumps({
                    "app": {
                        "name": "File Portal",
                        "slug": "file-portal",
                        "id": "com.example.FilePortal",
                        "url": "https://file.example.com/",
                        "homepage_url": "https://file.example.com/",
                        "allowed_origins": ["https://file.example.com"],
                    },
                    "snap": {"name": "file-portal"},
                }),
                encoding="utf-8",
            )
            environment = {
                "WEBAPP_NAME": "Example Portal",
                "WEBAPP_SLUG": "example-portal",
                "WEBAPP_ID": "com.example.Portal",
                "WEBAPP_URL": "https://portal.example.com/",
                "WEBAPP_HOMEPAGE_URL": "https://portal.example.com/",
                "WEBAPP_ALLOWED_ORIGINS": "https://portal.example.com,https://login.example.com",
                "WEBAPP_SNAP_NAME": "example-portal",
            }
            with patch.dict(os.environ, environment, clear=True):
                config = configure_app.load_configuration(config_path)

        self.assertEqual("Example Portal", config["app"]["name"])
        self.assertEqual("example-portal", config["app"]["slug"])
        self.assertEqual("com.example.Portal", config["app"]["id"])
        self.assertEqual("1.0.0", config["app"]["version"])
        self.assertEqual("https://portal.example.com/", config["app"]["url"])
        self.assertEqual("https://portal.example.com/", config["app"]["homepage_url"])
        self.assertEqual(
            ["https://portal.example.com", "https://login.example.com"],
            config["app"]["allowed_origins"],
        )
        self.assertEqual("example-portal", config["snap"]["name"])

    def test_secret_json_is_deep_merged(self) -> None:
        secret = json.dumps({
            "app": {
                "name": "Private Portal",
                "slug": "private-portal",
                "id": "com.example.PrivatePortal",
                "url": "https://private.example.com/",
                "allowed_origins": ["https://private.example.com"],
            },
            "snap": {"name": "private-portal"},
        })
        with tempfile.TemporaryDirectory() as directory:
            config_path = Path(directory) / "app.json"
            config_path.write_text("{}", encoding="utf-8")
            with patch.dict(os.environ, {"WEBAPP_CONFIG_JSON": secret}, clear=True):
                config = configure_app.load_configuration(config_path)

        self.assertEqual("Private Portal", config["app"]["name"])
        self.assertEqual("1.0.0", config["app"]["version"])
        self.assertEqual("private-portal", config["snap"]["name"])

    def test_platform_environment_overrides(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config_path = Path(directory) / "app.json"
            config_path.write_text("{}", encoding="utf-8")
            environment = {
                "WEBAPP_WINDOWS_IDENTITY_NAME": "com.example.portal",
                "WEBAPP_WINDOWS_PUBLISHER": "CN=Example Publisher",
                "WEBAPP_WINDOWS_PUBLISHER_DISPLAY_NAME": "Example Publisher",
                "WEBAPP_WINDOWS_ARCHITECTURE": "arm64",
                "WEBAPP_MACOS_MINIMUM_SYSTEM_VERSION": "12.0",
                "WEBAPP_MACOS_CATEGORY": "public.app-category.productivity",
            }
            with patch.dict(os.environ, environment, clear=True):
                config = configure_app.load_configuration(config_path)

        self.assertEqual("arm64", config["windows"]["architecture"])
        self.assertEqual("CN=Example Publisher", config["windows"]["publisher"])
        self.assertEqual("12.0", config["macos"]["minimum_system_version"])

    def test_invalid_non_https_url_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config_path = Path(directory) / "app.json"
            config_path.write_text(json.dumps({"app": {"url": "http://example.com"}}), encoding="utf-8")
            with patch.dict(os.environ, {}, clear=True):
                with self.assertRaises(configure_app.ConfigurationError):
                    configure_app.load_configuration(config_path)

    def test_app_url_origin_is_automatically_allowed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config_path = Path(directory) / "app.json"
            config_path.write_text(json.dumps({
                "app": {
                    "url": "https://reader.example.org/app/",
                    "allowed_origins": ["https://auth.example.org"],
                }
            }), encoding="utf-8")
            with patch.dict(os.environ, {}, clear=True):
                config = configure_app.load_configuration(config_path)

        self.assertEqual(
            ["https://reader.example.org", "https://auth.example.org"],
            config["app"]["allowed_origins"],
        )

    def test_generated_internal_routes_use_scheme_base(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "generated"
            with patch.dict(os.environ, {}, clear=True):
                config = configure_app.load_configuration(root / "missing.json")
                with patch.object(config_generate, "generate_icons"), patch("shutil.copy2"):
                    config_generate.generate(root, output, config)

            header = (output / "app_config.hpp").read_text(encoding="utf-8")

        self.assertIn(
            'inline constexpr std::string_view shellURL = "webapp://app";',
            header,
        )
        self.assertIn(
            'inline constexpr std::string_view offlineURL = "webapp://app/offline";',
            header,
        )

    def test_external_configuration_resolves_icon_relative_to_profile(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            root = base / "repository"
            profile = base / "external" / "settings"
            branding = profile.parent / "branding"
            root.mkdir()
            profile.mkdir(parents=True)
            branding.mkdir()
            color = (19, 87, 203, 255)
            Image.new("RGBA", (32, 32), color).save(branding / "icon.png")
            config_path = profile / "app.json"
            config_path.write_text(json.dumps({"branding": {"icon": "../branding/icon.png"}}), encoding="utf-8")

            with patch.dict(os.environ, {}, clear=True):
                result = configure_app.main([
                    "--root", str(root), "--config", str(config_path), "--output", "generated",
                ])

            self.assertEqual(0, result)
            with Image.open(root / "generated/assets/icon-32.png") as icon:
                self.assertEqual(color, icon.getpixel((16, 16)))

    def test_profile_icon_takes_precedence_with_repository_root_fallback(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            profile = root / "config"
            profile.mkdir()
            relative = Path("branding") / "icon.png"
            (root / relative).parent.mkdir()
            (profile / relative).parent.mkdir()
            root_color = (200, 30, 40, 255)
            profile_color = (10, 130, 70, 255)
            Image.new("RGBA", (16, 16), root_color).save(root / relative)
            Image.new("RGBA", (16, 16), profile_color).save(profile / relative)
            config = {"branding": {"icon": str(relative)}}

            with patch.dict(os.environ, {}, clear=True):
                self.assertEqual(profile_color, config_generate.source_icon(root, config, profile).getpixel((0, 0)))
                (profile / relative).unlink()
                self.assertEqual(root_color, config_generate.source_icon(root, config, profile).getpixel((0, 0)))
                config["branding"]["icon"] = str(root / relative)
                self.assertEqual(root_color, config_generate.source_icon(root, config, profile).getpixel((0, 0)))


if __name__ == "__main__":
    unittest.main()
