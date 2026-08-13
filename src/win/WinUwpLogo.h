#pragma once

#include <QImage>
#include <QString>

#include <iterator> // std::size

// UWP store-app logo resolver firewall (C++/WinRT) — the pure scale-variant
// helper lives INLINE here so tst_icons can unit-test it without touching
// WinRT objects (Shared Pattern 2). Live WinRT detail is in the .cpp.
namespace WinUwpLogo {

// Scale-variant pick (criterion 4: "scale variants"): candidates
// {100,125,150,200,300,400}; target = qRound(dpr*100); return the FIRST
// candidate >= target, else "400" (ceil rule, ties upward). The variant
// suffix ".scale-{N}" is inserted before the LAST dot of logoBasePath:
//   "C:\pkg\Assets\AppIcon.png" + dpr 1.5 → "C:\pkg\Assets\AppIcon.scale-150.png"
// Pure string logic — no filesystem access.
inline QString scaleVariantFor(const QString &logoBasePath, qreal devicePixelRatio)
{
    static constexpr int kCandidates[] = { 100, 125, 150, 200, 300, 400 };
    const int target = qRound(devicePixelRatio * 100);
    int variant = kCandidates[std::size(kCandidates) - 1]; // ceil rule default: 400
    for (const int candidate : kCandidates) {
        if (candidate >= target) {
            variant = candidate;
            break;
        }
    }
    const qsizetype dot = logoBasePath.lastIndexOf(u'.');
    const QString suffix = QStringLiteral(".scale-") + QString::number(variant);
    if (dot <= 0) // no dot (or dot at position 0) — append
        return logoBasePath + suffix;
    return logoBasePath.left(dot) + suffix + logoBasePath.mid(dot);
}

// Live WinRT resolution (defined in WinUwpLogo.cpp): AppxManifest.xml
// Square44x44Logo indirect string → SHLoadIndirectString → scale-variant
// probe → DisplayInfo.GetLogo({64,64}) stream fallback. Called from Qt's
// provider thread (MTA apartment initialized per call). ALL-FAIL contract:
// silent null QImage (D-16) — never a crash, never a toast, no throw across
// the seam (T-05-03: malformed XML → null QImage).
QImage extractLogo(const QString &packageFullName, const QString &appId,
                   qreal devicePixelRatio);

} // namespace WinUwpLogo
