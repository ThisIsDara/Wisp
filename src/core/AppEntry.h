#pragma once
#include <QString>

// Shared phase-3 entry contract: produced by the 03-02 enumerators,
// aggregated/deduped by 03-03, launched by 03-04, rendered by 03-05.
// iconRef: GetIconLocation output / UWP logo ref — Phase 5 consumes it, unused here.
// Phase-4 extension (04-01): Source::File rows come from the Windows Search
// pipeline (04-02). File semantics: displayName = filename minus ".exe"
// (title — see fileEntryTitle), targetPath = full path (subtitle + launch +
// Ctrl+Enter Explorer reveal), isFolder = D-04 folder rows (glyph in the
// monogram, opens in Explorer). arguments/aumid stay empty for File rows.

// 2026-08-15: the display title for a File row — the filename WITHOUT the
// ".exe"/".lnk" extension ("Wow.exe" → "Wow", "Steam.lnk" → "Steam"). Used at
// the two File-row entry-build sites (FileIndex::toEntries,
// LaunchHistory::appendEntry); never stored. Only those two extensions are
// stripped (case-insensitive) — other files keep their full filename (file
// search can return anything, but the launcher's inventory is executables +
// shortcuts). Derived from the basename, never the whole path.
inline QString fileEntryTitle(const QString &path)
{
    const int slash = qMax(path.lastIndexOf(u'/'), path.lastIndexOf(u'\\'));
    QString name = path.mid(slash + 1);
    if (name.size() > 4
        && (name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
            || name.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive)))
        name.chop(4);
    return name;
}
struct AppEntry {
    enum class Source { Lnk, Uwp, File, Calculator };
    Source source = Source::Lnk;
    QString displayName;    // What the user sees and searches (D-01..D-07 use only this)
    QString targetPath;     // Resolved .lnk target (classic); full path (File); empty for UWP
    QString arguments;      // .lnk GetArguments (elevation contract, RESEARCH §1) — empty for UWP/File
    QString aumid;          // PackageFamilyName + "!" + AppId (UWP launch key) — empty for Lnk/File
    QString iconRef;        // GetIconLocation output / UWP logo ref — Phase 5 consumes, unused here
    bool isFolder = false;  // File rows only (D-04): folder rows render a glyph and open in Explorer
    bool hidden = false;  // 05.1: set by markCurated (default rules OR user hide);
                          // never set for Source::File rows (escape hatch, CUR-04)
};
