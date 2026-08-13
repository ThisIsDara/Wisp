#pragma once

#include <QImage>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

#include <functional>

class IconCache;

// Serves image://wispicons/{id} to QML (D-02). get-or-extract against the
// bounded LRU (IconCache, plan 05-02): cache hits are returned directly; a
// miss runs the injected extraction seam (bound to WinIconExtractor::extract
// in main.cpp) and only SUCCESSES are cached (D-16 — failures are never
// cached; the QML monogram stays and re-extraction happens on demand).
// The std::function seam keeps Win32 out of this file (the src/win firewall
// discipline) — pure Qt + injected seams.
//
// THREADING: provider methods run on Qt's dedicated provider thread per
// engine — blocking extraction is safe here and never blocks the UI thread
// (05-RESEARCH Pattern 1). QML MUST render with Image { cache: false } so
// QPixmapCache never defeats the LRU (plan 05-05 consumer contract).
class IconProvider : public QQuickImageProvider
{
public:
    explicit IconProvider(IconCache *cache,
                          std::function<QImage(const QString &)> extractor);

    QImage requestImage(const QString &id, QSize *size,
                        const QSize &requestedSize) override;

private:
    IconCache *m_cache;                                  // owned by main.cpp — non-owning
    std::function<QImage(const QString &)> m_extractor;  // seam: WinIconExtractor::extract
};