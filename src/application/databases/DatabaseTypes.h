#pragma once

#include <QHash>
#include <QDateTime>
#include <QVector>
#include <QVariant>

namespace devhub::application::databases {

enum class DatabaseOpenMode { ReadOnly, ReadWrite };
enum class DatabaseObjectKind { Table, View, Index, Trigger };

struct DatabaseReference final {
    QString id;
    QString workspaceId;
    QString displayName;
    QString filePath;
    QString engineKey = QStringLiteral("sqlite");
    bool headerVerified = false;
};

struct DatabaseSchemaObject final {
    DatabaseObjectKind kind = DatabaseObjectKind::Table;
    QString name;
    QString tableName;
    QString sql;
};

struct DatabaseColumn final {
    QString name;
    QString declaredType;
    QString defaultValue;
    bool notNull = false;
    int primaryKeyOrder = 0;
    QString foreignTable;
    QString foreignColumn;
};

struct DatabasePageResult final {
    QStringList columns;
    QVector<int> primaryKeyColumns;
    QVector<QVector<QVariant>> rows;
    QVector<QByteArray> rowFingerprints;
    int pageSize = 0;
    int offset = 0;
    bool hasMore = false;
    bool withoutRowId = false;
};

struct DatabaseQueryResult final {
    QStringList columns;
    QVector<QVector<QVariant>> rows;
    qint64 durationMs = 0;
    qint64 affectedRows = -1;
    bool truncated = false;
    QString message;
};

struct PendingCellEdit final {
    QVector<QVariant> primaryKey;
    QString columnName;
    QVariant value;
    QVariant originalValue;
};

struct PendingRowInsert final {
    QHash<QString, QVariant> values;
};

struct PendingRowDelete final {
    QVector<QVariant> primaryKey;
    QByteArray originalFingerprint;
};

struct PendingChanges final {
    QString tableName;
    QVector<PendingCellEdit> cellEdits;
    QVector<PendingRowInsert> inserts;
    QVector<PendingRowDelete> deletes;
};

[[nodiscard]] QString databaseObjectKindName(DatabaseObjectKind kind);

} // namespace devhub::application::databases

Q_DECLARE_METATYPE(devhub::application::databases::DatabaseSchemaObject)
Q_DECLARE_METATYPE(QVector<devhub::application::databases::DatabaseSchemaObject>)
Q_DECLARE_METATYPE(QVector<devhub::application::databases::DatabaseColumn>)
Q_DECLARE_METATYPE(devhub::application::databases::DatabasePageResult)
Q_DECLARE_METATYPE(devhub::application::databases::DatabaseQueryResult)
Q_DECLARE_METATYPE(devhub::application::databases::PendingChanges)
