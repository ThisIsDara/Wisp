#include "win/WinDirectoryWalk.h"

#include <QDebug>

#include <windows.h>

#include <cwchar> // wcscmp — "." / ".." skip

namespace {

// `\\?\` long-path prefix (RESEARCH Pitfall 7): FindFirstFileExW fails with
// ERROR_PATH_NOT_FOUND on patterns longer than MAX_PATH (260) — the same
// prefixing qtbase's own file iterator applies on Windows. UNC paths need
// the `\\?\UNC\server\share` form (the prefix consumes the leading `\\`).
QString longPathPrefix(const QString &path)
{
    if (path.size() + 2 <= 260)
        return {};
    if (path.startsWith(QStringLiteral("\\\\")))
        return QStringLiteral("\\\\?\\UNC\\") + path.mid(2);
    return QStringLiteral("\\\\?\\");
}

// FILETIME (100ns ticks since 1601-01-01) → ms since 1970-01-01 — the ONE
// shared conversion in the walker so mtime memo keys are comparable across
// listings (D-08 memo correctness depends on a single conversion).
qint64 fileTimeToMs(const FILETIME &ft)
{
    const quint64 ticks = (quint64(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return qint64(ticks / 10000 - 11644473600000ULL);
}

} // namespace

namespace WinDirectoryWalk {

WinDirListing winListDirectory(const QString &path)
{
    WinDirListing out;

    // Paths beyond the `\\?\` 32767-char limit cannot be enumerated by Win32
    // at all — error state + log, never a hang (RESEARCH Pitfall 7). 32000
    // leaves headroom for the "\\*" suffix and a few trailing components.
    if (path.size() > 32000) {
        qWarning() << "WinDirectoryWalk: path exceeds the \\\\?\\ limit, skipping:" << path;
        return out;
    }

    const QString pattern = longPathPrefix(path) + path + QLatin1String("\\*");

    WIN32_FIND_DATA fd;
    const HANDLE h = FindFirstFileExW(reinterpret_cast<const wchar_t *>(pattern.utf16()),
                                      FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                      FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        qWarning() << "WinDirectoryWalk: FindFirstFileExW failed for" << path;
        return out; // ok stays false — Error state, never a hang
    }

    // The first find result is the directory's own "." entry — its
    // ftLastWriteTime IS the directory's mtime (find data, zero extra calls).
    out.lastWriteMs = fileTimeToMs(fd.ftLastWriteTime);

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        WinDirEntry e;
        e.name = QString::fromWCharArray(fd.cFileName);
        e.isDir = fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        e.hidden = fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN;
        e.system = fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM;
        e.reparse = fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT; // never descend (T-07-01)
        e.lastWriteMs = fileTimeToMs(fd.ftLastWriteTime);
        out.entries.append(e);
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    out.ok = true;
    return out;
}

} // namespace WinDirectoryWalk