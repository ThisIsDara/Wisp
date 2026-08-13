#include "core/IconCache.h"

IconCache::IconCache(const int capacity)
    : m_capacity(capacity)
{
}

QImage IconCache::get(const QString &key)
{
    // One lock per public method (non-recursive mutex — no nested calls).
    const QMutexLocker locker(&m_mutex);
    const auto it = m_map.constFind(key);
    if (it == m_map.cend())
        return {}; // miss — NOT cached; caller re-extracts on demand
    // Hit: reorder to MRU (back of m_order). removeOne + append is O(n) at
    // n ≤ 500 — fine; keeps a single canonical order list.
    m_order.removeOne(key);
    m_order.append(key);
    return it.value(); // implicit sharing → cheap copy
}

void IconCache::insert(const QString &key, const QImage &img)
{
    if (img.isNull())
        return; // failure-not-cached contract — caller retries extraction
    const QMutexLocker locker(&m_mutex);
    if (m_map.contains(key)) {
        m_map.insert(key, img); // update value in place
        m_order.removeOne(key);
        m_order.append(key); // reorder to MRU
        return;
    }
    m_map.insert(key, img);
    m_order.append(key);
    // The boundedness proof (D-03): evict the LRU front while over capacity.
    while (m_order.size() > m_capacity) {
        const QString oldest = m_order.first();
        m_order.removeFirst();
        m_map.remove(oldest);
    }
}

int IconCache::size() const
{
    const QMutexLocker locker(&m_mutex);
    return m_order.size();
}
