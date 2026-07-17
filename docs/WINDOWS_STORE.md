# Windows build and distribution

Windows packages must be built on Windows with Visual Studio 2022 C++ tools, CMake, Git, Python 3, Pillow, and the Windows 10/11 SDK installed.

## Build and package

From PowerShell:

```powershell
python -m pip install --requirement tools/requirements.txt
./scripts/build-windows.ps1
./scripts/package-windows.ps1
```

The package is written under `dist/windows/`.

## Application identity

Before submitting to Microsoft Store, create the application in Partner Center and set these configuration values to the identities assigned by Microsoft:

```json
{
  "windows": {
    "identity_name": "Store-assigned identity name",
    "publisher": "CN=Store-assigned publisher",
    "publisher_display_name": "Publisher display name",
    "minimum_version": "10.0.17763.0",
    "architecture": "x64"
  }
}
```

The certificate subject must exactly equal `windows.publisher`.

## Local signing

Set:

```powershell
$env:WINDOWS_PFX_PATH = 'C:\secure\publisher.pfx'
$env:WINDOWS_PFX_PASSWORD = 'certificate-password'
./scripts/package-windows.ps1
```

Without a certificate, the workflow still creates an unsigned MSIX for artifact inspection. A normal Windows installation requires a trusted signature or developer-mode sideloading.

## GitHub Actions signing

Create repository secrets:

- `WINDOWS_PFX_BASE64` — raw base64 of the `.pfx` file;
- `WINDOWS_PFX_PASSWORD` — certificate password.

PowerShell encoding example:

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes('publisher.pfx')) |
  Set-Content -NoNewline publisher.pfx.base64.txt
```

The build and release workflows import the certificate only for the job and remove it with the hosted runner.

## Microsoft Store

Upload the signed MSIX from the GitHub Release to the matching Partner Center product submission. Store API submission is intentionally not enabled by default because Microsoft tenant, product, and flight identifiers vary between publishers. The release workflow always produces the package so it can be reviewed before submission.

A fork must configure its own Windows identity and signing certificate; it must not reuse the GetBible publishing identity.
