#pragma once
#include <QSet>
#include <QSettings>
#include <QString>

// 05.1: persisted per-row Hide/Unhide overrides (CUR-02/CUR-03). Persists
// to the existing wisp INI under the ADDITIVE groups "curationHidden" /
// "curationShown" (id → 1) via the makeSettings factory (SettingsStore/
// LaunchHistory precedent, PATTERNS P1). Identity contract: Lnk →
// targetPath, UWP → aumid — both slash-free by the enumerator contracts
// (research Pitfall 2); an id whose app was uninstalled is inert (nothing
// matches it on the next build). Last user action wins: hide() clears the
// shown entry, show() clears the hidden entry.
//
// THREADING CONTRACT: UI-thread-only (SettingsStore precedent — NO mutex;
// if a worker ever reads this store, add the LaunchHistory WR-01 QMutex
// discipline first).
class CurationStore {
public:
    explicit CurationStore(const QString &settingsPath = {}); // "" → TID/wisp INI
    void hide(const QString &id);   // "curationHidden/{id}" = 1 + sync()
    void show(const QString &id);   // "curationShown/{id}" = 1 + sync()
    QSet<QString> hiddenIds() const; // missing group → empty set, silent (D-16)
    QSet<QString> shownIds() const;

    // ── Removed (2026-10-08) ────────────────────────────────────────────────
    // A THIRD override, deliberately not "hidden". The user asked for a removed
    // duplicate to leave the app entirely — not to sit in "Show hidden (N)"
    // waiting to be un-hidden, which is what hiding does by design (CUR-03).
    // Removal therefore has its own group, so it:
    //   - filters rows out unconditionally, including in show-hidden mode;
    //   - is NOT counted by hiddenCount(), so the footer never offers to restore
    //     it and it cannot be stumbled back into by toggling Show hidden;
    //   - still deletes nothing — the .lnk/.exe stays on disk.
    //
    // ROOT-BOUND (2026-10-08, later the same day). The removed key's VALUE is
    // the scan root that owns that entry. Removing a root therefore drops the
    // removals it owned, and re-adding it brings those apps back — the user's
    // explicit requirement, after "Discord" stayed gone forever even though the
    // containing directory had been removed and re-added.
    void remove(const QString &id, const QString &owningRoot = {});
    void restore(const QString &id);
    // Drops every removal whose owning root is no longer in `activeRoots`.
    // Removal records with an EMPTY owner (a manual pick, which belongs to no
    // scanned directory) are never dropped. Records written before this scheme
    // existed carry the legacy value "1", which is not a root, so the first
    // prune clears them - which is how a previously permanent removal becomes
    // reversible again.
    void pruneRemovals(const QStringList &activeRoots);
    QSet<QString> removedIds() const;
    // The root that owns `path`: the LONGEST active root that is a
    // case-insensitive path-prefix of it (so a nested root wins over its parent).
    // Empty when the path belongs to no configured root - i.e. a manual pick.
    static QString owningRoot(const QStringList &roots, const QString &path);
private:
    QSettings m_settings;           // non-copyable member (LaunchHistory precedent)
    // Roots the removal records were last pruned against — the cheap gate that
    // keeps pruneRemovals() off the per-batch read path (see its comment).
    // Guarded by an explicit flag rather than a sentinel string: "no roots
    // configured" joins to an EMPTY string, which would compare equal to a
    // default-constructed member and silently skip the very first prune — so
    // stale removals would never be cleared.
    mutable QString m_lastPrunedRoots;
    mutable bool m_prunedOnce = false;
};
