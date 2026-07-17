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
