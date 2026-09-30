#include "ICommandSearchProvider.h"

#include <algorithm>

namespace devhub::application::commands {

namespace {
int fuzzyScore(const QString& query, const QString& text)
{
    const QString needle = query.trimmed().toCaseFolded();
    const QString haystack = text.toCaseFolded();
    if (needle.isEmpty()) return 0;
    if (haystack == needle) return 1000;
    if (haystack.startsWith(needle)) return 800 - static_cast<int>(haystack.size() - needle.size());
    const int substring = static_cast<int>(haystack.indexOf(needle));
    if (substring >= 0) return 600 - substring;
    int position = 0;
    int gaps = 0;
    for (QChar ch : needle) {
        const int found = static_cast<int>(haystack.indexOf(ch, position));
        if (found < 0) return -1;
        gaps += found - position;
        position = found + 1;
    }
    return 300 - gaps;
}
}

QVector<SearchCandidate> CommandSearchProvider::search(const QString& query,
                                                        const CommandContext& context) const
{
    Q_UNUSED(context);
    struct Ranked final { int score; SearchCandidate candidate; };
    QVector<Ranked> ranked;
    const QStringList recent = m_registry.recentCommandIds();
    for (const auto& command : m_registry.commands()) {
        const QString text = command.title + QLatin1Char(' ') + command.description + QLatin1Char(' ')
            + command.keywords.join(QLatin1Char(' '));
        int score = fuzzyScore(query, text);
        if (query.trimmed().isEmpty())
            score = 100 - qMax(0, static_cast<int>(recent.indexOf(command.id))) * 3;
        if (score < 0) continue;
        score += recent.contains(command.id) ? 20 : 0;
        ranked.push_back({score, {command.title, command.availability.available
            ? command.description : command.availability.reason, command.id, name()}});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& left, const Ranked& right) {
        return left.score > right.score;
    });
    QVector<SearchCandidate> results;
    results.reserve(ranked.size());
    for (auto& match : ranked) results.push_back(std::move(match.candidate));
    return results;
}

QVector<SearchCandidate> CachedSearchProvider::search(const QString& query,
                                                       const CommandContext& context) const
{
    Q_UNUSED(context);
    QVector<SearchCandidate> matches;
    const QString needle = query.trimmed();
    for (auto candidate : m_entries) {
        if (needle.isEmpty() || candidate.title.contains(needle, Qt::CaseInsensitive)
            || candidate.detail.contains(needle, Qt::CaseInsensitive)) {
            candidate.provider = m_name;
            matches.push_back(std::move(candidate));
        }
    }
    return matches;
}

} // namespace devhub::application::commands
