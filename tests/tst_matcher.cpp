#include <QElapsedTimer>
#include <QtTest>

#include <cmath>

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
    void scoreFastParity();
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

// 2026-09-15: scoreFast() is the cache-driven twin of score(), and BOTH are on
// the live query path (ResultsModel + AppProvider rank through them). Any
// divergence would silently change user-visible ranking, so assert byte-identical
// scores AND match ranges across a structured corpus (camelCase, separators,
// non-ASCII, trailing space) plus an exhaustive sweep of short queries.
void TstMatcher::scoreFastParity()
{
    const QStringList names = {
        QStringLiteral("Steam"),
        QStringLiteral("Steam Helper"),
        QStringLiteral("steam"),
        QStringLiteral("SteamWorks"),          // camelCase mid-name boundary
        QStringLiteral("my-steam_tool"),       // separator boundaries
        QStringLiteral("MS Visual Studio"),    // space boundary
        QStringLiteral("a"), QStringLiteral("ab"),
        QStringLiteral("A B C"),
        QStringLiteral("Adobe Photoshop"),     // two capital starts
        QStringLiteral("STRASSE"),             // uppercase run, no camel boundary
        QStringLiteral("café_runner"),         // non-ASCII
        QStringLiteral("Ünïcödé App"),         // non-ASCII leading
        QStringLiteral("tab\tsep"),
        QStringLiteral("trailing "),
        QStringLiteral(""),
        QStringLiteral("x/y.z_w-q"),           // every separator char
    };
    const QStringList queries = {
        QStringLiteral(""), QStringLiteral("a"), QStringLiteral("s"),
        QStringLiteral("st"), QStringLiteral("steam"), QStringLiteral("Steam"),
        QStringLiteral("sh"), QStringLiteral("mvs"), QStringLiteral("mvs v"),
        QStringLiteral("ad"), QStringLiteral("aps"), QStringLiteral("adps"),
        QStringLiteral("caf"), QStringLiteral("café"), QStringLiteral("ün"),
        QStringLiteral("xyz"), QStringLiteral("x"), QStringLiteral("zzzz"),
        QStringLiteral("tab"), QStringLiteral("strasse"), QStringLiteral("stea"),
    };

    for (const QString &name : names) {
        QString lower;
        QVector<char> bounds;
        FuzzyMatcher::buildCaches(name, &lower, &bounds);
        for (const QString &q : queries) {
            const FuzzyMatcher::Result slow = FuzzyMatcher::score(q, name);
            const FuzzyMatcher::Result fast = FuzzyMatcher::scoreFast(q.toLower(), lower, bounds);
            if (slow.score != fast.score)
                QFAIL(qPrintable(QStringLiteral("score mismatch q='%1' name='%2': %3 vs %4")
                                     .arg(q, name).arg(slow.score).arg(fast.score)));
            if (slow.ranges != fast.ranges)
                QFAIL(qPrintable(QStringLiteral("range mismatch q='%1' name='%2'").arg(q, name)));
        }
    }

    // Exhaustive short-query sweep so a tier/boundary edge case in generated
    // combinations can't slip past the hand-picked corpus.
    const QString alphabet = QStringLiteral("abcs ");
    const QStringList micro = { QStringLiteral("a"), QStringLiteral("ab"), QStringLiteral("abc"),
                                QStringLiteral("aB"), QStringLiteral("A b"), QStringLiteral("a-b"),
                                QStringLiteral("As"), QStringLiteral("aS") };
    for (const QString &name : micro) {
        QString lower;
        QVector<char> bounds;
        FuzzyMatcher::buildCaches(name, &lower, &bounds);
        for (int len = 1; len <= 3; ++len) {
            const int total = int(std::pow(alphabet.size(), len));
            for (int code = 0; code < total; ++code) {
                QString q;
                int c = code;
                for (int k = 0; k < len; ++k) {
                    q.append(alphabet.at(c % alphabet.size()));
                    c /= alphabet.size();
                }
                const FuzzyMatcher::Result slow = FuzzyMatcher::score(q, name);
                const FuzzyMatcher::Result fast = FuzzyMatcher::scoreFast(q.toLower(), lower, bounds);
                if (slow.score != fast.score || slow.ranges != fast.ranges)
                    QFAIL(qPrintable(QStringLiteral("sweep mismatch q='%1' name='%2'").arg(q, name)));
            }
        }
    }
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
