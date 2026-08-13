#pragma once

#include <QString>
#include <QVector>

#include "core/AppEntry.h"

// UWP/Store app scanner — C++/WinRT firewall wrapper (RESEARCH §2), built
// against the Windows SDK projection headers (no NuGet). The pure decision
// helpers live here as free functions so tst_enum can unit-test the junk
// filter, AUMID format, and name fallback without touching WinRT objects.
namespace WinUwpEnumerator {

// Junk filter decision (PITFALLS #4 / ROADMAP criterion 2): true when ANY of
// isFramework (framework packages), !hasAppListEntry (AppListEntry None —
// non-launchable), !hasDisplayName (empty DisplayInfo.DisplayName). The live
// scan passes the DERIVED booleans: pkg.IsFramework(),
// !app.GetAppListEntries().empty(), !info.DisplayName().empty().
bool isSkippable(bool isFramework, bool hasAppListEntry, bool hasDisplayName);

// AUMID = PackageFamilyName + "!" + AppId — the Store-app launch key
// (RESEARCH §2: never guess the AUMID, never parse it from user input).
QString buildAumid(const QString &packageFamilyName, const QString &appId);

// Non-empty displayName else fallback (fallback = package.Id().Name());
// never returns empty.
QString displayNameOr(const QString &displayName, const QString &fallback);

// Live scan: PackageManager.FindPackagesForUser(L"") — empty SID = current
// user (RESEARCH §2; PowerToys Run's exact strategy) → package-level
// GetAppListEntries() → AppListEntry.DisplayInfo; junk filtered via
// isSkippable. Each entry's AUMID comes from AppListEntry.AppUserModelId(),
// which IS PackageFamilyName + "!" + AppId by OS contract (the
// deprecated-era Package.Applications/PackageApplication projection is absent
// from recent SDK headers; buildAumid's unit test locks the exact format).
// Entries carry AppEntry{source Uwp, displayName (fallback = package name),
// aumid, iconRef "" (logo ref consumed in Phase 5)}.
// CONTRACT: the calling thread must have initialized WinRT —
// winrt::init_apartment(winrt::apartment_type::multi_threaded) — the catalog
// worker (03-03) owns init/uninit. This scanner avoids direct access to the
// %ProgramFiles%\WindowsApps folder and registry Appx keys (PITFALLS #4).
QVector<AppEntry> scanUwpApps();

} // namespace WinUwpEnumerator