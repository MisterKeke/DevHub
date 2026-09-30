#include "WorkspaceStateStore.h"

#include <QSettings>

namespace devhub::application::workspaces {
namespace {
QString key(const domain::account::AccountId& id) { return QStringLiteral("workspaces/%1/selected").arg(id.toString()); }
}
std::optional<domain::workspace::WorkspaceId> WorkspaceStateStore::selected(const domain::account::AccountId& accountId) const
{
    const QString stored = QSettings().value(key(accountId)).toString();
    if (stored.isEmpty()) return std::nullopt;
    auto id = domain::workspace::WorkspaceId::fromString(stored);
    if (!id) return std::nullopt;
    return std::move(id).value();
}
void WorkspaceStateStore::setSelected(const domain::account::AccountId& accountId,
                                      const domain::workspace::WorkspaceId& workspaceId) const
{ QSettings().setValue(key(accountId), workspaceId.toString()); }
void WorkspaceStateStore::clearSelected(const domain::account::AccountId& accountId) const
{ QSettings().remove(key(accountId)); }
} // namespace devhub::application::workspaces
