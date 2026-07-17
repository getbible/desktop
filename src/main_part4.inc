std::string createNavigationPolicyScript()
{
    std::string script = R"JS((() => {
        if (window.__webappDesktopPolicyInitialised) return;
        window.__webappDesktopPolicyInitialised = true;

        const homeURL = __HOME_URL__;
        const offlineURL = __OFFLINE_URL__;
        const allowedOrigins = new Set(__ALLOWED_ORIGINS__);

        const stopEvent = event => {
            event.preventDefault();
            event.stopImmediatePropagation();
        };

        const resolveURL = value => {
            try { return new URL(value, window.location.href); }
            catch (error) { return null; }
        };

        const isAllowedURL = value => {
            const url = resolveURL(value);
            if (!url) return false;
            if (allowedOrigins.has(url.origin)) return true;
            if (url.protocol === "webapp:" && url.host === "app") return true;
            if (url.protocol === "blob:") {
                const nested = resolveURL(url.pathname);
                return Boolean(nested && allowedOrigins.has(nested.origin));
            }
            return url.href === "about:blank";
        };

        const navigateInApplication = value => {
            const destination = resolveURL(value);
            window.location.assign(destination && isAllowedURL(destination.href)
                ? destination.href
                : homeURL);
        };

        if (!isAllowedURL(window.location.href)) {
            window.location.replace(homeURL);
            return;
        }

        window.addEventListener("offline", () => {
            if (window.location.protocol !== "webapp:") window.location.replace(offlineURL);
        });

        document.addEventListener("contextmenu", stopEvent, true);
        document.addEventListener("auxclick", stopEvent, true);
        document.addEventListener("mousedown", event => {
            if (event.button !== 0) stopEvent(event);
        }, true);
        document.addEventListener("pointerdown", event => {
            if (event.button !== 0) stopEvent(event);
        }, true);
        document.addEventListener("keydown", event => {
            if (event.key === "ContextMenu" || (event.shiftKey && event.key === "F10")) stopEvent(event);
        }, true);

        document.addEventListener("click", event => {
            if (event.button !== 0) {
                stopEvent(event);
                return;
            }

            const anchor = event.target instanceof Element ? event.target.closest("a[href]") : null;
            if (!anchor) return;

            const destination = resolveURL(anchor.href);
            const otherContext = anchor.target && anchor.target.toLowerCase() !== "_self";
            const modifiedClick = event.ctrlKey || event.metaKey || event.shiftKey || event.altKey;

            if (!destination || !isAllowedURL(destination.href)) {
                stopEvent(event);
                window.location.replace(homeURL);
                return;
            }

            if (otherContext || modifiedClick) {
                stopEvent(event);
                window.location.assign(destination.href);
            }
        }, true);

        document.addEventListener("submit", event => {
            const form = event.target instanceof HTMLFormElement ? event.target : null;
            if (!form || isAllowedURL(form.action)) return;
            stopEvent(event);
            window.location.replace(homeURL);
        }, true);

        const controlledWindowOpen = value => {
            navigateInApplication(value || homeURL);
            return window;
        };

        try {
            Object.defineProperty(window, "open", {
                configurable: false,
                enumerable: true,
                writable: false,
                value: controlledWindowOpen
            });
        } catch (error) {
            window.open = controlledWindowOpen;
        }
    })();)JS";

    replaceAll(script, "__HOME_URL__", jsonString(appURL));
    replaceAll(script, "__OFFLINE_URL__", jsonString(offlineURL));
    replaceAll(script, "__ALLOWED_ORIGINS__", allowedOriginsJSON);
    return script;
}
