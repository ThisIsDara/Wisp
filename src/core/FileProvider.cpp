#include "core/FileProvider.h"
#include "core/FuzzyMatcher.h"
#include <QSet>

static QString pathIdOf(const AppEntry &e) { return e.targetPath.toCaseFolded(); }

QVector<ScoredEntry> FileProvider::query(const QString &q, int limit, bool exact)
{
    Q_UNUSED(exact)
    const QString trimmed = q.trimmed();
    const int minTier = (trimmed.size() == 1) ? 800 : (trimmed.size() == 2 ? 600 : 0);
    // Index rows first (trigram-accelerated), capped generously for sorting.
    QVector<ScoredEntry> out;
    QSet<QString> seenPaths;

    if (m_index) {
        auto candidates = m_index->queryCandidates(trimmed);
        out.reserve(qMin(candidates.size(), limit * 2));
        for (auto &c : candidates) {
            QString disp = c.path.mid(c.path.lastIndexOf(u'\\') + 1);
            if (disp.endsWith(u".exe", Qt::CaseInsensitive) || disp.endsWith(u".lnk", Qt::CaseInsensitive))
                disp.chop(4);
            auto r = FuzzyMatcher::score(trimmed, disp);
            if (r.score > 0 && r.score < minTier) continue;
            if (r.score == 0 && minTier > 0) continue;
            const int total = r.score > 0 ? r.score : kPathMatchScore;
            AppEntry e;
            e.source = AppEntry::Source::File;
            e.displayName = disp;
            e.targetPath = c.path;
            e.isFolder = c.isFolder;
            seenPaths.insert(pathIdOf(e));
            out.append({std::move(e), FuzzyMatcher::Result{total, r.ranges}, total});
        }
    }

    // Manual picks (added executables) — searchable like any row, deduped
    // against index rows by path (the catalog-row-wins parity from mergeFiles).
    if (m_addedSource) {
        const auto added = m_addedSource();
        for (const AppEntry &ae : added) {
            if (seenPaths.contains(pathIdOf(ae))) continue;
            auto r = FuzzyMatcher::score(trimmed, ae.displayName);
            if (r.score > 0 && r.score < minTier) continue;
            if (r.score == 0 && minTier > 0) continue;
            const int total = r.score > 0 ? r.score : kPathMatchScore;
            AppEntry e = ae;
            e.source = AppEntry::Source::File;
            out.append({std::move(e), FuzzyMatcher::Result{total, r.ranges}, total});
        }
    }

    std::sort(out.begin(), out.end(), [](const ScoredEntry &a, const ScoredEntry &b){
        if (a.totalScore != b.totalScore) return a.totalScore > b.totalScore;
        return a.entry.displayName.toCaseFolded() < b.entry.displayName.toCaseFolded();
    });
    if (out.size() > limit) out.resize(limit);
    return out;
}
