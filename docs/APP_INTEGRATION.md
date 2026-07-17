# Website integration

The desktop shell exposes a stable application identity before the website's normal scripts execute.

## JavaScript identity

Every page receives:

```javascript
window.WebAppDesktop
```

The configured alias is also created. For the default profile:

```javascript
window.GetBibleApp
```

Example:

```javascript
if (window.WebAppDesktop?.isApp === true) {
    document.documentElement.classList.add('running-in-desktop-shell');
    console.log(window.WebAppDesktop.version);
    console.log(window.WebAppDesktop.platform);
}
```

The object includes the application name, slug, version, platform, application URL, About URL, and Support URL. It is frozen and installed as a non-writable property.

The document root also receives:

```html
<html
  class="webapp-desktop-app getbible-life-desktop-app"
  data-webapp-desktop="true"
  data-webapp-desktop-platform="linux"
  data-webapp-desktop-version="1.0.0"
  data-webapp-desktop-slug="getbible-life">
```

CSS example:

```css
html.webapp-desktop-app .browser-only {
    display: none !important;
}

html:not(.webapp-desktop-app) .desktop-only {
    display: none !important;
}
```

## Server-side detection

On Linux, the native WebKit user agent retains its normal value and appends:

```text
GetBibleLifeDesktop/1.0.0
```

PHP example:

```php
<?php
$userAgent = $_SERVER['HTTP_USER_AGENT'] ?? '';
$isDesktopApp = preg_match(
    '/(?:^|\s)GetBibleLifeDesktop\/([0-9A-Za-z.+-]+)/',
    $userAgent,
    $matches
) === 1;
$appVersion = $isDesktopApp ? $matches[1] : null;
```

This marker is suitable for layout and feature selection. It is not authentication because any HTTP client can imitate a user agent.

## About and Support

The website may provide controls in a position that fits its own layout:

```javascript
document.querySelector('#desktop-about')?.addEventListener('click', () => {
    window.location.assign(window.WebAppDesktop.aboutURL);
});

document.querySelector('#desktop-support')?.addEventListener('click', () => {
    window.location.assign(window.WebAppDesktop.supportURL);
});
```

The built-in floating menu is disabled in the default profile to avoid covering website controls.

## Navigation restrictions

Top-level pages may load only from `app.allowed_origins` or the internal `webapp://app/` pages. Attempts to open unrelated websites are cancelled and return to the configured application URL.

Subresources required by the allowed website, such as images, APIs, fonts, or authentication resources, are not subjected to the top-level origin restriction. Configure the website's own Content Security Policy and CORS rules normally.

Right-click, middle-click, auxiliary mouse buttons, the context-menu key, and `Shift+F10` are blocked. Normal left-click interaction remains available.
