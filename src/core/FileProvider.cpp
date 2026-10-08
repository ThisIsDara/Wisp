#include "core/FileProvider.h"
#include "core/FuzzyMatcher.h"
#include <QSet>

static QString pathIdOf(const AppEntry &e) { return e.targetPath.toCaseFolded(); }

// 2026-09-15 (user report: searching "Discord" also listed "Wow", and "Hermes"
// pulled in unrelated apps).
//
// FileIndex::queryCandidates is a loose SUBSEQUENCE prefilter over the whole
// path — correct as a prefilter (it must stay a superset so nothing whose NAME
// matches is dropped early), but a long Windows path satisfies a subsequence
// by ACCIDENT. The reproduction was
//     C:\Users\Trick\Documents\Disciplines\Soccer\Recordings\Wow.exe
// which contains no "discord" as text, but d-i-s…s-c-o…o-r-d in order.
//
// Treating that as proof the path matched admitted "Wow" at the D-07 path tier
// and rendered it as a normal result row. A path must now CONTAIN the query as
// a contiguous substring to earn the path-match tier — which is what users
// actually mean (searching "steam" finds "...\Steam\steam.exe"; "tax 2025"
// finds "...\Tax 2025\report.txt" = D-07's path-only row, still supported).
static bool pathMatchesQuery(const QString &query, const QString &path)
{
    if (query.isEmpty())
        return true;
    return path.toCaseFolded().contains(query.toCaseFolded());
}

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
            // Path-only admission (D-07) is now EARNED, not assumed: the name
            // didn't match, so the path must genuinely contain the query. See
            // pathMatchesQuery() for the full account.
            if (r.score == 0 && !pathMatchesQuery(trimmed, c.path))
                continue;
            const int total = r.score > 0 ? r.score : kPathMatchScore;
            AppEntry e;
            e.source = AppEntry::Source::File;
            e.displayName = disp;
            e.targetPath = c.path;
            e.isFolder = c.isFolder;
            // 2026-10-08: carry the resolved .lnk target through. WITHOUT this
            // the provider fan-out produced rows with an empty launchTarget, so
            // duplicate detection fell back to each row's own path — two
            // identical "Discord" shortcuts could never group and the feature
            // silently reported zero duplicates for every typed query. This is
            // the typed-query path; FileIndex::toEntries is the default-list one,
            // and both must copy the field or the two disagree.
            e.launchTarget = c.launchTarget;
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
            if (r.score == 0 && !pathMatchesQuery(trimmed, ae.targetPath))
                continue;   // 2026-09-15: see pathMatchesQuery() above
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
