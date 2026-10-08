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

    // 2026-09-15 perf: this used to call FuzzyMatcher::score() per entry, which
    // re-lowercases the display name AND recomputes word/camelCase boundary
    // flags on EVERY keystroke — while m_lowers/m_bounds (the caches setEntries
    // was already being handed for exactly this purpose) sat unused. Score off
    // the caches instead; scoreFast is the cache-driven twin of score() and
    // returns identical scores/ranges.
    const bool haveCaches = m_lowers.size() == m_entries.size()
                            && m_bounds.size() == m_entries.size();
    // Frecency/favorite probes need the row id; fold it once per entry and
    // reuse for both the hidden check and the score boost (was two QStrings).
    for (int i = 0; i < m_entries.size(); ++i) {
        const AppEntry &e = m_entries.at(i);
        QString id;
        if (e.hidden && !m_showHidden) {
            id = e.targetPath.isEmpty() ? e.aumid : e.targetPath;
            if (!(m_favOnly && m_favIds.contains(id)))
                continue;
        }
        FuzzyMatcher::Result r = haveCaches
            ? FuzzyMatcher::scoreFast(qLower, m_lowers.at(i), m_bounds.at(i))
            : FuzzyMatcher::score(q, e.displayName);
        if (r.score > 0 && r.score < minTier) continue;
        if (r.score == 0) continue;
        if (id.isEmpty())
            id = e.targetPath.isEmpty() ? e.aumid : e.targetPath;
        r.score += m_frecency.value(id, 0);
        // Both derived keys taken here, once, while we still have the entry.
        out.append({e, r, r.score, e.displayName.toCaseFolded(), id});
        if (out.size() >= limit * 2) break; // collect enough for sorting, cap early
    }
    // 2026-09-15 perf: the tie-break folded both display names on every
    // comparison. m_lowers is already a case-folded-per-char copy, but note
    // toCaseFolded() and toLower() differ for a few characters (e.g. 'ß'), so
    // the fold is taken ONCE per collected row rather than per comparison —
    // same ordering as before, none of the per-comparison allocation.
    std::sort(out.begin(), out.end(), [](const ScoredEntry &a, const ScoredEntry &b){
        if (a.totalScore != b.totalScore) return a.totalScore > b.totalScore;
        return a.foldName < b.foldName;
    });
    if (out.size() > limit) out.resize(limit);
    // Favorites filter
    if (m_favOnly) {
        QVector<ScoredEntry> keep; keep.reserve(out.size());
        for (auto &e : out) if (m_favIds.contains(e.foldId)) keep.append(e);
        return keep;
    }
    return out;
}
