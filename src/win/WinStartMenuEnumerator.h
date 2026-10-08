#pragma once

#include <QStringList>
#include <QVector>

#include "core/AppEntry.h"

// Start Menu .lnk scanner — Win32/COM firewall wrapper (ARCHITECTURE.md
// src/win/ pattern; RESEARCH §1). Pure C++ interface: all Win32 detail lives
// in the .cpp so callers (catalog worker, tst_enum) never touch COM.
namespace WinStartMenuEnumerator {

// Scans FOLDERID_Programs (per-user) + FOLDERID_CommonPrograms (all-users)
// recursively for *.lnk, parses each via IShellLinkW+IPersistFile, and
// returns AppEntry{source Lnk, displayName (description, else .lnk base
// name), targetPath (resolved), arguments (GetArguments — elevation feeds
// lpParameters), iconRef ("iconPath;index" from GetIconLocation — Phase 5)}.
// Both known folders are mandatory: skipping CommonPrograms misses
// machine-wide installs. Broken links (Load/GetPath failure) are skipped
// with a qWarning — the scan never aborts mid-batch.
// Must be called on a COM-initialized thread (catalog worker — CoInitializeEx
// or winrt::init_apartment); a one-time CoInitializeEx fallback is attempted
// internally when the thread has no apartment yet.
QVector<AppEntry> scanStartMenu();

// Test seam: scans an explicit set of root directories instead of the two
// known folders (tst_enum feeds a QTemporaryDir fixture with real .lnk
// files). Same parse/omit rules as scanStartMenu(); the live scan delegates
// here after resolving the known folders.
QVector<AppEntry> scanRoots(const QStringList &rootDirs);

// Resolves ONE .lnk to its target path. Returns an empty string when the link is
// broken, unreadable, or has no target — never throws and never aborts a scan.
//
// This is the discriminator duplicate detection needs: two Start Menu entries are
// duplicates of the same app when they launch the SAME program, which the file's
// name cannot tell you. Measured on the real index (2026-10-08), grouping by
// display name flagged an app's own versioned build folder
// ("Discord\app-1.0.9261\Discord.exe" against "Discord\Discord.exe") and an
// autostart shortcut as duplicates of the app — both nonsense. Resolving the
// target gives one honest answer per program.
//
// Called during the file-index walk, which runs on the scan worker and memoises
// unchanged directories, so the COM cost is paid only when a directory actually
// changes. Must be called on a COM-initialized thread; a one-time
// CoInitializeEx fallback is attempted internally when the thread has no
// apartment yet (same contract as scanRoots).
QString resolveLnkTarget(const QString &lnkPath);

} // namespace WinStartMenuEnumerator