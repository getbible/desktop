# Website Desktop Application Template

A configurable C++ shell that turns an HTTPS website into a focused Linux, macOS, and Windows desktop application. It uses the operating system's native web engine through [CHOC](https://github.com/Tracktion/choc), preserves cookies and local storage, shows a branded offline page, disables the browser context menu, and prevents top-level navigation outside configured origins.

The checked-in fallback profile is:

- Name: `getBible.Life`
- Application URL, homepage, support URL, and allowed origin: `https://app.getbible.life/`
- Application ID: `live.getbible.app`
- Executable and Snap name: `getbible-life`
- Icon: `branding/getbible-life.png`

Missing values inherit these defaults.

## What is configurable

`config/app.json` controls:

- Website URL, homepage, allowed origins, application identity, and version
- Name, summary, description, About text, and support information
- Offline messages and retry text
- Window dimensions
- Optional injected application menu
- User-agent and JavaScript application markers
- Linux desktop and Snap metadata
- Windows MSIX identity, publisher, architecture, and minimum Windows version
- macOS bundle category, minimum version, and copyright
- Source icon and branding colours

The generator writes C++ headers, Linux metadata, Snapcraft files, Windows resources and MSIX manifest/assets, macOS `Info.plist` and entitlements, plus PNG/ICO/ICNS icons.

## Linux quick start

```bash
sudo apt update
sudo apt install --yes \
    build-essential cmake git libgtk-3-dev libwebkit2gtk-4.1-dev \
    pkg-config python3 python3-pil

./scripts/bootstrap.sh
./scripts/build-linux.sh
./build/getbible-life
```

## macOS build

Run on macOS with Xcode Command Line Tools, CMake, Git, Python 3, and Pillow installed:

```bash
./scripts/build-macos.sh
open ./build-macos/getbible-life.app
```

Create a DMG:

```bash
./scripts/package-macos.sh
```

Signing and notarization instructions are in [docs/MACOS_DISTRIBUTION.md](docs/MACOS_DISTRIBUTION.md).

## Windows build

Run from PowerShell on Windows with Visual Studio 2022 C++ tools, CMake, Git, Python 3, Pillow, and the Windows SDK installed:

```powershell
./scripts/build-windows.ps1
./build-windows/Release/getbible-life.exe
```

Create an MSIX:

```powershell
./scripts/package-windows.ps1
```

Signing and Microsoft Store instructions are in [docs/WINDOWS_STORE.md](docs/WINDOWS_STORE.md).

## Configure another website

```bash
mkdir -p config/sites branding/sites
cp config/app.json config/sites/my-site.json
cp branding/getbible-life.png branding/sites/my-site.png
```

Set at least:

```json
{
  "app": {
    "name": "My Website",
    "slug": "my-website",
    "id": "com.example.mywebsite",
    "version": "1.0.0",
    "url": "https://app.example.com/",
    "homepage_url": "https://app.example.com/",
    "allowed_origins": ["https://app.example.com"]
  },
  "window": {
    "show_app_menu": false
  },
  "branding": {
    "icon": "branding/sites/my-site.png"
  },
  "snap": {
    "name": "my-website"
  },
  "windows": {
    "identity_name": "com.example.mywebsite",
    "publisher": "CN=Example Publisher",
    "publisher_display_name": "Example Publisher"
  }
}
```

Build that profile on Linux:

```bash
WEBAPP_CONFIG_FILE=config/sites/my-site.json ./scripts/build-linux.sh
```

The macOS and Windows scripts accept the same `WEBAPP_CONFIG_FILE` environment variable.

## Application menu

The injected three-dot application menu is now **disabled by default** because a floating control can overlap controls owned by the website. About and Support pages remain available internally at:

```text
webapp://app/about
webapp://app/support
```

The website can open them through the injected identity object:

```javascript
document.querySelector('#about-app')?.addEventListener('click', () => {
    window.location.assign(window.WebAppDesktop.aboutURL);
});

document.querySelector('#support-app')?.addEventListener('click', () => {
    window.location.assign(window.WebAppDesktop.supportURL);
});
```

Set `window.show_app_menu` to `true` only for websites that have reserved clear space for the floating menu. CHOC does not currently expose one portable native title-bar/menu API across Linux, macOS, and Windows, so the template does not pretend the injected menu is native.

## Runtime behaviour

Only origins listed in `app.allowed_origins` may load as top-level pages. External attempts are cancelled and return to `app.url`. Right-click, middle-click, auxiliary mouse buttons, the keyboard context-menu key, and `Shift+F10` do nothing. Normal left-click interaction remains available.

The shell starts on an embedded offline page, probes the configured URL, and loads the site when connectivity returns. Native cookies, local storage, and login state persist across restarts.

## Website detection

Every page receives:

```javascript
window.WebAppDesktop
```

and the configured alias, which is `window.GetBibleApp` in the default profile. The object includes `name`, `slug`, `version`, `platform`, `aboutURL`, and `supportURL`.

Linux also appends `GetBibleLifeDesktop/<version>` to WebKitGTK's normal user agent. These markers are presentation hints, not authentication credentials.

See [docs/APP_INTEGRATION.md](docs/APP_INTEGRATION.md).

## Distribution channels

The Snap Store distributes Linux Snaps only. It does not install applications on Windows or macOS.

- Linux: Snap Store — [docs/SNAP_STORE.md](docs/SNAP_STORE.md)
- Windows: signed MSIX and Microsoft Store — [docs/WINDOWS_STORE.md](docs/WINDOWS_STORE.md)
- macOS: signed/notarized DMG or Mac App Store — [docs/MACOS_DISTRIBUTION.md](docs/MACOS_DISTRIBUTION.md)

The repository includes environment-selected manual workflows for all three package types, plus a tag-driven production workflow:

```text
Build configured Snap
Build configured Windows MSIX
Build configured macOS DMG
Build and publish release
```

The production workflow always creates Linux, Snap, Windows, and macOS packages and attaches them to a GitHub Release. Repository secrets enable optional Windows signing, Apple signing/notarization, and automatic Snap Store publication. A fork can target another website by setting `WEBAPP_CONFIG_JSON`, `WEBAPP_ICON_BASE64`, and `WEBAPP_*` repository values without editing the C++ source.

See [docs/AUTOMATED_RELEASES.md](docs/AUTOMATED_RELEASES.md) for the complete release and secret setup.

## Automated stable release

Push a version tag after the normal checks pass:

```bash
git tag -s v1.0.0 -m 'getBible.Life 1.0.0'
git push origin v1.0.0
```

The workflow creates downloadable packages for every target and then attempts to publish the Snap to `stable`. If Snap credentials are missing, only the Snap Store publication job fails; the GitHub Release and all generated packages remain available.

## Validation

```bash
./scripts/validate.sh
```

GitHub Actions also performs Linux, Windows, and macOS compilation.

## License

The template is GPL-3.0-or-later. CHOC is distributed separately under its ISC license.
