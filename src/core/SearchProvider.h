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
};

class SearchProvider
{
public:
    virtual ~SearchProvider() = default;
    // limit = max results to return, exact = true means prefix/exact only (for 1-2 char tier gate)
    virtual QVector<ScoredEntry> query(const QString &q, int limit, bool exact) = 0;
};
