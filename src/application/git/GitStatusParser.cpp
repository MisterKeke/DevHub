#include "GitStatusParser.h"
#include <QRegularExpression>

namespace devhub::application::git {
namespace {
QByteArrayList splitFields(const QByteArray& record, int count, QByteArray& tail) {
    QByteArrayList fields; qsizetype position = 0;
    for (int i = 0; i < count; ++i) {
        const qsizetype space = record.indexOf(' ', position);
        if (space < 0) { tail.clear(); return {}; }
        fields.append(record.mid(position, space - position)); position = space + 1;
    }
    tail = record.mid(position); return fields;
}
QString displayPath(const QByteArray& bytes) { return QString::fromUtf8(bytes); }
QChar statusChar(const QByteArray& status, qsizetype index) {
    return index < status.size() ? QLatin1Char(status.at(index)) : QLatin1Char(' ');
}
}
GitRepositoryState GitStatusParser::parse(const QByteArray& output) {
    GitRepositoryState state; state.isRepository = true;
    const QList<QByteArray> records = output.split('\0');
    for (qsizetype index = 0; index < records.size(); ++index) {
        const QByteArray& record = records.at(index);
        if (record.isEmpty()) continue;
        if (record.startsWith("# branch.oid ")) { state.objectId = QString::fromLatin1(record.mid(13)); continue; }
        if (record.startsWith("# branch.head ")) {
            state.branch = QString::fromUtf8(record.mid(14));
            state.detached = state.branch == QStringLiteral("(detached)"); continue;
        }
        if (record.startsWith("# branch.upstream ")) { state.upstream = QString::fromUtf8(record.mid(18)); continue; }
        if (record.startsWith("# branch.ab ")) {
            static const QRegularExpression ab(QStringLiteral("\\+(\\d+) -(\\d+)"));
            const auto match = ab.match(QString::fromLatin1(record.mid(12)));
            if (match.hasMatch()) { state.ahead = match.captured(1).toInt(); state.behind = match.captured(2).toInt(); }
            continue;
        }
        GitFileStatus file;
        QByteArray path;
        if (record.startsWith("? ")) {
            file.kind = GitFileKind::Untracked; path = record.mid(2);
        } else if (record.startsWith("1 ")) {
            const auto fields = splitFields(record, 8, path);
            if (fields.size() != 8) continue;
            file.indexStatus = statusChar(fields[1], 0); file.workTreeStatus = statusChar(fields[1], 1);
        } else if (record.startsWith("2 ")) {
            const auto fields = splitFields(record, 9, path);
            if (fields.size() != 9 || index + 1 >= records.size()) continue;
            file.indexStatus = statusChar(fields[1], 0); file.workTreeStatus = statusChar(fields[1], 1);
            file.kind = fields[8].startsWith('C') ? GitFileKind::Copy : GitFileKind::Rename;
            file.originalPathBytes = records.at(++index); file.originalPath = displayPath(file.originalPathBytes);
        } else if (record.startsWith("u ")) {
            const auto fields = splitFields(record, 10, path);
            if (fields.size() != 10) continue;
            file.kind = GitFileKind::Unmerged;
            file.indexStatus = statusChar(fields[1], 0); file.workTreeStatus = statusChar(fields[1], 1);
        } else continue;
        file.pathBytes = path; file.path = displayPath(path); state.files.append(std::move(file));
    }
    return state;
}
}
