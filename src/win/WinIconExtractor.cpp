#include "win/WinIconExtractor.h"

#include "win/WinUwpLogo.h"

#include <QDebug>
#include <QGuiApplication> // qApp->devicePixelRatio()
#include <QImage>          // QImage::fromHICON (Qt 6 replacement for QPixmap::fromWinHICON)

#include <windows.h>
#include <objbase.h>          // CoInitializeEx / CoUninitialize
#include <shellapi.h>         // ExtractIconExW
#include <shobjidl_core.h>    // SHCreateItemFromParsingName, IShellItemImageFactory, SIIGBF_*

namespace {

// Minimal COM ownership guard (RAII shape from the deleted Phase-04
// search-query precedent). The icon
// chain spans up to 3 interfaces with a failure exit at every step; RAII is
// the only release discipline that guarantees "release on every path".
template <typename T>
class ComPtr
{
public:
    ComPtr() = default;
    ~ComPtr() { release(); }
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;

    T **put() noexcept { return &m_ptr; }
    T *get() const noexcept { return m_ptr; }
    T *operator->() const noexcept { return m_ptr; }
    explicit operator bool() const noexcept { return m_ptr != nullptr; }

private:
    void release() noexcept
    {
        if (m_ptr) {
            m_ptr->Release();
            m_ptr = nullptr;
        }
    }
    T *m_ptr = nullptr;
};

} // namespace

// HBITMAP → QImage with EXPLICIT top-down row order (fix 2026-08-10: icons
// rendered upside down). QImage::fromHBITMAP's orientation handling did not
// match what IShellItemImageFactory::GetImage ships on this Qt build, so the
// pixels came out bottom-up. This helper never guesses: it asks GDI for the
// bits through a NEGATIVE biHeight (top-down request) — GetDIBits converts
// from whatever orientation the DIB natively has — and the 32bpp BGRA rows
// GDI writes are byte-for-byte Format_ARGB32 (little-endian 0xAARRGGBB:
// byte0=B, byte1=G, byte2=R, byte3=A) — NO channel swizzle, ever (a
// qRgba(B,G,R,...) pass here swapped red↔blue and produced wrong colors,
// observed 2026-08-10). Only premultiplication remains (icon DIB alpha is
// straight; premultiplied is what the scene graph expects).
QImage imageFromHBitmap(HBITMAP hbm)
{
    DIBSECTION ds = {};
    if (GetObject(hbm, sizeof(ds), &ds) != sizeof(ds)
        || ds.dsBm.bmBitsPixel != 32 || ds.dsBm.bmWidth <= 0 || ds.dsBm.bmHeight <= 0)
        return QImage::fromHBITMAP(hbm).copy(); // rare non-DIB fallback — old behavior
    const int w = ds.dsBm.bmWidth;
    const int h = ds.dsBm.bmHeight;
    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(bih);
    bih.biWidth = w;
    bih.biHeight = -h; // negative = ask for TOP-DOWN rows (GDI converts)
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;
    QImage raw(w, h, QImage::Format_ARGB32);
    if (raw.isNull())
        return {};
    const HDC hdc = GetDC(nullptr);
    const int lines = hdc
        ? GetDIBits(hdc, hbm, 0, h, raw.bits(),
                    reinterpret_cast<BITMAPINFO *>(&bih), DIB_RGB_COLORS)
        : 0;
    if (hdc)
        ReleaseDC(nullptr, hdc);
    if (lines != h)
        return QImage::fromHBITMAP(hbm).copy();
    return raw.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

namespace WinIconExtractor {

QImage extract(const QString &id)
{
    const IconKey key = parseKey(id);
    if (!key.isValid()) {
        // Per-item qWarning, never abort (WinStartMenuEnumerator.cpp:128-136
        // precedent); the caller keeps the monogram placeholder (D-16).
        qWarning() << "WinIconExtractor: invalid icon key" << id;
        return QImage();
    }

    // COM apartment per call, verbatim WinStartMenuEnumerator.cpp:110-115:
    // reuse an existing apartment (S_FALSE; RPC_E_CHANGED_MODE = different
    // mode — fine to continue), take ownership only when WE initialize.
    // MTA init also initializes the WinRT apartment for the UWP route.
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        qWarning() << "WinIconExtractor: CoInitializeEx failed" << initHr;
        return QImage();
    }
    const bool weInitialized = (initHr == S_OK);

    QImage result;

    if (key.kind == Kind::Uwp) {
        // UWP route — defined in WinUwpLogo.cpp (WinRT seam).
        result = WinUwpLogo::extractLogo(key.packageFullName, key.appId,
                                         qApp->devicePixelRatio());
    } else if (key.index > 0) {
        // Multi-icon .exe/.dll — IShellItemImageFactory has no index concept
        // (RESEARCH Pattern 2 step 3). Legacy but correct; rare case (most
        // .lnk icons are index 0).
        HICON large = nullptr;
        HICON smallIcon = nullptr; // NOTE: "small" collides with a windows.h macro
        const UINT count = ExtractIconExW(key.path.toStdWString().c_str(),
                                          key.index, &large, &smallIcon, 1);
        const HICON hicon = large ? large : smallIcon;
        if (count == 0 || hicon == nullptr) {
            qWarning() << "WinIconExtractor: ExtractIconExW failed for"
                       << key.path << key.index;
        } else {
            result = QImage::fromHICON(hicon);
            if (result.size() != QSize(64, 64)) // result stays 64px (D-03)
                result = result.scaled(64, 64, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
        }
        if (large)
            DestroyIcon(large);
        if (smallIcon)
            DestroyIcon(smallIcon);
    } else {
        // STACK icon map (RESEARCH lines 414-424 verbatim):
        // SHCreateItemFromParsingName → IShellItemImageFactory::GetImage.
        ComPtr<IShellItem> item;
        HRESULT hr = SHCreateItemFromParsingName(key.path.toStdWString().c_str(),
                                                 nullptr, IID_PPV_ARGS(item.put()));
        ComPtr<IShellItemImageFactory> factory;
        if (SUCCEEDED(hr))
            hr = item->QueryInterface(IID_PPV_ARGS(factory.put()));
        HBITMAP hbm = nullptr;
        if (SUCCEEDED(hr))
            hr = factory->GetImage({ 64, 64 },
                                   SIIGBF_ICONONLY | SIIGBF_RESIZETOFIT
                                       | SIIGBF_SCALEUP,
                                   &hbm);
        if (FAILED(hr)) {
            qWarning() << "WinIconExtractor: GetImage failed" << hr << key.path;
        } else {
            // Explicit top-down copy (imageFromHBitmap) — .copy()-free:
            // GetDIBits copies into our own buffer, so DeleteObject after
            // conversion is always safe.
            result = imageFromHBitmap(hbm);
            DeleteObject(hbm);
        }
    }

    if (weInitialized)
        CoUninitialize();
    return result;
}

} // namespace WinIconExtractor
