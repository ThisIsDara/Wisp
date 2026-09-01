#include "core/AppProvider.h"
#include "core/Calculator.h"
#include <QSet>

static QString idOfApp(const AppEntry &e) { return e.targetPath.isEmpty() ? e.aumid : e.targetPath; }

void AppProvider::setEntries(const QVector<AppEntry> &entries,
                             const QVector<QString> &lowers,
                             const QVector<QVector<char>> &bounds)
{
    m_entries = entries;
    m_lowers = lowers;
    m_bounds = bounds;
}

void AppProvider::setMeta(const QSet<QString> &favIds, bool showHidden, bool favOnly,
                          const QHash<QString,int> &frecencyMap)
{
    m_favIds = favIds;
    m_showHidden = showHidden;
    m_favOnly = favOnly;
    m_frecency = frecencyMap;
}

QVector<ScoredEntry> AppProvider::query(const QString &q, int limit, bool exact)
{
    Q_UNUSED(exact)
    const QString qLower = q.trimmed().toLower();
    const int minTier = (qLower.size() == 1) ? 800 : (qLower.size() == 2 ? 600 : 0);
    QVector<ScoredEntry> out;
    out.reserve(limit);

    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).hidden && !m_showHidden) {
            if (!(m_favOnly && m_favIds.contains(idOfApp(m_entries.at(i))))) continue;
        }
        FuzzyMatcher::Result r = FuzzyMatcher::score(q, m_entries.at(i).displayName);
        if (r.score > 0 && r.score < minTier) continue;
        if (r.score == 0) continue;
        r.score += m_frecency.value(idOfApp(m_entries.at(i)), 0);
        out.append({m_entries.at(i), r, r.score});
        if (out.size() >= limit * 2) break; // collect enough for sorting, cap early
    }
    std::sort(out.begin(), out.end(), [](const ScoredEntry &a, const ScoredEntry &b){
        if (a.totalScore != b.totalScore) return a.totalScore > b.totalScore;
        return a.entry.displayName.toCaseFolded() < b.entry.displayName.toCaseFolded();
    });
    if (out.size() > limit) out.resize(limit);
    // Favorites filter
    if (m_favOnly) {
        QVector<ScoredEntry> keep; keep.reserve(out.size());
        for (auto &e : out) if (m_favIds.contains(idOfApp(e.entry))) keep.append(e);
        return keep;
    }
    return out;
}
