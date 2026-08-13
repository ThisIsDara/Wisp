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

} // namespace FuzzyMatcher
