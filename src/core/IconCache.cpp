#include "core/IconCache.h"

IconCache::IconCache(const int capacity)
    : m_capacity(capacity)
{
    // Reserve up front so the hot insert path doesn't rehash mid-scroll. Only
    // worth it for a real cap; capacity 0 (tests) keeps the default.
    if (capacity > 0)
        m_index.reserve(capacity);
}

QImage IconCache::get(const QString &key)
{
    // One lock per public method (non-recursive mutex — no nested calls).
    const QMutexLocker locker(&m_mutex);
    const auto it = m_index.find(key);
    if (it == m_index.end())
        return {}; // miss — NOT cached; caller re-extracts on demand
    // Hit: promote to MRU. splice relinks the node in O(1) — no allocation, no
    // scan, and other handles stay valid.
    m_order.splice(m_order.end(), m_order, it.value());
    return it.value()->img; // implicit sharing → cheap copy
}

void IconCache::insert(const QString &key, const QImage &img)
{
    if (img.isNull())
        return; // failure-not-cached contract — caller retries extraction
    const QMutexLocker locker(&m_mutex);
    const auto it = m_index.find(key);
    if (it != m_index.end()) {
        it.value()->img = img;  // update value in place
        m_order.splice(m_order.end(), m_order, it.value());  // reorder to MRU
        return;
    }
    m_index.insert(key, m_order.emplace(m_order.end(), Node{ key, img }));
    // The boundedness proof (D-03): evict the LRU front while over capacity.
    while (static_cast<int>(m_order.size()) > m_capacity) {
        const auto oldest = m_order.begin();
        // Qt 6 QHash::erase takes an iterator, not a key — look the handle up first.
        m_index.erase(m_index.find(oldest->key));  // drop the handle BEFORE the node dies
        m_order.erase(oldest);
    }
}

int IconCache::size() const
{
    const QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_order.size());
}