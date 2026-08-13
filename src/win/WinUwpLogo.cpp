#include "win/WinUwpLogo.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QXmlStreamReader>

#include <vector>

#include <windows.h>
#include <objbase.h>  // CoInitializeEx / CoUninitialize
#include <shlwapi.h>  // SHLoadIndirectString (shlwapi.lib)

#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.Storage.Streams.h>

namespace {

// winrt::hstring has no implicit std::wstring conversion — copy via
// fromWCharArray (length-aware) instead (WinUwpEnumerator.cpp:13-17 verbatim).
QString toQString(const winrt::hstring &s)
{
    return QString::fromWCharArray(s.c_str(), int(s.size()));
}

// Resolve an indirect manifest value to a concrete absolute file path via
// SHLoadIndirectString (criterion 4 "indirect strings"; PowerToys-proven
// "@{PackageFullName?ms-resource:...}" form). Accepts either the wrapped
// "@{...}" form or a bare "ms-resource:..." value; the PackageFullName is
// substituted when missing. Empty QString on failure — caller falls through
// to the GetLogo fallback (D-16 silent).
QString resolveIndirectString(const QString &raw, const QString &packageFullName)
{
    QString inner = raw;
    if (inner.startsWith(QLatin1String("@{")) && inner.endsWith(u'}'))
        inner = inner.mid(2, inner.size() - 3);
    if (!inner.startsWith(packageFullName + u'?'))
        inner = packageFullName + u'?' + inner;
    const std::wstring wrapped = L"@{" + inner.toStdWString() + L"}";

    wchar_t out[2048] = {};
    const HRESULT hr =
        SHLoadIndirectString(wrapped.c_str(), out, static_cast<UINT>(std::size(out)), nullptr);
    if (FAILED(hr)) {
        qWarning() << "WinUwpLogo: SHLoadIndirectString failed" << hr << raw;
        return {};
    }
    return QString::fromWCharArray(out);
}

// Find the Square44x44Logo base asset for the Application whose Id attribute
// equals appId. Namespace-tolerant (QXmlStreamReader local-name matching —
// default-ns / uap-prefixed forms both work; no external entities are
// resolved, T-05-03). Empty on missing app / missing attribute / parse error.
QString findLogoBaseFromManifest(QFile &manifest, const QString &appId)
{
    QXmlStreamReader xml(&manifest);
    bool inTargetApp = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;
        if (xml.name() == QLatin1String("Application")) {
            bool idMatches = false;
            for (const QXmlStreamAttribute &attr : xml.attributes()) {
                if (attr.name() == QLatin1String("Id") && attr.value() == appId) {
                    idMatches = true;
                    break;
                }
            }
            inTargetApp = idMatches;
        } else if (inTargetApp && xml.name() == QLatin1String("VisualElements")) {
            for (const QXmlStreamAttribute &attr : xml.attributes()) {
                if (attr.name() == QLatin1String("Square44x44Logo"))
                    return attr.value().toString();
            }
            qWarning() << "WinUwpLogo: VisualElements without Square44x44Logo"
                       << appId;
            return {};
        }
    }
    if (xml.hasError())
        qWarning() << "WinUwpLogo: AppxManifest parse error" << xml.errorString();
    else
        qWarning() << "WinUwpLogo: no matching Application (Id)"
                   << appId << "in manifest";
    return {};
}

// Scale-variant probe (criterion 4 "scale variants"): the preferred variant
// first (scaleVariantFor pure helper), then the remaining candidates
// {400,300,200,150,125,100} descending, then the un-scaled base — first
// existing file wins. Non-null QImage on success, null on all-miss.
QImage loadLogoVariant(const QString &basePath, qreal devicePixelRatio)
{
    QStringList order;
    order << WinUwpLogo::scaleVariantFor(basePath, devicePixelRatio); // preferred first
    static constexpr int kDescending[] = { 400, 300, 200, 150, 125, 100 };
    for (const int candidate : kDescending)
        order << WinUwpLogo::scaleVariantFor(basePath, candidate / 100.0);
    order << basePath; // un-scaled base last

    for (const QString &path : order) {
        if (QFile::exists(path)) {
            const QImage img(path);
            if (!img.isNull())
                return img;
        }
    }
    return {};
}

// Fallback: AppListEntry.DisplayInfo().GetLogo({64,64}) → read the stream
// (blocking .get() on the async op — provider thread is MTA, so blocking is
// safe) → QImage::fromData (OS resolves indirect strings internally; A4).
QImage fallbackGetLogo(const QString &packageFullName, const QString &appId)
{
    try {
        winrt::Windows::Management::Deployment::PackageManager packageManager;
        const auto package =
            packageManager.FindPackage(winrt::hstring(packageFullName.toStdWString()));
        if (!package)
            return {};
        const QString familyName = toQString(package.Id().FamilyName());
        const QString targetAumid = familyName + u'!' + appId;
        for (const auto &entry : package.GetAppListEntries()) {
            if (toQString(entry.AppUserModelId()) != targetAumid)
                continue;
            const auto streamRef = entry.DisplayInfo().GetLogo({ 64, 64 });
            const auto stream = streamRef.OpenReadAsync().get();
            const uint32_t size = static_cast<uint32_t>(stream.Size());
            if (size == 0)
                return {};
            std::vector<uint8_t> bytes(size);
            winrt::Windows::Storage::Streams::DataReader reader(stream);
            reader.LoadAsync(size).get();
            reader.ReadBytes(bytes);
            return QImage::fromData(bytes.data(), int(bytes.size()));
        }
        qWarning() << "WinUwpLogo: no AppListEntry matches" << targetAumid;
    } catch (const winrt::hresult_error &e) {
        qWarning() << "WinUwpLogo: GetLogo fallback failed"
                   << QString::fromStdString(winrt::to_string(e.message()));
    }
    return {};
}

} // namespace

namespace WinUwpLogo {

QImage extractLogo(const QString &packageFullName, const QString &appId,
                   qreal devicePixelRatio)
{
    // Same per-call apartment discipline as WinIconExtractor (task 2): MTA
    // init also initializes the WinRT apartment for this route.
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        qWarning() << "WinUwpLogo: CoInitializeEx failed" << initHr;
        return {};
    }
    const bool weInitialized = (initHr == S_OK);

    QImage result;

    // Primary route: AppxManifest.xml Square44x44Logo → SHLoadIndirectString
    // → scale-variant probe (research Pattern 2 steps 2-5). Every step
    // failure is a qWarning + null QImage — never an abort, no throw across
    // the seam (WinUwpEnumerator batch discipline; T-05-03).
    try {
        winrt::Windows::Management::Deployment::PackageManager packageManager;
        const auto package =
            packageManager.FindPackage(winrt::hstring(packageFullName.toStdWString()));
        if (!package) {
            qWarning() << "WinUwpLogo: package not found" << packageFullName;
        } else {
            const QString installRoot = toQString(package.InstalledLocation().Path());
            const QString manifestPath =
                QDir(installRoot).filePath(QStringLiteral("AppxManifest.xml"));
            QFile manifest(manifestPath);
            if (!manifest.open(QIODevice::ReadOnly)) {
                qWarning() << "WinUwpLogo: cannot open" << manifestPath;
            } else {
                const QString logoBase = findLogoBaseFromManifest(manifest, appId);
                if (!logoBase.isEmpty()) {
                    const QString basePath =
                        logoBase.startsWith(QLatin1String("@{"))
                            ? resolveIndirectString(logoBase, packageFullName)
                            : QDir(installRoot).filePath(logoBase);
                    if (!basePath.isEmpty())
                        result = loadLogoVariant(basePath, devicePixelRatio);
                }
            }
        }
    } catch (const winrt::hresult_error &e) {
        qWarning() << "WinUwpLogo: WinRT error"
                   << QString::fromStdString(winrt::to_string(e.message()));
    }

    // Fallback: DisplayInfo.GetLogo({64,64}) stream (A4 belt-and-suspenders).
    if (result.isNull())
        result = fallbackGetLogo(packageFullName, appId);

    if (weInitialized)
        CoUninitialize();
    return result;
}

} // namespace WinUwpLogo
