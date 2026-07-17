#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
import sys
sys.path.insert(0, str(ROOT / "tools"))

import configure_app  # noqa: E402


class ConfigureAppTests(unittest.TestCase):
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

    def test_environment_values_override_file_and_defaults(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config_path = Path(directory) / "app.json"
            config_path.write_text("{}", encoding="utf-8")
            environment = {
                "WEBAPP_NAME": "Example Portal",
                "WEBAPP_SLUG": "example-portal",
                "WEBAPP_ID": "com.example.Portal",
                "WEBAPP_URL": "https://portal.example.com/",
                "WEBAPP_ALLOWED_ORIGINS": "https://portal.example.com,https://login.example.com",
                "WEBAPP_SNAP_NAME": "example-portal",
            }
            with patch.dict(os.environ, environment, clear=True):
                config = configure_app.load_configuration(config_path)

        self.assertEqual("Example Portal", config["app"]["name"])
        self.assertEqual(
            ["https://portal.example.com", "https://login.example.com"],
            config["app"]["allowed_origins"],
        )

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


if __name__ == "__main__":
    unittest.main()
