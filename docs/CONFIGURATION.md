# Application configuration

The template resolves application settings in this order, with later sources overriding earlier sources:

1. Built-in GetBible defaults.
2. `config/app.json` or the file selected by `WEBAPP_CONFIG_FILE`.
3. The optional `WEBAPP_CONFIG_JSON` secret.
4. Individual `WEBAPP_*` environment variables.

Run the generator directly:

```bash
python3 tools/configure_app.py \
  --root . \
  --config config/app.json \
  --output generated
```

Or use:

```bash
./scripts/configure-app.sh
```

## Core application fields

```json
{
  "app": {
    "name": "getBible.Life",
    "slug": "getbible-life",
    "id": "live.getbible.app",
    "persistent": true,
    "version": "1.0.0",
    "url": "https://app.getbible.life/",
    "homepage_url": "https://app.getbible.life/",
    "allowed_origins": ["https://app.getbible.life"],
    "summary": "Short Store summary",
    "description": "Long Store description",
    "about": "About-page text",
    "user_agent_product": "GetBibleLifeDesktop",
    "javascript_object": "GetBibleApp",
    "html_class": "getbible-life-desktop-app"
  }
}
```

`app.url`, `app.homepage_url`, and every allowed origin must use HTTPS. The origin of `app.url` is automatically added to the allowed list.

## Persistent website data

`app.persistent` is a boolean and defaults to `true`, including when older profiles omit it. With persistence enabled, the desktop app reuses its website data when reopened: persistent cookies, local storage, IndexedDB, and cache. Give separate applications distinct `app.id` and `app.slug` values and keep them stable between builds; these identities distinguish the native profiles and installed applications.

```json
{
  "app": {
    "id": "com.example.portal",
    "persistent": true
  }
}
```

Use `false` for a fresh profile on each launch. On Linux and macOS this uses the web engine's non-persistent data store; Windows uses a separate temporary WebView2 profile for each launch. Disabling persistence does not erase an existing persistent profile, so reenabling it reopens that profile.

`WEBAPP_PERSISTENT=true` or `WEBAPP_PERSISTENT=false` overrides the configuration at build time. The resulting choice is embedded in the binary; no launcher environment variables are required.

Persistent profile locations:

| Platform | Website data and cookies | Cache |
| --- | --- | --- |
| Linux custom profiles | `$XDG_DATA_HOME/<app.id>` (default `~/.local/share/<app.id>`); cookies in `cookies.sqlite` | `$XDG_CACHE_HOME/<app.id>` (default `~/.cache/<app.id>`) |
| Linux default GetBible profile | Existing WebKit data location; cookies added under `$XDG_DATA_HOME/live.getbible.app/cookies.sqlite` | Existing WebKit cache location |
| Windows | Existing `%APPDATA%/<executable filename>` directory, including `.exe` | Managed within that WebView2 profile |
| macOS | WKWebView's default data store for the application's bundle identifier (`app.id`) | Managed by WKWebView |

The original GetBible Linux storage location and the existing Windows profile location are retained so enabling persistence does not discard previously stored website data. Windows temporary profiles are removed after the browser process exits normally; interrupted shutdown can leave temporary files, but they are never reopened on a later launch.

Persistence respects website authentication rules: cookie expiry, server-side logout, session-only cookies, and password-vault locking still apply. Select a website's **Remember me** option when provided. This setting does not turn an online website into an offline application.

## Window and embedded menu

```json
{
  "window": {
    "width": 1200,
    "height": 800,
    "minimum_width": 720,
    "minimum_height": 520,
    "show_app_menu": false
  }
}
```

The floating three-dot menu is disabled by default because it can cover website controls. Enable it only when the website intentionally reserves space for it. About and Support pages remain available at `webapp://app/about` and `webapp://app/support` even when the menu is disabled.

## Offline and support content

The `offline` object controls the embedded reconnect page. The `support` object controls the generated support page and Store contact metadata.

A support URL opens in the application only when its origin is included in `app.allowed_origins`; otherwise it is displayed as information without allowing an external website to replace the application.

## Branding

`branding.icon` may point to a PNG or to a text file containing raw base64 PNG data. `WEBAPP_ICON_BASE64` takes priority in CI.

Relative icon paths are resolved against the selected configuration file's directory first. If that path does not exist, the generator falls back to the repository root for compatibility with existing profiles such as `branding/getbible-life.png.base64`. Absolute paths are used directly. `WEBAPP_ICON` follows the same path resolution rules.

For a profile outside the checkout, keep its configuration and icon together:

```text
my-profile/
  app.json
  icon.png
```

Set `branding.icon` to `icon.png` in that configuration. The configuration and logo can then be moved together without changing either file.

The generator produces:

- Linux PNG sizes;
- Windows ICO and MSIX tile images;
- macOS ICNS;
- an embedded PNG header used by offline, About, and Support pages.

## Platform metadata

The `snap`, `windows`, and `macos` objects control Store identity and package settings. Store identity values should be chosen before the first public publication because changing them later may create a different application identity.

Windows `publisher` must equal the subject of the signing certificate. The application ID and Snap name must satisfy the naming requirements of their target Stores.

## Using another configuration file

```bash
WEBAPP_CONFIG_FILE=config/sites/customer.json ./scripts/build-linux.sh
```

Windows PowerShell:

```powershell
./scripts/build-windows.ps1 -ConfigFile config/sites/customer.json
```

macOS:

```bash
WEBAPP_CONFIG_FILE=config/sites/customer.json ./scripts/package-macos.sh
```

## Generated files

Do not edit files under `generated/` manually. They are regenerated from configuration and branding before builds. The generator also refreshes `snap/snapcraft.yaml` and `snap/gui/` so the Snap metadata always matches the selected profile.
