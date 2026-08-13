#include <QtTest>

#include <QtConcurrent>
#include <QImage>
#include <QRandomGenerator>
#include <QThread>

#include <atomic>

#include "core/IconCache.h"

// IconCache contract (D-03): bounded in-memory LRU for extracted 64px
// QImages — cap honored (the "no unbounded growth" proof), oldest evicted
// first, get() reorders to MRU, misses return null and are never cached,
// null images are ignored on insert, and the QMutex guard survives a
// concurrent insert/get race (WR-01 QtConcurrent smoke shape copied from
// tst_history.cpp:173-210).

namespace {

QImage iconFor(const int tag)
{
    // Cheap distinct 8x8 ARGB32 payload per key (64px production images are
    // 64x64 — the cache is size-agnostic; only the key and cap matter here).
    QImage img(8, 8, QImage::Format_ARGB32);
    img.fill(QColor(tag % 255, 16, 32));
    return img;
}

} // namespace

class TstIconCache : public QObject
{
    Q_OBJECT

private slots:
    void capHonored();
    void oldestEvicted();
    void hitReorders();
    void missReturnsNull();
    void insertNullIgnored();
    void threadSafeInsertGet();
};

void TstIconCache::capHonored()
{
    // D-03 boundedness proof: 501 distinct inserts into a 500-cap cache
    // never grow beyond the cap.
    IconCache cache(500);
    for (int i = 0; i < 501; ++i)
        cache.insert(QStringLiteral("k%1").arg(i), iconFor(i));
    QCOMPARE(cache.size(), 500);
}

void TstIconCache::oldestEvicted()
{
    // Insertion order IS eviction order when nothing is touched: "a" (oldest)
    // must go when "c" arrives at cap 2.
    IconCache cache(2);
    cache.insert(QStringLiteral("a"), iconFor(0));
    cache.insert(QStringLiteral("b"), iconFor(1));
    cache.insert(QStringLiteral("c"), iconFor(2));
    QVERIFY(cache.get(QStringLiteral("a")).isNull()); // evicted
    QVERIFY(!cache.get(QStringLiteral("b")).isNull());
    QVERIFY(!cache.get(QStringLiteral("c")).isNull());
    QCOMPARE(cache.size(), 2);
}

void TstIconCache::hitReorders()
{
    // A get() touch promotes the key to MRU: after touching "a", "b" becomes
    // the oldest and is evicted when "c" arrives — proves hits re-order.
    IconCache cache(2);
    cache.insert(QStringLiteral("a"), iconFor(0));
    cache.insert(QStringLiteral("b"), iconFor(1));
    QVERIFY(!cache.get(QStringLiteral("a")).isNull()); // touch → MRU
    cache.insert(QStringLiteral("c"), iconFor(2));
    QVERIFY(cache.get(QStringLiteral("b")).isNull()); // b was oldest → evicted
    QVERIFY(!cache.get(QStringLiteral("a")).isNull());
    QCOMPARE(cache.size(), 2);
}

void TstIconCache::missReturnsNull()
{
    // A miss returns a null QImage and is never cached (no phantom entry).
    IconCache cache(2);
    QVERIFY(cache.get(QStringLiteral("nope")).isNull());
    QCOMPARE(cache.size(), 0);
}

void TstIconCache::insertNullIgnored()
{
    // The failure-not-cached contract: null images are silently ignored, so
    // a failed extraction can never poison the cache.
    IconCache cache(2);
    cache.insert(QStringLiteral("x"), QImage());
    QCOMPARE(cache.size(), 0);
    QVERIFY(cache.get(QStringLiteral("x")).isNull());
}

void TstIconCache::threadSafeInsertGet()
{
    // QMutex smoke (tst_history.cpp:173-210 shape): a writer inserts N
    // distinct keys while readers randomly get from the same range — no torn
    // state, no crash, every insert survives.
    constexpr int kCap = 500;
    constexpr int kKeys = 500;
    IconCache cache(kCap);

    std::atomic<bool> start{ false };
    const auto writer = [&] {
        while (!start.load())
            QThread::yieldCurrentThread();
        for (int i = 0; i < kKeys; ++i)
            cache.insert(QStringLiteral("k%1").arg(i), iconFor(i));
    };
    const auto reader = [&] {
        while (!start.load())
            QThread::yieldCurrentThread();
        for (int i = 0; i < 250; ++i) {
            const int idx = int(QRandomGenerator::global()->bounded(kKeys));
            cache.get(QStringLiteral("k%1").arg(idx));
        }
    };
    QFuture<void> w = QtConcurrent::run(writer);
    QFuture<void> r = QtConcurrent::run(reader);
    start = true;
    w.waitForFinished();
    r.waitForFinished();

    // kKeys == kCap → no eviction could have occurred; every insert survived
    // the interleaving.
    QCOMPARE(cache.size(), kKeys);
}

QTEST_MAIN(TstIconCache)
#include "tst_iconcache.moc"
