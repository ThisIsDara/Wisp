#include <QElapsedTimer>
#include <QtTest>

#include "core/FuzzyMatcher.h"

// Pure scoring contract (D-04..D-07): priority ladder exact > prefix >
// word-boundary > subsequence, case-insensitive, camelCase bonuses, and
// exact match ranges returned from day one (the LAUN-06 Phase-5 contract).

namespace {

QString bestName(const QString &query, const QStringList &names)
{
    int bestScore = -1;
    QString best;
    for (const QString &name : names) {
        const int s = FuzzyMatcher::score(query, name).score;
        if (s > bestScore) {
            bestScore = s;
            best = name;
        }
    }
    return best;
}

} // namespace

class TstMatcher : public QObject
{
    Q_OBJECT

private slots:
    void goldenList();
    void ladderTiers();
    void camelCaseBoundary();
    void caseInsensitive();
    void matchRangesExact();
    void noCutoff();
    void tieBreakDeterminism();
    void perfSmoke();
};

void TstMatcher::goldenList()
{
    // D-04 golden-list bar: each query must rank its app first in the fixture.
    const QStringList fixtures = {
        QStringLiteral("Calculator"),
        QStringLiteral("Terminal"),
        QStringLiteral("Notepad"),
        QStringLiteral("Paint"),
        QStringLiteral("Settings"),
        QStringLiteral("Recalibrate"),
    };
    QCOMPARE(bestName(QStringLiteral("cal"), fixtures), QStringLiteral("Calculator"));
    QCOMPARE(bestName(QStringLiteral("term"), fixtures), QStringLiteral("Terminal"));
    QCOMPARE(bestName(QStringLiteral("note"), fixtures), QStringLiteral("Notepad"));
}

void TstMatcher::ladderTiers()
{
    using FuzzyMatcher::score;
    // exact beats prefix beats subsequence
    QVERIFY(score(QStringLiteral("calc"), QStringLiteral("Calculator")).score
            > score(QStringLiteral("cal"), QStringLiteral("Calculator")).score);
    QVERIFY(score(QStringLiteral("cal"), QStringLiteral("Calculator")).score
            > score(QStringLiteral("cal"), QStringLiteral("Recalibrate")).score);
    // name-prefix beats mid-string subsequence (no boundary before "not" in "Knotty")
    QVERIFY(score(QStringLiteral("not"), QStringLiteral("Notepad")).score
            > score(QStringLiteral("not"), QStringLiteral("Knotty")).score);
}

void TstMatcher::camelCaseBoundary()
{
    using FuzzyMatcher::score;
    // P at 4 follows a lowercase letter → camelCase transition = boundary bonus
    QVERIFY(score(QStringLiteral("np"), QStringLiteral("NotePad")).score
            > score(QStringLiteral("np"), QStringLiteral("notepad")).score);
    // space and camelCase are EQUALLY boundaries (D-04: same boundary tier)
    QCOMPARE(score(QStringLiteral("np"), QStringLiteral("Note Pad")).score,
             score(QStringLiteral("np"), QStringLiteral("NotePad")).score);
}

void TstMatcher::caseInsensitive()
{
    using FuzzyMatcher::score;
    QCOMPARE(score(QStringLiteral("CAL"), QStringLiteral("Calculator")).score,
             score(QStringLiteral("cal"), QStringLiteral("Calculator")).score);
}

void TstMatcher::matchRangesExact()
{
    using FuzzyMatcher::MatchRange;
    // prefix → one contiguous run at 0
    const QVector<FuzzyMatcher::MatchRange> expectedPrefix = { {0, 3} };
    QCOMPARE(FuzzyMatcher::score(QStringLiteral("cal"), QStringLiteral("Calculator")).ranges, expectedPrefix);
    // two runs: contiguous matched chars merge, gaps split
    const QVector<FuzzyMatcher::MatchRange> expectedTwoRuns = { {0, 1}, {4, 1} };
    QCOMPARE(FuzzyMatcher::score(QStringLiteral("np"), QStringLiteral("NotePad")).ranges, expectedTwoRuns);
    // ranges always stay inside [0, name.length())
    const QStringList names = { QStringLiteral("Recalibrate"), QStringLiteral("Knotty"),
                                QStringLiteral("Squid"), QStringLiteral("Note Pad") };
    const QStringList queries = { QStringLiteral("cal"), QStringLiteral("not"),
                                  QStringLiteral("q"), QStringLiteral("np") };
    for (const QString &q : queries) {
        for (const QString &n : names) {
            const auto ranges = FuzzyMatcher::score(q, n).ranges;
            for (const FuzzyMatcher::MatchRange &r : ranges) {
                QVERIFY2(r.start >= 0, "range start below 0");
                QVERIFY2(r.length > 0, "range length must be positive");
                QVERIFY2(r.start + r.length <= n.length(),
                         qPrintable(QStringLiteral("range [%1,+%2) escapes name \"%3\" (length %4)")
                                        .arg(r.start).arg(r.length).arg(n).arg(n.length())));
            }
        }
    }
}

void TstMatcher::noCutoff()
{
    using FuzzyMatcher::score;
    // D-06: any 1-char subsequence scores — no cutoff
    QVERIFY(score(QStringLiteral("q"), QStringLiteral("Squid")).score > 0);
    // genuine no-match → score 0 with empty ranges
    QCOMPARE(score(QStringLiteral("x"), QStringLiteral("NothingHere")).score, 0);
    QVERIFY(score(QStringLiteral("x"), QStringLiteral("NothingHere")).ranges.isEmpty());
    // empty query → exactly {0, {}}
    QCOMPARE(score(QStringLiteral(""), QStringLiteral("Anything")).score, 0);
    QVERIFY(score(QStringLiteral(""), QStringLiteral("Anything")).ranges.isEmpty());
}

void TstMatcher::tieBreakDeterminism()
{
    using FuzzyMatcher::score;
    // Equal-score names: the matcher returns EQUAL scores for equal patterns;
    // alphabetical ordering of ties is the MODEL's job (asserted in tst_model).
    QCOMPARE(score(QStringLiteral("cal"), QStringLiteral("Calc")).score,
             score(QStringLiteral("cal"), QStringLiteral("Calculator")).score);
}

void TstMatcher::perfSmoke()
{
    // D-06 UI-thread budget: scoring 500 fixture names for a 4-char query
    // must stay well under 5ms (linear scan only — no allocation per char).
    QStringList names;
    const QStringList prefixes = { QStringLiteral("Alpha"), QStringLiteral("Beta"),
                                   QStringLiteral("Gamma"), QStringLiteral("Delta"),
                                   QStringLiteral("Epsilon"), QStringLiteral("Zeta"),
                                   QStringLiteral("Theta"), QStringLiteral("Kappa"),
                                   QStringLiteral("Lambda"), QStringLiteral("Sigma"),
                                   QStringLiteral("Omega") };
    for (int i = 0; i < 500; ++i)
        names.append(prefixes.at(i % prefixes.size()) + QStringLiteral("Tool") + QString::number(i));

    QElapsedTimer timer;
    timer.start();
    for (const QString &n : names)
        FuzzyMatcher::score(QStringLiteral("atoo"), n);
    const qint64 elapsedUs = timer.nsecsElapsed() / 1000;
    QVERIFY2(elapsedUs < 5000,
             qPrintable(QStringLiteral("scored 500 names in %1 µs — over the 5ms budget").arg(elapsedUs)));
}

QTEST_MAIN(TstMatcher)
#include "tst_matcher.moc"