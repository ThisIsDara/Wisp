#include "win/WinUwpEnumerator.h"

#include <QDebug>

#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Management.Deployment.h>

namespace {
// winrt::hstring has no implicit std::wstring conversion — copy via
// fromWCharArray (length-aware) instead.
QString toQString(const winrt::hstring &s)
{
    return QString::fromWCharArray(s.c_str(), int(s.size()));
}
} // namespace

namespace WinUwpEnumerator {

bool isSkippable(bool isFramework, bool hasAppListEntry, bool hasDisplayName)
{
    return isFramework || !hasAppListEntry || !hasDisplayName;
}

QString buildAumid(const QString &packageFamilyName, const QString &appId)
{
    return packageFamilyName + u'!' + appId;
}

QString displayNameOr(const QString &displayName, const QString &fallback)
{
    return displayName.isEmpty() ? fallback : displayName;
}

QVector<AppEntry> scanUwpApps()
{
    QVector<AppEntry> entries;
    try {
        winrt::Windows::Management::Deployment::PackageManager packageManager;
        const auto packages = packageManager.FindPackagesForUser(L"");
        for (const auto &package : packages) {
            try {
                if (package.IsFramework())
                    continue;
                // Per-app entries come from the package's app list — the
                // deprecated-era Package.Applications/PackageApplication
                // projection is absent from the 26100 SDK headers, and each
                // AppListEntry exposes the launch AUMID directly.
                for (const auto &appEntry : package.GetAppListEntries()) {
                    try {
                        const QString aumid = toQString(appEntry.AppUserModelId());
                        if (aumid.isEmpty())
                            continue; // no launch key — treat as junk (T-03-02-04)
                        const QString rawName =
                            toQString(appEntry.DisplayInfo().DisplayName());
                        if (isSkippable(false, true, !rawName.isEmpty()))
                            continue;

                        AppEntry entry;
                        entry.source = AppEntry::Source::Uwp;
                        entry.displayName = displayNameOr(
                            rawName, toQString(package.Id().Name()));
                        // OS-provided AppUserModelId == PackageFamilyName +
                        // "!" + AppId by contract (buildAumid unit test locks
                        // the exact format).
                        entry.aumid = aumid;
                        // 05-04: iconRef "uwp:{PFN}|{appId}" — the exact uwp
                        // rule WinIconExtractor::parseKey routes on
                        // (WinIconExtractor.h:49-72). PFN from the package
                        // identity; appId split from the AUMID (contract
                        // above). Empty halves are never emitted — parseKey
                        // would reject the key anyway; the QML monogram
                        // covers the no-icon case.
                        const QString packageFullName =
                            toQString(package.Id().FullName());
                        const qsizetype bang = aumid.indexOf(u'!');
                        if (!packageFullName.isEmpty() && bang > 0
                            && bang < aumid.size() - 1)
                            entry.iconRef = QStringLiteral("uwp:")
                                + packageFullName + u'|' + aumid.mid(bang + 1);
                        entries.push_back(entry);
                    } catch (const winrt::hresult_error &e) {
                        // One broken app must not abort the batch.
                        qWarning() << "skipping UWP app:"
                                   << QString::fromStdString(
                                          winrt::to_string(e.message()));
                    }
                }
            } catch (const winrt::hresult_error &e) {
                // One broken package must not abort the batch.
                qWarning() << "skipping UWP package:"
                           << QString::fromStdString(
                                  winrt::to_string(e.message()));
            }
        }
    } catch (const winrt::hresult_error &e) {
        qWarning() << "UWP enumeration failed:"
                   << QString::fromStdString(winrt::to_string(e.message()));
    }
    return entries;
}

} // namespace WinUwpEnumerator