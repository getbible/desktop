#!/usr/bin/env python3
"""Configuration loading and validation for website desktop applications."""
from __future__ import annotations

import argparse
import base64
import copy
import io
import json
import os
import re
import shlex
import sys
import textwrap
from pathlib import Path
from typing import Any
from urllib.parse import urlparse
from xml.sax.saxutils import escape as xml_escape

from PIL import Image

DEFAULT_CONFIG: dict[str, Any] = {
    "schema": 1,
    "app": {
        "name": "getBible.Life", "slug": "getbible-life", "id": "live.getbible.app",
        "persistent": True,
        "version": "1.0.0", "url": "https://app.getbible.life/",
        "homepage_url": "https://app.getbible.life/",
        "summary": "Read and study the Bible in a focused desktop application",
        "description": "getBible.Life is a lightweight desktop application for https://app.getbible.life/. It uses the operating system's native web engine, retains normal website cookies and local storage, and displays a reconnect page when the service is unavailable.",
        "about": "getBible.Life brings the getBible web application into a focused desktop window while retaining the security updates, rendering engine, cookies, and storage supplied by the operating system.",
        "allowed_origins": ["https://app.getbible.life"],
        "user_agent_product": "GetBibleLifeDesktop", "javascript_object": "GetBibleApp",
        "html_class": "getbible-life-desktop-app",
    },
    "window": {"width": 1200, "height": 800, "minimum_width": 720, "minimum_height": 520, "show_app_menu": False},
    "offline": {
        "title": "Connect to the internet",
        "message": "getBible.Life needs an internet connection. This page will reconnect automatically as soon as the service becomes available.",
        "retry_label": "Try again", "checking_message": "Checking the connection…",
        "connected_message": "Connected. Loading getBible.Life…",
        "failed_message": "Still offline. getBible.Life will retry automatically.",
    },
    "support": {"label": "Support", "url": "https://app.getbible.life/", "email": "", "message": "For help, use the support and feedback options available on the getBible.Life website."},
    "branding": {"icon": "branding/getbible-life.png.base64", "accent_color": "#d4af37", "background_color": "#111111"},
    "linux": {"generic_name": "Web Application", "categories": ["Network"], "keywords": ["Web", "Desktop", "Application"]},
    "snap": {"name": "getbible-life", "base": "core24", "grade": "stable", "confinement": "strict", "license": "GPL-3.0-or-later"},
    "windows": {"identity_name": "live.getbible.app", "publisher": "CN=Vast Development Method", "publisher_display_name": "Vast Development Method", "minimum_version": "10.0.17763.0", "architecture": "x64"},
    "macos": {"minimum_system_version": "11.0", "category": "public.app-category.reference", "copyright": "Copyright © 2026 Vast Development Method"},
}

ENV_MAP = {
    "WEBAPP_NAME": "app.name", "WEBAPP_SLUG": "app.slug", "WEBAPP_ID": "app.id", "WEBAPP_VERSION": "app.version",
    "WEBAPP_URL": "app.url", "WEBAPP_HOMEPAGE_URL": "app.homepage_url", "WEBAPP_SUMMARY": "app.summary",
    "WEBAPP_DESCRIPTION": "app.description", "WEBAPP_ABOUT": "app.about", "WEBAPP_USER_AGENT_PRODUCT": "app.user_agent_product",
    "WEBAPP_JAVASCRIPT_OBJECT": "app.javascript_object", "WEBAPP_HTML_CLASS": "app.html_class",
    "WEBAPP_SUPPORT_LABEL": "support.label", "WEBAPP_SUPPORT_URL": "support.url", "WEBAPP_SUPPORT_EMAIL": "support.email",
    "WEBAPP_SUPPORT_MESSAGE": "support.message", "WEBAPP_OFFLINE_TITLE": "offline.title", "WEBAPP_OFFLINE_MESSAGE": "offline.message",
    "WEBAPP_OFFLINE_RETRY_LABEL": "offline.retry_label", "WEBAPP_ICON": "branding.icon",
    "WEBAPP_ACCENT_COLOR": "branding.accent_color", "WEBAPP_BACKGROUND_COLOR": "branding.background_color",
    "WEBAPP_SNAP_NAME": "snap.name", "WEBAPP_SNAP_GRADE": "snap.grade", "WEBAPP_SNAP_CONFINEMENT": "snap.confinement",
    "WEBAPP_SNAP_LICENSE": "snap.license", "WEBAPP_WINDOWS_IDENTITY_NAME": "windows.identity_name",
    "WEBAPP_WINDOWS_PUBLISHER": "windows.publisher", "WEBAPP_WINDOWS_PUBLISHER_DISPLAY_NAME": "windows.publisher_display_name",
    "WEBAPP_WINDOWS_MINIMUM_VERSION": "windows.minimum_version", "WEBAPP_WINDOWS_ARCHITECTURE": "windows.architecture",
    "WEBAPP_MACOS_MINIMUM_SYSTEM_VERSION": "macos.minimum_system_version", "WEBAPP_MACOS_CATEGORY": "macos.category",
    "WEBAPP_MACOS_COPYRIGHT": "macos.copyright",
}

class ConfigurationError(ValueError):
    pass


def deep_merge(base: dict[str, Any], override: dict[str, Any]) -> dict[str, Any]:
    result = copy.deepcopy(base)
    for key, value in override.items():
        result[key] = deep_merge(result[key], value) if isinstance(value, dict) and isinstance(result.get(key), dict) else copy.deepcopy(value)
    return result


def set_path(config: dict[str, Any], path: str, value: Any) -> None:
    section, key = path.split(".", 1)
    config.setdefault(section, {})[key] = value


def parse_list(value: str) -> list[str]:
    value = value.strip()
    if value.startswith("["):
        parsed = json.loads(value)
        if not isinstance(parsed, list) or not all(isinstance(item, str) for item in parsed):
            raise ConfigurationError("List override must be a JSON string array")
        return parsed
    return [item.strip() for item in value.split(",") if item.strip()]


def parse_bool(value: str) -> bool:
    if value.lower() in {"1", "true", "yes", "on"}: return True
    if value.lower() in {"0", "false", "no", "off"}: return False
    raise ConfigurationError("Boolean override must be true or false")


def https_url(value: str, field: str, allow_empty: bool = False) -> str:
    value = value.strip()
    if allow_empty and not value: return ""
    parsed = urlparse(value)
    if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password:
        raise ConfigurationError(f"{field} must be an HTTPS URL without credentials")
    return parsed.geturl()


def origin(value: str, field: str) -> str:
    parsed = urlparse(value)
    if parsed.scheme != "https" or not parsed.hostname or parsed.path not in {"", "/"} or parsed.query or parsed.fragment:
        raise ConfigurationError(f"{field} must be an HTTPS origin")
    port = f":{parsed.port}" if parsed.port and parsed.port != 443 else ""
    return f"https://{parsed.hostname.lower()}{port}"


def normalize(config: dict[str, Any]) -> dict[str, Any]:
    if config.get("schema") != 1: raise ConfigurationError("Only configuration schema 1 is supported")
    app = config["app"]
    if not isinstance(app.get("persistent"), bool): raise ConfigurationError("app.persistent must be boolean")
    for key in ("name", "slug", "id", "version", "summary", "description", "about", "user_agent_product", "javascript_object", "html_class"):
        if not isinstance(app.get(key), str) or not app[key].strip(): raise ConfigurationError(f"app.{key} must be a non-empty string")
        app[key] = app[key].strip()
    app["slug"] = app["slug"].lower()
    app["html_class"] = app["html_class"].lower()
    if not re.fullmatch(r"[a-z][a-z0-9-]*[a-z0-9]", app["slug"]): raise ConfigurationError("app.slug is invalid")
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*(?:[.-][A-Za-z0-9]+)+", app["id"]): raise ConfigurationError("app.id is invalid")
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){0,3}(?:[-+][A-Za-z0-9.-]+)?", app["version"]): raise ConfigurationError("app.version is invalid")
    if len(app["summary"]) > 79: raise ConfigurationError("app.summary must not exceed 79 characters")
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", app["user_agent_product"]): raise ConfigurationError("app.user_agent_product is invalid")
    if not re.fullmatch(r"[A-Za-z_$][A-Za-z0-9_$]*", app["javascript_object"]): raise ConfigurationError("app.javascript_object is invalid")
    if not re.fullmatch(r"[a-z][a-z0-9-]*", app["html_class"]): raise ConfigurationError("app.html_class is invalid")
    app["url"] = https_url(str(app["url"]), "app.url")
    app["homepage_url"] = https_url(str(app.get("homepage_url") or app["url"]), "app.homepage_url")
    raw_origins = app.get("allowed_origins")
    if not isinstance(raw_origins, list): raise ConfigurationError("app.allowed_origins must be an array")
    origins = [origin(str(item), "app.allowed_origins") for item in raw_origins]
    app_origin = origin(f"https://{urlparse(app['url']).netloc}", "app.url")
    app["allowed_origins"] = list(dict.fromkeys([app_origin, *origins]))
    for section in ("offline", "support", "branding", "linux", "snap", "windows", "macos", "window"):
        if not isinstance(config.get(section), dict): raise ConfigurationError(f"{section} must be an object")
    config["support"]["url"] = https_url(str(config["support"].get("url", "")), "support.url", True)
    if config["support"].get("email") and not re.fullmatch(r"[^\s@]+@[^\s@]+\.[^\s@]+", str(config["support"]["email"])): raise ConfigurationError("support.email is invalid")
    for color in ("accent_color", "background_color"):
        if not re.fullmatch(r"#[0-9A-Fa-f]{6}", str(config["branding"].get(color, ""))): raise ConfigurationError(f"branding.{color} is invalid")
    if not isinstance(config["window"].get("show_app_menu"), bool): raise ConfigurationError("window.show_app_menu must be boolean")
    for key, minimum in (("width", 320), ("height", 240), ("minimum_width", 320), ("minimum_height", 240)):
        if not isinstance(config["window"].get(key), int) or config["window"][key] < minimum: raise ConfigurationError(f"window.{key} is invalid")
    if config["windows"].get("architecture") not in {"x64", "arm64"}: raise ConfigurationError("windows.architecture must be x64 or arm64")
    if not str(config["windows"].get("publisher", "")).startswith("CN="): raise ConfigurationError("windows.publisher must begin with CN=")
    if config["snap"].get("grade") not in {"stable", "devel"}: raise ConfigurationError("snap.grade is invalid")
    if config["snap"].get("confinement") not in {"strict", "devmode", "classic"}: raise ConfigurationError("snap.confinement is invalid")
    return config


def load_configuration(config_path: Path) -> dict[str, Any]:
    config = copy.deepcopy(DEFAULT_CONFIG)
    if config_path.exists():
        loaded = json.loads(config_path.read_text(encoding="utf-8"))
        if not isinstance(loaded, dict): raise ConfigurationError("Configuration must be a JSON object")
        config = deep_merge(config, loaded)
    secret = os.environ.get("WEBAPP_CONFIG_JSON", "").strip()
    if secret:
        loaded = json.loads(secret)
        if not isinstance(loaded, dict): raise ConfigurationError("WEBAPP_CONFIG_JSON must be an object")
        config = deep_merge(config, loaded)
    for variable, path in ENV_MAP.items():
        if os.environ.get(variable, "") != "": set_path(config, path, os.environ[variable])
    if os.environ.get("WEBAPP_ALLOWED_ORIGINS", "") != "": config["app"]["allowed_origins"] = parse_list(os.environ["WEBAPP_ALLOWED_ORIGINS"])
    if os.environ.get("WEBAPP_LINUX_CATEGORIES", "") != "": config["linux"]["categories"] = parse_list(os.environ["WEBAPP_LINUX_CATEGORIES"])
    if os.environ.get("WEBAPP_LINUX_KEYWORDS", "") != "": config["linux"]["keywords"] = parse_list(os.environ["WEBAPP_LINUX_KEYWORDS"])
    if os.environ.get("WEBAPP_SHOW_APP_MENU", "") != "": config["window"]["show_app_menu"] = parse_bool(os.environ["WEBAPP_SHOW_APP_MENU"])
    if os.environ.get("WEBAPP_PERSISTENT", "") != "": config["app"]["persistent"] = parse_bool(os.environ["WEBAPP_PERSISTENT"])
    return normalize(config)


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text.rstrip() + "\n", encoding="utf-8", newline="\n")


def xml(value: Any) -> str: return xml_escape(str(value), {'"': '&quot;', "'": '&apos;'})
def cpp(value: Any) -> str: return json.dumps(str(value), ensure_ascii=False)
def cmake(value: Any) -> str: return str(value).replace("\\", "/").replace('"', '\\"').replace(";", "\\;")
def yamlq(value: Any) -> str: return "'" + str(value).replace("'", "''") + "'"
