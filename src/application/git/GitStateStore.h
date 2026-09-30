#pragma once
#include "GitTypes.h"
#include <QObject>
#include <optional>

namespace devhub::application::git {
class GitStateStore final : public QObject {
    Q_OBJECT
public:
    explicit GitStateStore(QObject* parent = nullptr) : QObject(parent) {}
    [[nodiscard]] bool beginStatus(quint64 generation);
    void completeStatus(GitRepositoryState state, quint64 generation);
    void failStatus(QString message, quint64 generation);
    [[nodiscard]] const std::optional<GitRepositoryState>& status() const noexcept { return m_status; }
    [[nodiscard]] bool isLoading() const noexcept { return m_loading; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] bool isFresh(std::chrono::seconds age = std::chrono::seconds(5)) const;
signals:
    void changed();
private:
    std::optional<GitRepositoryState> m_status;
    quint64 m_generation = 0;
    bool m_loading = false;
    QString m_error;
};
}
