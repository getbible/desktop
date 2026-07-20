#!/usr/bin/env python3
from __future__ import annotations

from config_core import *  # noqa: F401,F403

def source_icon(root: Path, config: dict[str, Any]) -> Image.Image:
    encoded = os.environ.get("WEBAPP_ICON_BASE64", "").strip()
    if encoded:
        return Image.open(io.BytesIO(base64.b64decode(encoded, validate=True))).convert("RGBA")
    path = Path(config["branding"]["icon"])
    if not path.is_absolute(): path = root / path
    if path.suffix.lower() in {".base64", ".b64"}:
        data = base64.b64decode("".join(path.read_text(encoding="utf-8").split()), validate=True)
        return Image.open(io.BytesIO(data)).convert("RGBA")
    return Image.open(path).convert("RGBA")


def square(image: Image.Image, size: int, background: str) -> Image.Image:
    canvas = Image.new("RGBA", (size, size), background)
    sample = image.copy(); sample.thumbnail((size, size), Image.Resampling.LANCZOS)
    canvas.alpha_composite(sample, ((size - sample.width) // 2, (size - sample.height) // 2))
    return canvas


def generate_icons(root: Path, out: Path, config: dict[str, Any]) -> None:
    image = source_icon(root, config)
    assets = out / "assets"; assets.mkdir(parents=True, exist_ok=True)
    produced: dict[int, Image.Image] = {}
    for size in (16, 32, 44, 48, 50, 64, 128, 150, 256, 310, 512, 1024):
        produced[size] = square(image, size, config["branding"]["background_color"])
        produced[size].save(assets / f"icon-{size}.png", "PNG")
    produced[512].save(assets / "icon.png", "PNG")
    produced[256].save(assets / "icon.ico", format="ICO", sizes=[(16,16),(32,32),(48,48),(64,64),(128,128),(256,256)])
    produced[1024].save(assets / "icon.icns", format="ICNS")
    win = out / "windows" / "Assets"; win.mkdir(parents=True, exist_ok=True)
    for filename, size in {"Square44x44Logo.png":44,"Square150x150Logo.png":150,"Square310x310Logo.png":310,"StoreLogo.png":50}.items(): produced[size].save(win / filename)
    wide = Image.new("RGBA", (310,150), config["branding"]["background_color"]); icon = square(image,130,config["branding"]["background_color"]); wide.alpha_composite(icon,(90,10)); wide.save(win / "Wide310x150Logo.png")
    raw = (assets / "icon-256.png").read_bytes()
    lines = [", ".join(f"0x{byte:02x}" for byte in raw[i:i+12]) for i in range(0,len(raw),12)]
    write(out / "app_icon.hpp", "#pragma once\n#include <cstddef>\nnamespace webapp::generated {\ninline constexpr unsigned char appIconPng[] = {\n    " + ",\n    ".join(lines) + "\n};\ninline constexpr std::size_t appIconPngSize = sizeof(appIconPng);\n}")


def generate(root: Path, out: Path, config: dict[str, Any]) -> None:
    app, window, offline, support, branding = config["app"], config["window"], config["offline"], config["support"], config["branding"]
    support_allowed = bool(support["url"] and origin(f"https://{urlparse(support['url']).netloc}", "support.url") in app["allowed_origins"])
    identity = {"isApp":True,"name":app["name"],"client":"desktop","version":app["version"],"url":app["url"],"aboutURL":"webapp://app/about","supportURL":"webapp://app/support"}
    constants = {
        "name":app["name"],"slug":app["slug"],"version":app["version"],"appURL":app["url"],"aboutText":app["about"],
        "aboutURL":"webapp://app/about","shellURL":"webapp://app","offlineURL":"webapp://app/offline",
        "supportPageURL":"webapp://app/support","supportLabel":support["label"],"supportURL":support["url"],"supportEmail":support["email"],
        "supportMessage":support["message"],"offlineTitle":offline["title"],"offlineMessage":offline["message"],"offlineRetryLabel":offline["retry_label"],
        "offlineCheckingMessage":offline["checking_message"],"offlineConnectedMessage":offline["connected_message"],"offlineFailedMessage":offline["failed_message"],
        "javascriptObject":app["javascript_object"],"htmlClass":app["html_class"],"userAgentProduct":app["user_agent_product"],
        "accentColor":branding["accent_color"],"backgroundColor":branding["background_color"],"allowedOriginsJSON":json.dumps(app["allowed_origins"]),
        "identityJSON":json.dumps(identity,ensure_ascii=False,separators=(",",":")),
    }
    header = ["#pragma once","#include <array>","#include <string_view>","namespace webapp::config {"]
    header += [f"inline constexpr std::string_view {key} = {cpp(value)};" for key,value in constants.items()]
    header += [f"inline constexpr bool showAppMenu = {'true' if window['show_app_menu'] else 'false'};",f"inline constexpr bool supportURLAllowed = {'true' if support_allowed else 'false'};"]
    header += [f"inline constexpr int windowWidth = {window['width']};",f"inline constexpr int windowHeight = {window['height']};",f"inline constexpr int minimumWindowWidth = {window['minimum_width']};",f"inline constexpr int minimumWindowHeight = {window['minimum_height']};"]
    header += [f"inline constexpr std::array<std::string_view, {len(app['allowed_origins'])}> allowedOrigins = {{"] + [f"    {cpp(item)}," for item in app["allowed_origins"]] + ["};","}"]
    write(out / "app_config.hpp", "\n".join(header))
    write(out / "app_config.cmake", "\n".join([f'set(WEBAPP_NAME "{cmake(app["name"])}")',f'set(WEBAPP_SLUG "{cmake(app["slug"])}")',f'set(WEBAPP_ID "{cmake(app["id"])}")',f'set(WEBAPP_VERSION "{cmake(app["version"])}")',f'set(WEBAPP_DESCRIPTION "{cmake(app["description"])}")',f'set(WEBAPP_HOMEPAGE_URL "{cmake(app["homepage_url"])}")',f'set(WEBAPP_EXECUTABLE "{cmake(app["slug"])}")',f'set(WEBAPP_SNAP_NAME "{cmake(config["snap"]["name"])}")',f'set(WEBAPP_MACOS_MINIMUM_SYSTEM_VERSION "{cmake(config["macos"]["minimum_system_version"])}")']))
    write(out / "app.env", "\n".join(f"{key}={shlex.quote(str(value))}" for key,value in {"WEBAPP_NAME":app["name"],"WEBAPP_SLUG":app["slug"],"WEBAPP_EXECUTABLE":app["slug"],"WEBAPP_VERSION":app["version"],"WEBAPP_URL":app["url"],"WEBAPP_ID":app["id"],"WEBAPP_SNAP_NAME":config["snap"]["name"],"WEBAPP_WINDOWS_ARCHITECTURE":config["windows"]["architecture"]}.items()))
    generate_icons(root,out,config)
    version4 = ".".join((re.findall(r"\d+",app["version"])+["0"]*4)[:4])
    write(out / "Info.plist", f'''<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "https://www.apple.com/DTDs/PropertyList-1.0.dtd">\n<plist version="1.0"><dict>\n<key>CFBundleDisplayName</key><string>{xml(app['name'])}</string><key>CFBundleExecutable</key><string>{xml(app['slug'])}</string><key>CFBundleIdentifier</key><string>{xml(app['id'])}</string><key>CFBundleIconFile</key><string>icon.icns</string><key>CFBundleName</key><string>{xml(app['name'])}</string><key>CFBundlePackageType</key><string>APPL</string><key>CFBundleShortVersionString</key><string>{xml(app['version'])}</string><key>CFBundleVersion</key><string>{xml(app['version'])}</string><key>LSApplicationCategoryType</key><string>{xml(config['macos']['category'])}</string><key>LSMinimumSystemVersion</key><string>{xml(config['macos']['minimum_system_version'])}</string><key>NSHighResolutionCapable</key><true/><key>NSHumanReadableCopyright</key><string>{xml(config['macos']['copyright'])}</string>\n</dict></plist>''')
    write(out / "macos/entitlements.plist", '<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "https://www.apple.com/DTDs/PropertyList-1.0.dtd">\n<plist version="1.0"><dict><key>com.apple.security.app-sandbox</key><true/><key>com.apple.security.network.client</key><true/></dict></plist>')
    nums = version4.replace(".",",")
    write(out / "app.rc.in", f'''#include <windows.h>\nIDI_WEBAPP ICON "@WEBAPP_WINDOWS_ICON@"\nVS_VERSION_INFO VERSIONINFO\n FILEVERSION {nums}\n PRODUCTVERSION {nums}\nBEGIN\n BLOCK "StringFileInfo" BEGIN BLOCK "040904b0" BEGIN\n VALUE "CompanyName", "{config['windows']['publisher_display_name']}"\n VALUE "FileDescription", "{app['summary']}"\n VALUE "FileVersion", "{app['version']}"\n VALUE "InternalName", "{app['slug']}"\n VALUE "OriginalFilename", "{app['slug']}.exe"\n VALUE "ProductName", "{app['name']}"\n VALUE "ProductVersion", "{app['version']}"\n END END\n BLOCK "VarFileInfo" BEGIN VALUE "Translation", 0x409, 1200 END\nEND''')
    write(out / "windows/AppxManifest.xml", f'''<?xml version="1.0" encoding="utf-8"?>\n<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10" xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10" xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities" IgnorableNamespaces="uap rescap"><Identity Name="{xml(config['windows']['identity_name'])}" Publisher="{xml(config['windows']['publisher'])}" Version="{version4}" ProcessorArchitecture="{xml(config['windows']['architecture'])}"/><Properties><DisplayName>{xml(app['name'])}</DisplayName><PublisherDisplayName>{xml(config['windows']['publisher_display_name'])}</PublisherDisplayName><Description>{xml(app['summary'])}</Description><Logo>Assets\\StoreLogo.png</Logo></Properties><Resources><Resource Language="en-us"/></Resources><Dependencies><TargetDeviceFamily Name="Windows.Desktop" MinVersion="{xml(config['windows']['minimum_version'])}" MaxVersionTested="10.0.26100.0"/></Dependencies><Applications><Application Id="App" Executable="{xml(app['slug'])}.exe" EntryPoint="Windows.FullTrustApplication"><uap:VisualElements DisplayName="{xml(app['name'])}" Description="{xml(app['summary'])}" BackgroundColor="transparent" Square150x150Logo="Assets\\Square150x150Logo.png" Square44x44Logo="Assets\\Square44x44Logo.png"><uap:DefaultTile Wide310x150Logo="Assets\\Wide310x150Logo.png" Square310x310Logo="Assets\\Square310x310Logo.png"/></uap:VisualElements></Application></Applications><Capabilities><Capability Name="internetClient"/><rescap:Capability Name="runFullTrust"/></Capabilities></Package>''')
    categories = ";".join(config["linux"]["categories"])+";"; keywords = ";".join(config["linux"]["keywords"])+";"
    desktop = f'''[Desktop Entry]\nVersion=1.0\nType=Application\nName={app['name']}\nGenericName={config['linux']['generic_name']}\nComment={app['summary']}\nExec={app['slug']}\nIcon={app['id']}\nTerminal=false\nCategories={categories}\nKeywords={keywords}\nStartupNotify=true\nStartupWMClass={app['slug']}'''
    write(out / f"packaging/linux/{app['id']}.desktop", desktop)
    category_xml = "".join(f"<category>{xml(item)}</category>" for item in config["linux"]["categories"])
    write(out / f"packaging/linux/{app['id']}.metainfo.xml", f'''<?xml version="1.0" encoding="UTF-8"?>\n<component type="desktop-application"><id>{xml(app['id'])}</id><name>{xml(app['name'])}</name><summary>{xml(app['summary'])}</summary><metadata_license>CC0-1.0</metadata_license><project_license>{xml(config['snap']['license'])}</project_license><description><p>{xml(app['description'])}</p></description><launchable type="desktop-id">{xml(app['id'])}.desktop</launchable><url type="homepage">{xml(app['homepage_url'])}</url><url type="help">{xml(support['url'] or app['homepage_url'])}</url><provides><binary>{xml(app['slug'])}</binary></provides><categories>{category_xml}</categories></component>''')
    snap = config["snap"]
    snap_yaml = f'''name: {snap['name']}\nversion: {yamlq(app['version'])}\nsummary: {yamlq(app['summary'])}\ndescription: |\n{textwrap.indent(textwrap.fill(app['description'],78), '  ')}\nwebsite: {app['homepage_url']}\ncontact: {support['url'] or app['homepage_url']}\nlicense: {snap['license']}\nbase: {snap['base']}\ngrade: {snap['grade']}\nconfinement: {snap['confinement']}\nparts:\n  application:\n    plugin: cmake\n    source: .\n    build-packages: [g++, libgtk-3-dev, libwebkit2gtk-4.1-dev, pkg-config]\n    stage-packages: [libwebkit2gtk-4.1-0]\n    cmake-parameters: [-DCMAKE_BUILD_TYPE=Release, -DCMAKE_INSTALL_PREFIX=/usr, -DWEBAPP_USE_GENERATED=ON]\napps:\n  {snap['name']}:\n    command: usr/bin/{app['slug']}\n    extensions: [gnome]\n    plugs: [audio-playback, browser-support, desktop, desktop-legacy, home, network, network-status, opengl, screen-inhibit-control, wayland, x11]\n    environment:\n      TMPDIR: $XDG_RUNTIME_DIR\n      XDG_CACHE_HOME: $SNAP_USER_COMMON/.cache\n      XDG_CONFIG_HOME: $SNAP_USER_COMMON/.config\n      XDG_DATA_HOME: $SNAP_USER_COMMON/.local/share'''
    write(root / "snap/snapcraft.yaml", snap_yaml)
    write(root / f"snap/gui/{snap['name']}.desktop", desktop.replace(f"Icon={app['id']}",f"Icon=${{SNAP}}/meta/gui/{snap['name']}.png"))
    (root / "snap/gui").mkdir(parents=True,exist_ok=True)
    __import__('shutil').copy2(out / "assets/icon-256.png", root / f"snap/gui/{snap['name']}.png")
    write(out / "config.resolved.json", json.dumps(config,ensure_ascii=False,indent=2))
