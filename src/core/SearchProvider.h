#pragma once
#include <QVector>
#include "core/AppEntry.h"
#include "core/FuzzyMatcher.h"

// Shared scoring-tier contract (D-07): every match tier below this is a
// path-only fallback — ranks below ALL name matches across every provider.
inline constexpr int kPathMatchScore = 100;

struct ScoredEntry {
    AppEntry entry;
    FuzzyMatcher::Result match;
    int totalScore = 0;
    // 2026-09-15 perf: derived keys a provider computes ONCE while it still
    // has the entry in hand, so the provider's sort and favorites filter stop
    // rebuilding them per comparison / per row. Both are pure caches of
    // `entry` — providers that don't set them leave them empty, which only
    // costs the (still-correct) fallback at the read site.
    QString foldName;  // entry.displayName.toCaseFolded() — alphabetical tie-break
    QString foldId;    // targetPath (or aumid when empty) — favorites membership
};

class SearchProvider
{
public:
    virtual ~SearchProvider() = default;
    // limit = max results to return, exact = true means prefix/exact only (for 1-2 char tier gate)
    virtual QVector<ScoredEntry> query(const QString &q, int limit, bool exact) = 0;
};
