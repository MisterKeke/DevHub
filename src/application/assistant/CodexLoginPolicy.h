#pragma once
#include <QUrl>
namespace devhub::application::assistant {
// Shared with the pinned runtime contract and the explicit UI browser action.
inline bool acceptsCodexLoginUrl(const QUrl& url, bool deviceCode)
{
    if (!url.isValid() || url.toEncoded().size() > 8192 || url.scheme() != "https" || !url.userInfo().isEmpty()
        || (url.port() != -1 && url.port() != 443) || !url.fragment().isEmpty()) return false;
    if (deviceCode) return url.host() == "auth.openai.com" && url.path() == "/codex/device" && !url.hasQuery();
    return (url.host() == "auth.openai.com" && url.path() == "/oauth/authorize")
        || (url.host() == "chatgpt.com" && url.path() == "/oauth/authorize");
}
}
