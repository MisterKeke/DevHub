#include "NoteFolderExporter.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace devhub::application::notes
{
namespace
{
shared::common::Result<QString> failure(const QString &message)
{
    return shared::common::Result<QString>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::IoFailure, domain::errors::ErrorCategory::FileSystem, message,
        message, QStringLiteral("export note folder")));
}

QString safeSegment(QString value)
{
    value.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")),
                  QStringLiteral("_"));
    while (value.endsWith(QLatin1Char('.')) || value.endsWith(QLatin1Char(' ')))
        value.chop(1);
    if (value.isEmpty())
        value = QStringLiteral("Untitled");
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(value).hasMatch())
        value.prepend(QLatin1Char('_'));
    return value;
}
} // namespace

shared::common::Result<QString> exportNoteFolder(INotesRepository &repository,
                                                 const QString &workspaceId,
                                                 const QString &folderId,
                                                 const QString &destination)
{
    const auto listed = repository.list(workspaceId, false);
    if (!listed)
        return shared::common::Result<QString>::failure(listed.error());
    QString rootName;
    for (const auto &node : listed.value())
    {
        if (node.id == folderId && node.kind == NoteNodeKind::Folder && !node.trashed)
            rootName = safeSegment(node.title);
    }
    if (rootName.isEmpty())
        return failure(QStringLiteral("Choose an existing notes folder to export."));
    QHash<QString, QString> relative{{folderId, rootName}};
    QSet<QString> paths{rootName.toCaseFolded()};
    for (qsizetype pass = 0; pass < listed.value().size(); ++pass)
    {
        bool changed = false;
        for (const auto &node : listed.value())
        {
            if (relative.contains(node.id) || !relative.contains(node.parentId) || node.trashed)
                continue;
            const QString name =
                safeSegment(node.title) +
                (node.kind == NoteNodeKind::Note ? QStringLiteral(".md") : QString());
            const QString path = QDir(relative.value(node.parentId)).filePath(name);
            if (paths.contains(path.toCaseFolded()))
                return failure(QStringLiteral("Multiple note items would export to “%1”. Rename "
                                              "them to distinct filenames before exporting.")
                                   .arg(path));
            paths.insert(path.toCaseFolded());
            relative.insert(node.id, path);
            changed = true;
        }
        if (!changed)
            break;
    }

    QDir target(destination);
    const QString exportRoot = target.filePath(rootName);
    if (!target.exists() || !target.mkdir(rootName))
        return failure(QStringLiteral("Could not create export folder “%1”. Choose an existing "
                                      "destination where that name is unused.")
                           .arg(exportRoot));
    for (const auto &node : listed.value())
    {
        if (node.kind != NoteNodeKind::Folder || !relative.contains(node.id))
            continue;
        if (!QDir().mkpath(target.filePath(relative.value(node.id))))
            return failure(QStringLiteral("Could not create a folder. The partial export is at %1.")
                               .arg(exportRoot));
    }
    for (const auto &node : listed.value())
    {
        if (node.kind != NoteNodeKind::Note || !relative.contains(node.id))
            continue;
        const auto content = repository.readContent(workspaceId, node.id);
        if (!content)
            return failure(content.error().userMessage +
                           QStringLiteral(" The partial export is at %1.").arg(exportRoot));
        QSaveFile file(target.filePath(relative.value(node.id)));
        const QByteArray bytes = content.value().toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            return failure(
                QStringLiteral("Could not write a note: %1. The partial export is at %2.")
                    .arg(file.errorString(), exportRoot));
    }
    return shared::common::Result<QString>::success(exportRoot);
}

} // namespace devhub::application::notes
