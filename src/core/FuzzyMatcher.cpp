#include "core/FuzzyMatcher.h"

namespace {

// Ladder tiers (D-04): binary-threshold categories so that
// EXACT > PREFIX > BOUNDARY > SUBSEQUENCE always hold. Bonuses are capped
// below the tier gap so no bonus combination can ever flip a tier.
constexpr int kTierExact = 1000;
constexpr int kTierPrefix = 800;
constexpr int kTierBoundary = 600;
constexpr int kTierSubsequence = 400;
constexpr int kTierGap = 200;
constexpr int kPerCharBonus = 1;
constexpr int kPerBoundaryBonus = 2;
constexpr int kSingleRunBonus = 1;

// Locale-independent case folding, per char (avoids the multi-char fold
// position-mapping problem of QString::toCaseFolded on whole strings).
bool charsEqual(QChar a, QChar b)
{
    return a.toCaseFolded() == b.toCaseFolded();
}

// Word boundaries: start of string, after space/separator (-_/.), and
// camelCase transition (uppercase following lowercase).
bool isBoundaryAt(const QString &name, int i)
{
    if (i == 0)
        return true;
    const QChar prev = name.at(i - 1);
    if (prev.isSpace() || prev == QLatin1Char('-') || prev == QLatin1Char('_')
        || prev == QLatin1Char('/') || prev == QLatin1Char('.'))
        return true;
    return prev.isLower() && name.at(i).isUpper();
}

int clampBonus(int raw)
{
    return qMin(raw, kTierGap - 1);
}

} // namespace

namespace FuzzyMatcher {

Result score(const QString &query, const QString &displayName)
{
    Result none; // { score: 0, ranges: {} }
    const int qLen = query.size();
    const int nLen = displayName.size();
    if (qLen == 0 || qLen > nLen)
        return none;

    // Case-insensitive prefix check — covers both the exact tier (query
    // spans the whole name) and the prefix tier with one deterministic scan.
    bool startsWith = true;
    for (int i = 0; i < qLen; ++i) {
        if (!charsEqual(query.at(i), displayName.at(i))) {
            startsWith = false;
            break;
        }
    }
    if (startsWith) {
        int boundaries = 0;
        for (int i = 0; i < qLen; ++i)
            if (isBoundaryAt(displayName, i))
                ++boundaries;
        Result r;
        r.score = (qLen == nLen ? kTierExact : kTierPrefix)
                  + clampBonus(qLen * kPerCharBonus + boundaries * kPerBoundaryBonus + kSingleRunBonus);
        r.ranges = { { 0, qLen } };
        return r;
    }

    // General subsequence scan. The FIRST matched char is tier-determining,
    // so prefer a word-boundary occurrence for it; remaining chars are greedy
    // leftmost (keeps runs compact and contiguous). Pure linear scan — no
    // regex, no recursion, no per-char allocation (T-03-01-01).
    int first = -1;
    for (int j = 0; j < nLen; ++j) {
        if (charsEqual(query.at(0), displayName.at(j)) && isBoundaryAt(displayName, j)) {
            first = j;
            break;
        }
    }
    if (first < 0) {
        for (int j = 0; j < nLen; ++j) {
            if (charsEqual(query.at(0), displayName.at(j))) {
                first = j;
                break;
            }
        }
    }
    if (first < 0)
        return none;

    QVector<int> positions;
    positions.append(first);
    int prev = first;
    for (int i = 1; i < qLen; ++i) {
        int j = prev + 1;
        while (j < nLen && !charsEqual(query.at(i), displayName.at(j)))
            ++j;
        if (j >= nLen)
            return none;
        positions.append(j);
        prev = j;
    }

    Result r;
    int boundaries = 0;
    for (int pos : positions)
        if (isBoundaryAt(displayName, pos))
            ++boundaries;

    // Merge contiguous matched chars into runs (gaps split ranges).
    int runStart = positions.first();
    int runEnd = runStart;
    int runs = 1;
    for (int i = 1; i < positions.size(); ++i) {
        if (positions.at(i) == runEnd + 1) {
            runEnd = positions.at(i);
            continue;
        }
        r.ranges.append({ runStart, runEnd - runStart + 1 });
        runStart = positions.at(i);
        runEnd = runStart;
        ++runs;
    }
    r.ranges.append({ runStart, runEnd - runStart + 1 });

    const int tier = isBoundaryAt(displayName, positions.first()) ? kTierBoundary : kTierSubsequence;
    r.score = tier + clampBonus(positions.size() * kPerCharBonus + boundaries * kPerBoundaryBonus
                                + (runs == 1 ? kSingleRunBonus : 0));
    return r;
}

} // namespace FuzzyMatcher