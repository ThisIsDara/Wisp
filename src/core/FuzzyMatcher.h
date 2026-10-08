#pragma once
#include <QString>
#include <QVector>

namespace FuzzyMatcher {

struct MatchRange { int start; int length; };   // contiguous run of matched chars

inline bool operator==(const MatchRange &a, const MatchRange &b)
{
    return a.start == b.start && a.length == b.length;
}

struct Result { int score = 0; QVector<MatchRange> ranges; };   // default = no-match {0, {}}

// Empty query → { score: 0, ranges: {} }. Case-insensitive. Ladder:
// exact > name prefix > word-boundary start > any subsequence; camelCase
// bonus (uppercase following lowercase = boundary). No cutoff (D-06):
// any subsequence match scores > 0.
Result score(const QString &query, const QString &displayName);

// 2026-09-15 perf: the same scoring ladder as score(), driven by caches the
// caller precomputes ONCE per entry instead of re-lowercasing the name and
// recomputing word boundaries on every keystroke.
//
//   targetLower   — displayName.toLower(), computed when the catalog is built
//   boundaries    — one flag per char of targetLower: true at index 0, after
//                   space/-/_//. , and where a lowercase char is followed by an
//                   uppercase one (camelCase)
//
// `queryLower` must already be query.toLower(). Results are IDENTICAL to
// score() for the same inputs — this is a caching strategy, not a different
// ranking — so both call sites must stay interchangeable.
Result scoreFast(const QString &queryLower, const QString &targetLower,
                 const QVector<char> &boundaries);

// 2026-09-15 perf: build the two caches scoreFast() needs from a displayName.
// Computed once per entry at catalog build time.
void buildCaches(const QString &displayName, QString *outLower,
                 QVector<char> *outBoundaries);

} // namespace FuzzyMatcher
