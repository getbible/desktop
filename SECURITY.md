# Security policy

## Supported versions

Security fixes are applied to the current `main` branch and the most recent published release.

## Reporting a vulnerability

Do not open a public issue for a suspected vulnerability. Use GitHub's private vulnerability reporting for this repository when available, or contact the project maintainers through the support details configured for the application.

Include:

- the affected version and operating system;
- reproducible steps;
- the expected and observed behaviour;
- the security impact;
- any proposed mitigation.

## Security model

The application embeds the operating system's native web engine through CHOC. It does not bundle passwords or application credentials. Cookies, local storage, and authenticated sessions are managed by the platform web engine.

Top-level navigation is restricted to the HTTPS origins configured in `app.allowed_origins`. The embedded JavaScript identity and custom user-agent product are presentation hints only; they must never be treated as authentication or authorization evidence.

Repository and workflow secrets are read only during GitHub Actions jobs. Never place Store credentials, signing certificates, passwords, or private configuration directly in tracked files.
