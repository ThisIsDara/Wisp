#pragma once
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QString>

#include <list>

// Bounded in-memory LRU icon cache (D-03): holds extracted 64px QImages keyed
// by the FULL provider id (opaque strings from the shell — .lnk iconRefs,
// "uwp:PFN|appId" keys, file paths — treated purely as opaque keys, never
// parsed or used for I/O here). Cap ~500 entries ≈ 8 MB (64×64×4 = 16 KB
// each) — the "no unbounded growth" proof. Eviction re-extracts on demand
// (sub-10ms extraction), so there is no disk persistence: pure in-memory
// utility, no QObject, no signals (plan 05-04 wires the provider to this).
//
// THREADING CONTRACT: called from Qt's single provider thread per engine
// (plan 05-04). The QMutex guards reentrancy and any future concurrent use;
// every public method takes and releases the lock exactly once (non-recursive
// mutex — never call one public method from inside another; LaunchHistory
// WR-01 discipline). Failures are never cached: a miss returns a null QImage
// and null images are ignored on insert, so extraction is retried on demand.
class IconCache
{
public:
    // D-03: default cap 1500 ≈ 24 MB at 64×64×4; configurable for tests
    // (capacity 0 = cache everything off, no growth). Raised for
    // maniac-scroll — keeps every icon from a 1000-file inventory resident.
    explicit IconCache(int capacity = 1500);

    // Cache hit → the stored QImage (implicit sharing → cheap copy), and the
    // key is reordered to MRU. Miss → null QImage (MISS IS NOT CACHED — the
    // caller re-extracts on demand and inserts the result).
    QImage get(const QString &key);

    // Inserts or updates the image for key and marks it MRU. Null images are
    // silently ignored — the failure-not-cached contract (the caller owns
    // extraction-failure handling; a failed extraction is never cached).
    // While the cache exceeds capacity, the oldest (LRU) entries are evicted.
    void insert(const QString &key, const QImage &img);

    // Number of entries currently held (test/cap-assertion helper).
    int size() const;

private:
    // 2026-09-15 perf: O(1) LRU.
    //
    // The previous design kept a QHash<key, QImage> plus a QList<key> recency
    // list, and marked a key MRU with m_order.removeOne(key) — a LINEAR scan
    // costing a string compare per element, over a list capped at 1500, done
    // while holding the mutex. Icon lookups run for every visible row on every
    // repaint (and again on every delegate recycle), so this was the hottest
    // O(n) path in the UI: ~15 rows × up to 1500 string compares, serialized
    // under the lock.
    //
    // std::list gives O(1) splice-to-end and, critically, never invalidates
    // other iterators on insert — so the hash can hold stable node handles.
    // Eviction stays O(1) (pop the front node, drop its hash entry).
    //
    // Eviction ORDER is byte-for-byte the same policy as before (front = LRU,
    // back = MRU, touch promotes to MRU). tst_iconcache pins that behaviour
    // with hitReorders() and oldestEvicted().
    struct Node {
        QString key;
        QImage img;
    };
    using List = std::list<Node>;

    mutable QMutex m_mutex;
    List m_order;                            // front = LRU/oldest, back = MRU
    QHash<QString, List::iterator> m_index;  // key → node handle (stable)
    int m_capacity = 500;
};