#include "DatabaseTypes.h"

namespace devhub::application::databases {

QString databaseObjectKindName(DatabaseObjectKind kind)
{
    switch (kind) {
    case DatabaseObjectKind::Table: return QStringLiteral("Table");
    case DatabaseObjectKind::View: return QStringLiteral("View");
    case DatabaseObjectKind::Index: return QStringLiteral("Index");
    case DatabaseObjectKind::Trigger: return QStringLiteral("Trigger");
    }
    return QStringLiteral("Object");
}

} // namespace devhub::application::databases
