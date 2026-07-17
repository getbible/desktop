# Changelog

All notable changes to this project are documented here.

## 2.2.0 — 2026-07-18

- Added automatic Linux, Snap, Windows, and macOS builds on every push to `main`.
- Added tag-driven GitHub Releases containing all four packages and SHA-256 checksums.
- Added optional Snap Store publication after the GitHub Release succeeds.
- Added repository-variable and repository-secret configuration for reusable forks.
- Kept GetBible as the default profile targeting `https://app.getbible.life/` with application ID `live.getbible.app`.
- Disabled the floating application menu by default.
- Added Windows MSIX and macOS DMG packaging, optional signing, and optional Apple notarization.
- Added configurable application identity, website metadata, offline content, support details, icons, and allowed origins.

## 2.1.0 — 2026-07-18

- Corrected all default GetBible URLs to `https://app.getbible.life/`.
- Added Windows and macOS build and packaging support.
- Disabled the web-overlay application menu by default.

## 2.0.0 — 2026-07-18

- Converted the GetBible-specific shell into a reusable website desktop application template.
- Added JSON configuration, environment overrides, generated metadata, and generated platform icons.

## 1.0.4 — 2026-07-18

- Added server-side and JavaScript detection for the desktop application.

## 1.0.3 — 2026-07-18

- Restricted top-level navigation to configured origins and disabled context-menu input.

## 1.0.2 — 2026-07-18

- Corrected the target domain to `getbible.life`.

## 1.0.1 — 2026-07-18

- Corrected Linux GTK include ordering.

## 1.0.0 — 2026-07-18

- Initial desktop shell based on upstream CHOC.
