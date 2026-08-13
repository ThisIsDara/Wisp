#include "core/IconProvider.h"

#include "core/IconCache.h"

#include <QUrl>

IconProvider::IconProvider(IconCache *cache,
                           std::function<QImage(const QString &)> extractor)
    : QQuickImageProvider(QQuickImageProvider::Image) // requestImage() -> QImage
    , m_cache(cache)
    , m_extractor(std::move(extractor))
{
}

QImage IconProvider::requestImage(const QString &id, QSize *size,
                                  const QSize &requestedSize)
{
    // T-05-19 (2026-08-10 fix): the engine hands the provider the id STILL
    // percent-encoded — QML's encodeURIComponent round-trip leaves %3A/%5C/
    // %3B untouched (only %20 decodes on the way in, verified in the field).
    // Undo the encodeURIComponent pass here so parseKey sees the real
    // 'path;index' / 'path:path' / 'uwp:PFN|appId' grammar — otherwise
    // SHCreateItemFromParsingName/ExtractIconExW get a path that does not
    // exist and every icon silently falls back to the monogram. Decoding is
    // also idempotent for already-plain segments (a literal space has no '%').
    const QString decoded = QUrl::fromPercentEncoding(id.toUtf8());

    // LRU-first (plan 05-02 contract): a hit skips extraction entirely. On a
    // miss, extract via the injected seam — on THIS provider thread, where
    // blocking extraction is safe (05-RESEARCH Pattern 1). Only successes are
    // inserted: failures return null and are never cached (D-16 — the QML
    // monogram stays, and the row re-extracts the next time it's requested).
    QImage img = m_cache->get(decoded);
    if (img.isNull()) {
        img = m_extractor(decoded);
        if (!img.isNull())
            m_cache->insert(decoded, img);
    }

    if (img.isNull()) {
        // Monogram fallback path (D-04/D-16): null image, 32x32 slot. The QML
        // row keeps its placeholder — no crash, no toast.
        if (size)
            *size = QSize(32, 32);
        return {};
    }

    // D-01: 32px render target. Extraction produces 64px (D-03) — downscale
    // smoothly to the requested size when the engine supplies one, else the
    // fixed 32x32 default.
    const QSize target = requestedSize.isValid() ? requestedSize : QSize(32, 32);
    img = img.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size)
        *size = img.size();
    return img;
}