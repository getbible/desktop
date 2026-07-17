# Automated builds and releases

The repository has two production workflows:

- `.github/workflows/build.yml` builds every desktop target on each push to `main` and on manual dispatch.
- `.github/workflows/release.yml` builds release packages from `v*` tags or manual dispatch, creates a GitHub Release, and then optionally publishes the Snap.

No feature branch or pull request is required. The checked-in default profile builds getBible.Life. A fork can target another website through repository variables and secrets without editing the C++ source.

## Main-branch builds

Every push to `main` runs validation and produces workflow artifacts for:

- Linux portable `.tar.gz`;
- Linux `.snap`;
- Windows `.msix`;
- macOS `.dmg`.

Signing credentials are optional for ordinary builds. Without them, Windows and macOS packages are generated unsigned for technical testing. The Snap is built but not published.

## Stable release flow

Create and push a version tag after the `main` build has passed:

```bash
git checkout main
git pull --ff-only
git tag -s v1.0.0 -m 'getBible.Life 1.0.0'
git push origin v1.0.0
```

The release workflow performs these steps:

1. Resolves the release version from the tag.
2. Builds Linux, Snap, Windows, and macOS packages in parallel.
3. Fails before release publication if any required build or validation fails.
4. Downloads all successful packages into one release job.
5. Generates `SHA256SUMS`.
6. Creates the GitHub Release and uploads every package.
7. Attempts Snap Store publication only after the GitHub Release exists.

If `SNAPCRAFT_STORE_CREDENTIALS` is missing or invalid, the final Snap publication job fails. The already-created GitHub Release and its downloadable packages remain available, which intentionally separates package availability from Store authentication.

Tag channel mapping:

- `1.0.0-alpha.1` and `1.0.0-dev.1` → `edge`
- `1.0.0-beta.1` → `beta`
- `1.0.0-rc.1` → `candidate`
- `1.0.0` → `stable`

The workflow can also be started manually with an explicit version, Snap channel, and publication toggle.

## Repository secrets

Configure these in **Settings → Secrets and variables → Actions → Secrets**.

### Website configuration

`WEBAPP_CONFIG_JSON` is optional. It may contain a complete or partial JSON configuration. It is merged over `config/app.json`.

Example:

```json
{
  "app": {
    "name": "Example Portal",
    "slug": "example-portal",
    "id": "com.example.portal",
    "url": "https://portal.example.com/",
    "homepage_url": "https://portal.example.com/",
    "allowed_origins": ["https://portal.example.com"]
  },
  "snap": {
    "name": "example-portal"
  }
}
```

`WEBAPP_ICON_BASE64` is optional. Store the raw base64 representation of a PNG icon, without a data-URI prefix:

```bash
base64 -w 0 branding/example.png
```

On macOS:

```bash
base64 < branding/example.png | tr -d '\n'
```

### Snap Store

`SNAPCRAFT_STORE_CREDENTIALS` enables automatic publication. Generate a restricted credential locally after logging in to Snapcraft:

```bash
snapcraft login
snapcraft export-login \
  --snaps getbible-life \
  --channels edge,beta,candidate,stable \
  --acls package_access,package_push,package_release \
  snapcraft-store-credentials.txt
```

Store the complete file contents as the secret:

```bash
gh secret set SNAPCRAFT_STORE_CREDENTIALS < snapcraft-store-credentials.txt
```

Delete the local credential file securely after use.

### Windows signing

- `WINDOWS_PFX_BASE64`: base64-encoded code-signing `.pfx` file.
- `WINDOWS_PFX_PASSWORD`: certificate password.

The certificate subject must exactly match `windows.publisher` in the resolved application configuration.

### Apple signing and notarization

- `APPLE_CERTIFICATE_BASE64`: base64-encoded Developer ID Application `.p12` file.
- `APPLE_CERTIFICATE_PASSWORD`: certificate export password.
- `APPLE_SIGNING_IDENTITY`: full identity, such as `Developer ID Application: Publisher (TEAMID)`.
- `APPLE_ID`: Apple developer account email.
- `APPLE_TEAM_ID`: Apple Developer team ID.
- `APPLE_APP_SPECIFIC_PASSWORD`: app-specific password for notarization.

When all Apple values are available, the workflow signs, notarizes, staples, and validates the DMG. Otherwise it still creates an unsigned test DMG.

## Repository variables

Configure non-sensitive overrides under **Settings → Secrets and variables → Actions → Variables**. Supported values include:

```text
WEBAPP_NAME
WEBAPP_SLUG
WEBAPP_ID
WEBAPP_VERSION
WEBAPP_URL
WEBAPP_HOMEPAGE_URL
WEBAPP_SUMMARY
WEBAPP_DESCRIPTION
WEBAPP_ABOUT
WEBAPP_ALLOWED_ORIGINS
WEBAPP_SHOW_APP_MENU
WEBAPP_SUPPORT_URL
WEBAPP_SUPPORT_EMAIL
WEBAPP_SUPPORT_MESSAGE
WEBAPP_OFFLINE_TITLE
WEBAPP_OFFLINE_MESSAGE
WEBAPP_SNAP_NAME
WEBAPP_WINDOWS_IDENTITY_NAME
WEBAPP_WINDOWS_PUBLISHER
WEBAPP_WINDOWS_PUBLISHER_DISPLAY_NAME
WEBAPP_WINDOWS_ARCHITECTURE
WEBAPP_MACOS_MINIMUM_SYSTEM_VERSION
WEBAPP_MACOS_CATEGORY
WEBAPP_MACOS_COPYRIGHT
```

The release tag always overrides `WEBAPP_VERSION` for a release build, ensuring package metadata matches the Git tag.

## Forking for another website

A fork can remain code-identical to this repository:

1. Reserve the target Snap name and Store identities.
2. Set `WEBAPP_CONFIG_JSON` and `WEBAPP_ICON_BASE64` in the fork.
3. Add optional signing and Store credentials.
4. Push to `main` and inspect the four build artifacts.
5. Push a version tag after all checks pass.

The fallback GetBible configuration is used whenever no repository overrides are supplied.
