#pragma once

#include "WorkspaceFilePolicy.h"
#include <QList>
#include <QByteArray>
#include <QStringList>
#include <atomic>
#include <memory>
#include <utility>

namespace devhub::application::files {

struct WorkspaceFileRead final {
    QString text;
    QString message;
    qint64 byteCount = 0;
    bool readable = false;
    bool binary = false;
    bool tooLarge = false;
};
struct WorkspaceFileRange final { QByteArray bytes; bool truncated = false; };
struct WorkspaceFileSearchLimits final {
    int maximumDepth = 12;
    int maximumEntries = 10000;
    int maximumMilliseconds = 2000;
    int maximumResults = 100;
};
struct WorkspaceFileSearchResult final {
    QStringList relativePaths;
    bool cancelled = false;
    bool budgetExhausted = false;
    int entriesVisited = 0;
};
class WorkspaceFileService final
{
public:
    static constexpr qint64 PreviewByteLimit = 1024 * 1024;
    explicit WorkspaceFileService(WorkspaceFilePolicy policy) : m_policy(std::move(policy)) {}
    [[nodiscard]] const WorkspaceFilePolicy& policy() const { return m_policy; }
    [[nodiscard]] WorkspaceFileRead readPreview(const QString& relativePath) const;
    [[nodiscard]] WorkspaceFileRange readRange(const QString& relativePath, qint64 offset, qint64 length,
                                               qint64 maximumReturnedBytes = 64 * 1024) const;
    [[nodiscard]] WorkspaceFileSearchResult searchNames(const QString& query,
        const std::shared_ptr<std::atomic_bool>& cancellation = {}, WorkspaceFileSearchLimits limits = {}) const;
private:
    WorkspaceFilePolicy m_policy;
};

} // namespace devhub::application::files
