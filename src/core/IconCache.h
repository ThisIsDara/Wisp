#pragma once
#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QString>

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
    // m_order front = LRU/oldest, back = MRU/newest. get()/insert() move the
    // touched key to the back; insert() evicts from the front while over cap.
    mutable QMutex m_mutex;
    QHash<QString, QImage> m_map;
    QList<QString> m_order;
    int m_capacity = 500;
};
