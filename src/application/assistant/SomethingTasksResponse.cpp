#include "SomethingTasksResponse.h"

#include <QJsonArray>
#include <algorithm>

namespace devhub::application::assistant {
namespace {

bool shortenText(QJsonObject& object, const QString& key, qsizetype minimumLength)
{
    const QString text = object.value(key).toString();
    if (text.size() <= minimumLength) return false;
    qsizetype length = std::max(minimumLength, text.size() / 2);
    // Never cut between UTF-16 surrogate halves.
    if (text.at(length - 1).isHighSurrogate() && text.at(length).isLowSurrogate()) --length;
    object.insert(key, text.left(length));
    object.insert(key + QStringLiteral("Truncated"), true);
    return true;
}

bool shortenTask(QJsonObject& task)
{
    bool changed = shortenText(task, QStringLiteral("title"), 32);
    changed |= shortenText(task, QStringLiteral("description"), 256);
    QJsonArray subtasks = task.value(QStringLiteral("subtasks")).toArray();
    for (qsizetype i = 0; i < subtasks.size(); ++i) {
        QJsonObject subtask = subtasks.at(i).toObject();
        if (shortenText(subtask, QStringLiteral("title"), 32)) {
            subtasks.replace(i, subtask);
            changed = true;
        }
    }
    if (task.contains(QStringLiteral("subtasks"))) task.insert(QStringLiteral("subtasks"), subtasks);
    if (changed) return true;

    // Keep remaining tag names exact; omit whole entries rather than changing
    // their meaning by shortening them.
    QJsonArray tags = task.value(QStringLiteral("tags")).toArray();
    if (!tags.isEmpty()) {
        tags.removeLast();
        task.insert(QStringLiteral("tags"), tags);
        task.insert(QStringLiteral("tagsTruncated"), true);
        return true;
    }
    return false;
}

bool taskTruncated(const QJsonObject& task)
{
    if (task.value(QStringLiteral("titleTruncated")).toBool()
        || task.value(QStringLiteral("descriptionTruncated")).toBool()
        || task.value(QStringLiteral("tagsTruncated")).toBool()
        || task.value(QStringLiteral("subtasksTruncated")).toBool()) return true;
    for (const auto& subtask : task.value(QStringLiteral("subtasks")).toArray())
        if (subtask.toObject().value(QStringLiteral("titleTruncated")).toBool()) return true;
    return false;
}

} // namespace

ToolResult boundSomethingTasksResponse(ToolResult result, qsizetype maximumBytes)
{
    if (!result.success) return result;
    QJsonArray tasks = result.data.value(QStringLiteral("tasks")).toArray();
    QJsonObject task = result.data.value(QStringLiteral("task")).toObject();
    bool truncated = result.truncated || result.data.value(QStringLiteral("truncated")).toBool()
        || taskTruncated(task);
    for (const auto& item : tasks) truncated |= taskTruncated(item.toObject());
    result.truncated = truncated;
    result.data.insert(QStringLiteral("truncated"), truncated);

    while (result.compactJson().size() > maximumBytes) {
        bool changed = false;
        for (qsizetype i = 0; i < tasks.size(); ++i) {
            QJsonObject summary = tasks.at(i).toObject();
            changed |= shortenTask(summary);
            tasks.replace(i, summary);
        }
        if (!task.isEmpty()) changed |= shortenTask(task);
        if (!changed) {
            if (tasks.size() > 1) {
                tasks.removeLast();
                result.data.insert(QStringLiteral("hasMore"), true);
                // Byte-limited pages must resume after returned rows, not the
                // requested limit, or the removed rows would be skipped.
                result.data.insert(QStringLiteral("nextOffset"),
                    result.data.value(QStringLiteral("offset")).toInt() + tasks.size());
            } else {
                QJsonArray subtasks = task.value(QStringLiteral("subtasks")).toArray();
                if (subtasks.isEmpty()) {
                    // Required metadata alone is outside the read contract's
                    // budget. Fail explicitly instead of reporting an empty success.
                    result.success = false;
                    result.errorCode = QStringLiteral("output_limit");
                    result.data.remove(QStringLiteral("tasks"));
                    result.data.remove(QStringLiteral("task"));
                    result.data.insert(QStringLiteral("message"),
                        QStringLiteral("Required task metadata exceeds the response limit."));
                    return result;
                }
                subtasks.removeLast();
                task.insert(QStringLiteral("subtasks"), subtasks);
                task.insert(QStringLiteral("subtasksTruncated"), true);
            }
        }
        result.truncated = true;
        result.data.insert(QStringLiteral("truncated"), true);
        if (result.data.contains(QStringLiteral("tasks"))) result.data.insert(QStringLiteral("tasks"), tasks);
        if (!task.isEmpty()) result.data.insert(QStringLiteral("task"), task);
    }
    return result;
}

} // namespace devhub::application::assistant
