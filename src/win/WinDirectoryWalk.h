#pragma once

#include <QString>
#include <QVector>

// Thin Win32 firewall for per-directory enumeration (07-01): the ONLY
// recursive-walk primitive the scan phase needs. FindFirstFileExW with
// FindExInfoBasic + FIND_FIRST_EX_LARGE_FETCH beats QDirIterator by ~15x
// and std::filesystem by ~130x on uncached trees (Qt 6.8+ iterator
// regression, RESEARCH §1) — all of that Win32 lives HERE and nowhere else.
//
// Contract: Win32 types never appear in this header — callers (FileIndex,
// ScanService) see plain Qt structs only. No COM: FindFirstFileExW needs
// kernel32 only (no COM apartment initialization required for
// FindFirstFile-style walking, D-08 — unlike every other src/win unit).
// One shared FILETIME → ms conversion keeps memo keys comparable across
// listings (memo comparability is a correctness requirement — D-08).
// Failure → ok=false + qWarning, never a throw, never a hang (the header
// documents the Error state).
namespace WinDirectoryWalk {

struct WinDirEntry {
    QString name;
    bool isDir = false, hidden = false, system = false, reparse = false;
    qint64 lastWriteMs = 0; // FILETIME → ms — ONE shared helper (memo comparability)
};

struct WinDirListing { QVector<WinDirEntry> entries; qint64 lastWriteMs = 0; bool ok = false; };

// Lists ONE directory, non-recursive: "." / ".." skipped; isDir/hidden/
// system/reparse bits and lastWriteMs come from the find data ONLY (no
// per-entry GetFileAttributesExW/status() calls — RESEARCH Pitfall 1/2).
// ok=false → error state (missing dir, path too long, access denied):
// callers retry on a later scan, never crash.
WinDirListing winListDirectory(const QString &path);

} // namespace WinDirectoryWalk