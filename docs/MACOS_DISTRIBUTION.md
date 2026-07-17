# macOS build and distribution

macOS packages must be built on macOS with Xcode Command Line Tools, CMake, Git, Python 3, and Pillow installed.

## Build and package

```bash
python3 -m pip install --requirement tools/requirements.txt
./scripts/build-macos.sh
open ./build-macos/getbible-life.app
./scripts/package-macos.sh
```

The DMG is written under `dist/macos/`.

## Developer ID signing

Export a Developer ID Application certificate and configure:

```bash
export APPLE_SIGNING_IDENTITY='Developer ID Application: Publisher Name (TEAMID)'
./scripts/package-macos.sh
```

The build uses the generated hardened-runtime entitlements and verifies the signed application before creating the DMG.

## Notarization

Create an app-specific Apple password and set:

```bash
export APPLE_SIGNING_IDENTITY='Developer ID Application: Publisher Name (TEAMID)'
export APPLE_ID='developer@example.com'
export APPLE_TEAM_ID='TEAMID'
export APPLE_APP_SPECIFIC_PASSWORD='app-specific-password'
export APPLE_NOTARIZE=1
./scripts/package-macos.sh
```

The script submits the DMG with `notarytool`, waits for the result, staples the ticket, and validates the staple.

## GitHub Actions

Configure repository secrets:

- `APPLE_CERTIFICATE_BASE64` — base64-encoded `.p12` certificate;
- `APPLE_CERTIFICATE_PASSWORD` — export password;
- `APPLE_SIGNING_IDENTITY` — full Developer ID identity;
- `APPLE_ID` — Apple developer account email;
- `APPLE_TEAM_ID` — developer team ID;
- `APPLE_APP_SPECIFIC_PASSWORD` — notarization password.

Encode the certificate:

```bash
base64 < signing.p12 | tr -d '\n'
```

With all secrets present, the workflow signs and notarizes. Without them, it still creates an unsigned DMG for build testing and GitHub artifact inspection.

## Distribution

A signed and notarized DMG may be distributed from the GitHub Release or another download site. Mac App Store publication requires additional App Store identities, provisioning, sandbox review, and App Store Connect configuration and is not performed automatically by this template.

A fork must configure its own bundle identifier, publisher certificate, and Apple developer credentials.
