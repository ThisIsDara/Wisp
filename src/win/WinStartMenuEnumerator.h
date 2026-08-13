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

} // namespace WinStartMenuEnumerator