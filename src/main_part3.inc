std::string createApplicationIdentityScript()
{
    std::string script = R"JS((() => {
        if (window.WebAppDesktop?.isApp) return;

        const identity = Object.freeze({
            ...__IDENTITY_JSON__,
            platform: __PLATFORM__,
            slug: __SLUG__
        });

        Object.defineProperty(window, "WebAppDesktop", {
            configurable: false,
            enumerable: true,
            writable: false,
            value: identity
        });

        Object.defineProperty(window, __CUSTOM_OBJECT__, {
            configurable: false,
            enumerable: true,
            writable: false,
            value: identity
        });

        const markDocument = () => {
            const root = document.documentElement;
            if (!root) return false;

            root.classList.add("webapp-desktop-app", __HTML_CLASS__);
            root.dataset.webappDesktop = "true";
            root.dataset.webappDesktopPlatform = identity.platform;
            root.dataset.webappDesktopVersion = identity.version;
            root.dataset.webappDesktopSlug = identity.slug;
            return true;
        };

        if (!markDocument()) {
            const observer = new MutationObserver(() => {
                if (markDocument()) observer.disconnect();
            });
            observer.observe(document, { childList: true, subtree: true });
        }

        window.dispatchEvent(new CustomEvent("webappdesktopready", { detail: identity }));
    })();)JS";

    replaceAll(script, "__IDENTITY_JSON__", identityJSON);
    replaceAll(script, "__PLATFORM__", jsonString(appPlatform));
    replaceAll(script, "__SLUG__", jsonString(slug));
    replaceAll(script, "__CUSTOM_OBJECT__", jsonString(javascriptObject));
    replaceAll(script, "__HTML_CLASS__", jsonString(htmlClass));
    return script;
}

std::string createAppMenuScript()
{
    if (!showAppMenu)
    {
        return "";
    }

    std::string script = R"JS((() => {
        const shellScheme = "webapp:";
        if (window.location.protocol === shellScheme) return;
        if (window.__webappDesktopMenuInitialised) return;
        window.__webappDesktopMenuInitialised = true;

        const mountMenu = () => {
            if (!document.documentElement || document.getElementById("webapp-desktop-menu-host")) return false;

            const host = document.createElement("div");
            host.id = "webapp-desktop-menu-host";
            host.setAttribute("aria-label", __APP_NAME__ + " application menu");
            const shadow = host.attachShadow({ mode: "closed" });
            shadow.innerHTML = `
                <style>
                    :host { all: initial; position: fixed; z-index: 2147483647; top: 12px; right: 12px; }
                    button { font: 600 13px/1 system-ui, sans-serif; }
                    .trigger {
                        width: 36px; height: 36px; border: 1px solid rgba(255,255,255,.25);
                        border-radius: 999px; background: rgba(17,17,17,.88); color: white;
                        box-shadow: 0 4px 18px rgba(0,0,0,.28); cursor: pointer;
                    }
                    .menu {
                        display: none; min-width: 170px; margin-top: 8px; padding: 6px;
                        border-radius: 10px; background: rgba(17,17,17,.95);
                        box-shadow: 0 8px 28px rgba(0,0,0,.32);
                    }
                    .menu.open { display: grid; gap: 4px; }
                    .menu button {
                        border: 0; border-radius: 7px; padding: 10px 12px;
                        background: transparent; color: white; text-align: left; cursor: pointer;
                    }
                    .menu button:hover, .menu button:focus { background: rgba(255,255,255,.13); outline: none; }
                </style>
                <button class="trigger" type="button" aria-expanded="false" aria-label="Application menu">•••</button>
                <div class="menu" role="menu">
                    <button type="button" data-page="about" role="menuitem">About</button>
                    <button type="button" data-page="support" role="menuitem">${__SUPPORT_LABEL__}</button>
                </div>`;

            const trigger = shadow.querySelector(".trigger");
            const menu = shadow.querySelector(".menu");
            const close = () => {
                menu.classList.remove("open");
                trigger.setAttribute("aria-expanded", "false");
            };

            trigger.addEventListener("click", event => {
                event.preventDefault();
                event.stopPropagation();
                const open = menu.classList.toggle("open");
                trigger.setAttribute("aria-expanded", String(open));
            });

            shadow.querySelector('[data-page="about"]').addEventListener("click", () => {
                window.location.assign(__ABOUT_URL__);
            });
            shadow.querySelector('[data-page="support"]').addEventListener("click", () => {
                window.location.assign(__SUPPORT_URL__);
            });
            document.addEventListener("click", close, true);
            document.documentElement.appendChild(host);
            return true;
        };

        if (!mountMenu()) {
            const observer = new MutationObserver(() => {
                if (mountMenu()) observer.disconnect();
            });
            observer.observe(document, { childList: true, subtree: true });
        }
    })();)JS";

    replaceAll(script, "__APP_NAME__", jsonString(name));
    replaceAll(script, "__SUPPORT_LABEL__", jsonString(supportLabel));
    replaceAll(script, "__ABOUT_URL__", jsonString(aboutURL));
    replaceAll(script, "__SUPPORT_URL__", jsonString(supportPageURL));
    return script;
}
