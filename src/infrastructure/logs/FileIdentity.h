#pragma once

#include <QString>

namespace devhub::infrastructure::logs {

struct FileIdentity final {
    QString key;
    [[nodiscard]] bool isValid() const noexcept { return !key.isEmpty(); }
    friend bool operator==(const FileIdentity&, const FileIdentity&) = default;
};

[[nodiscard]] FileIdentity identifyFile(const QString& path);

} // namespace devhub::infrastructure::logs
