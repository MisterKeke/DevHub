#include "GitStateStore.h"
#include <QDateTime>

namespace devhub::application::git {
bool GitStateStore::beginStatus(quint64 generation) {
    if (m_loading) return false;
    m_generation = generation; m_loading = true; m_error.clear(); emit changed(); return true;
}
void GitStateStore::completeStatus(GitRepositoryState state, quint64 generation) {
    if (generation != m_generation) return;
    m_status = std::move(state); m_loading = false; m_error.clear(); emit changed();
}
void GitStateStore::failStatus(QString message, quint64 generation) {
    if (generation != m_generation) return;
    m_loading = false; m_error = std::move(message); emit changed();
}
bool GitStateStore::isFresh(std::chrono::seconds age) const {
    return m_status && m_status->fetchedAt.isValid()
        && m_status->fetchedAt.secsTo(QDateTime::currentDateTimeUtc()) <= age.count();
}
}
