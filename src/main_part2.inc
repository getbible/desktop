std::string createOfflinePage()
{
    std::ostringstream body;
    body << "<p>" << htmlEscape(offlineMessage) << "</p>"
         << "<div id=\"status\" role=\"status\" aria-live=\"polite\"></div>";

    const std::string actions =
        "<button id=\"retry\" type=\"button\">" + htmlEscape(offlineRetryLabel) + "</button>";

    std::string script = R"JS((() => {
        const target = __APP_URL__;
        const retryButton = document.getElementById("retry");
        const status = document.getElementById("status");
        let checking = false;

        async function connect() {
            if (checking) return;
            checking = true;
            retryButton.disabled = true;
            status.textContent = __CHECKING_MESSAGE__;

            const controller = new AbortController();
            const timeout = window.setTimeout(() => controller.abort(), 8000);

            try {
                await fetch(target, {
                    method: "GET",
                    mode: "no-cors",
                    cache: "no-store",
                    redirect: "follow",
                    signal: controller.signal
                });
                status.textContent = __CONNECTED_MESSAGE__;
                window.location.replace(target);
                return;
            } catch (error) {
                status.textContent = __FAILED_MESSAGE__;
            } finally {
                window.clearTimeout(timeout);
                checking = false;
                retryButton.disabled = false;
            }
        }

        retryButton.addEventListener("click", connect);
        window.addEventListener("online", connect);
        window.setInterval(connect, 5000);
        connect();
    })();)JS";

    replaceAll(script, "__APP_URL__", jsonString(appURL));
    replaceAll(script, "__CHECKING_MESSAGE__", jsonString(offlineCheckingMessage));
    replaceAll(script, "__CONNECTED_MESSAGE__", jsonString(offlineConnectedMessage));
    replaceAll(script, "__FAILED_MESSAGE__", jsonString(offlineFailedMessage));
    return createPage(offlineTitle, body.str(), actions, script);
}

std::string createAboutPage()
{
    std::ostringstream body;
    body << "<p>" << htmlEscape(aboutText) << "</p>"
         << "<p class=\"meta\"><strong>Version:</strong> " << htmlEscape(version) << "<br>"
         << "<strong>Platform:</strong> " << htmlEscape(appPlatform) << "<br>"
         << "<strong>Website:</strong> <code>" << htmlEscape(appURL) << "</code></p>";

    const std::string actions =
        "<a href=\"" + htmlEscape(appURL) + "\">Open application</a>"
        "<a class=\"secondary\" href=\"" + htmlEscape(supportPageURL) + "\">" +
        htmlEscape(supportLabel) + "</a>";

    return createPage("About " + std::string(name), body.str(), actions);
}

std::string createSupportPage()
{
    std::ostringstream body;
    body << "<p>" << htmlEscape(supportMessage) << "</p>";

    if (!supportEmail.empty())
    {
        body << "<p class=\"meta\"><strong>Email:</strong> " << htmlEscape(supportEmail) << "</p>";
    }

    if (!supportURL.empty())
    {
        body << "<p class=\"meta\"><strong>Support website:</strong><br><code>"
             << htmlEscape(supportURL) << "</code></p>";
    }

    std::string actions;
    if (supportURLAllowed)
    {
        actions += "<a href=\"" + htmlEscape(supportURL) + "\">Open support website</a>";
    }
    actions += "<a class=\"secondary\" href=\"" + htmlEscape(appURL) + "\">Back to application</a>";

    return createPage(supportLabel, body.str(), actions);
}

std::optional<choc::ui::WebView::Options::Resource> fetchEmbeddedResource(const std::string& path)
{
    if (path.empty() || path == "/" || path == "/offline")
    {
        return choc::ui::WebView::Options::Resource(createOfflinePage(), "text/html; charset=utf-8");
    }

    if (path == "/about")
    {
        return choc::ui::WebView::Options::Resource(createAboutPage(), "text/html; charset=utf-8");
    }

    if (path == "/support")
    {
        return choc::ui::WebView::Options::Resource(createSupportPage(), "text/html; charset=utf-8");
    }

    if (path == "/icon.png")
    {
        return choc::ui::WebView::Options::Resource(
            std::string_view(
                reinterpret_cast<const char*>(webapp::generated::appIconPng),
                webapp::generated::appIconPngSize),
            "image/png");
    }

    return std::nullopt;
}
