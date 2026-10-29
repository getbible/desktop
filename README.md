# Website Desktop Application Template

A configurable C++ shell that turns an HTTPS website into a focused Linux, macOS, and Windows desktop application. It uses the operating system's native web engine through [CHOC](https://github.com/Tracktion/choc), preserves cookies and local storage, displays a branded offline page, disables the browser context menu, and prevents top-level navigation outside configured origins.

## Default GetBible profile

Without overrides, the repository builds:

```text
Name:             getBible.Life
Application URL:  https://app.getbible.life/
Homepage:         https://app.getbible.life/
Support URL:      https://app.getbible.life/
Allowed origin:   https://app.getbible.life
Application ID:   live.getbible.app
Executable:       getbible-life
Snap name:        getbible-life
```

The default floating three-dot menu is disabled so it cannot cover controls owned by the website.
`app.persistent` defaults to `true`, retaining website storage and persistent
cookies between launches according to the website's own login and expiry rules.

## Features

- Configurable name, domain, allowed origins, version, descriptions, support information, offline messages, window size, icons, and Store identities.
- JSON configuration with repository variable and secret overrides.
- Persistent native WebView cookies, local storage, and login state.
- One-command Ubuntu/GNOME installation with `build-linux.sh --install`.
- Branded embedded offline, About, and Support pages.
- Top-level navigation restricted to configured HTTPS origins.
- Right-click, middle-click, auxiliary buttons, and keyboard context menus disabled.
- JavaScript and user-agent application detection for website-specific layouts.
- Linux portable archive and Snap packaging.
- Windows MSIX packaging with optional certificate signing.
- macOS DMG packaging with optional Developer ID signing and notarization.
- Automatic builds on every push to `main`.
- Tag-driven GitHub Releases with all packages and SHA-256 checksums.
- Optional Snap Store publication after the GitHub Release succeeds.

## Repository layout

```text
config/app.json                 Default application configuration
config/app.schema.json          Configuration schema
branding/                       Default and custom source icons
src/                            C++ desktop shell
scripts/                        Local build and packaging commands
tools/                          Configuration and metadata generator
tests/                          Generator tests
docs/                           Integration and distribution guides
.github/workflows/build.yml     Main-branch multi-platform builds
.github/workflows/release.yml   Tag-driven release and Snap publication
```

Generated metadata is written under `generated/` and should not be edited manually.

## Linux quick start

```bash
sudo apt update
sudo apt install --yes \
    build-essential cmake git libgtk-3-dev libwebkit2gtk-4.1-dev \
    pkg-config python3 python3-pil

chmod +x scripts/*.sh
./scripts/build-linux.sh
./build/getbible-life
```

To build and install for the current desktop user:

```bash
bash scripts/build-linux.sh --install
```

This installs the binary in `~/.local/bin/images/` and registers its launcher
and icons in the standard XDG desktop directories. It does not require root
after the build dependencies are installed. To remove the installed application
while retaining its saved sessions and website data:

```bash
bash scripts/uninstall-linux.sh --local
```

The bootstrap script fetches the pinned upstream CHOC revision. Register it as a normal Git submodule in a local clone when desired:

```bash
./scripts/register-submodule.sh
git commit -m 'Register upstream CHOC submodule'
```

## macOS

Run on macOS with Xcode Command Line Tools, CMake, Git, Python 3, and Pillow:

```bash
chmod +x scripts/*.sh
./scripts/build-macos.sh
open ./build-macos/getbible-life.app
./scripts/package-macos.sh
```

See [macOS distribution](docs/MACOS_DISTRIBUTION.md).

## Windows

Run from PowerShell on Windows with Visual Studio 2022 C++ tools, CMake, Git, Python 3, Pillow, and the Windows SDK:

```powershell
python -m pip install --requirement tools/requirements.txt
./scripts/build-windows.ps1
./scripts/package-windows.ps1
```

See [Windows distribution](docs/WINDOWS_STORE.md).

## Configure another website

Copy the default configuration and supply another icon:

```bash
mkdir -p config/sites branding/sites
cp config/app.json config/sites/example.json
cp /path/to/example.png branding/sites/example.png
```

Minimal profile:

```json
{
  "app": {
    "name": "Example Portal",
    "slug": "example-portal",
    "id": "com.example.portal",
    "version": "1.0.0",
    "url": "https://portal.example.com/",
    "persistent": true,
    "homepage_url": "https://portal.example.com/",
    "allowed_origins": ["https://portal.example.com"]
  },
  "window": {
    "show_app_menu": false
  },
  "branding": {
    "icon": "branding/sites/example.png"
  },
  "snap": {
    "name": "example-portal"
  },
  "windows": {
    "identity_name": "com.example.portal",
    "publisher": "CN=Example Publisher",
    "publisher_display_name": "Example Publisher"
  }
}
```

Build it:

```bash
WEBAPP_CONFIG_FILE=config/sites/example.json ./scripts/build-linux.sh
```

See [configuration](docs/CONFIGURATION.md).

Profiles can also live entirely outside the repository. For example, a profile
at `~/my-apps/portal/config/app.json` can use `"icon": "../branding/icon.png"`:

```bash
bash scripts/build-linux.sh --config "$HOME/my-apps/portal/config/app.json" --install
bash scripts/uninstall-linux.sh --local --config "$HOME/my-apps/portal/config/app.json"
```

Relative icon paths are checked against the configuration file's directory
first, then the repository root for compatibility with existing profiles.
`WEBAPP_CONFIG_FILE` and existing environment overrides continue to work.
Each website should have a distinct `app.id` and `app.slug` so its storage,
installed binary and desktop launcher remain independent.

## Application detection

Every page receives:

```javascript
window.WebAppDesktop
```

The default profile also exposes:

```javascript
window.GetBibleApp
```

Example:

```javascript
if (window.WebAppDesktop?.isApp) {
    document.querySelector('.browser-only')?.remove();
}
```

The document root receives `webapp-desktop-app` and a configurable application-specific class. Linux also appends `GetBibleLifeDesktop/<version>` to WebKitGTK's normal user agent. These markers control presentation only; they are not authentication credentials.

See [website integration](docs/APP_INTEGRATION.md).

## About and Support

The embedded overlay menu is disabled by default. A website can add controls where they fit its own layout:

```javascript
document.querySelector('#desktop-about')?.addEventListener('click', () => {
    window.location.assign(window.WebAppDesktop.aboutURL);
});

document.querySelector('#desktop-support')?.addEventListener('click', () => {
    window.location.assign(window.WebAppDesktop.supportURL);
});
```

The internal destinations are `webapp://app/about` and `webapp://app/support`.

## Automatic builds

Every push to `main` runs **Build all desktop targets** and produces downloadable workflow artifacts for:

- Linux `.tar.gz`;
- Linux `.snap`;
- Windows `.msix`;
- macOS `.dmg`.

Repository configuration and signing values are optional. The default GetBible profile is used when none are supplied.

## Automatic releases

After the main-branch checks pass, push a semantic version tag:

```bash
git checkout main
git pull --ff-only
git tag -s v1.0.0 -m 'getBible.Life 1.0.0'
git push origin v1.0.0
```

**Build and publish release** then:

1. builds all four packages;
2. creates `SHA256SUMS`;
3. creates the GitHub Release and uploads all packages;
4. attempts Snap Store publication only afterward.

When `SNAPCRAFT_STORE_CREDENTIALS` is absent, the final Store job fails clearly, but the GitHub Release and downloadable packages remain available.

See [automated releases](docs/AUTOMATED_RELEASES.md) and [Snap Store distribution](docs/SNAP_STORE.md).

## Validation

```bash
chmod +x scripts/*.sh
./scripts/validate.sh
```

## Distribution channels

- Linux: GitHub Release archive or Snap Store.
- Windows: signed MSIX and Microsoft Store.
- macOS: signed/notarized DMG or a separately prepared Mac App Store submission.

The Snap Store distributes Linux packages only.

## License

The template is GPL-3.0-or-later. CHOC is distributed separately under its ISC license.
