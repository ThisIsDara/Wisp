#pragma once
#include <QMutex>
#include <QSettings>
#include <QVector>
#include "core/AppEntry.h"

// Launch tracking + manual executable store (D-10/D-11). Persists to the
// existing wisp INI via QSettings IniFormat (HotkeyManager precedent,
// PATTERNS §5): group "launchHistory" (path → launch count) records every
// executable wisp launches (D-10); group "addedExecutables" (path → 1)
// holds manual picks — the distinct store IS the "never pruned" marker
// (v2 recency may prune launchHistory, never addedExecutables).
// All paths normalized to native separators at write time: QSettings treats
// '/' as a group separator, and QFileDialog returns '/'-separated paths —
// normalization is what makes paths safe as keys.
class LaunchHistory
{
public:
    // Empty settingsPath → default %APPDATA%\TID\wisp\wisp.ini (IniFormat,
    // UserScope, TID/wisp). Explicit path = test seam (QTemporaryDir pattern).
    explicit LaunchHistory(const QString &settingsPath = {});

    // D-10: increment the count for entry.targetPath (native-normalized).
    // Entries with an empty targetPath (UWP) are skipped — only executables
    // are tracked. displayName is NOT stored: it derives from the path
    // (single source of truth; a renamed file shows its new name).
    void recordLaunch(const AppEntry &entry);

    // D-11: register a manually picked executable (file-dialog result;
    // may be '/'-separated). Stored in the addedExecutables group.
    void addExecutable(const QString &path);

    // D-06: the second search source — union of both groups, deduped by
    // path (added wins on collision), Source::File entries with
    // displayName = filename, targetPath = path. Order: added first, then
    // launchHistory by count desc (FileSearch re-scores anyway).
    QVector<AppEntry> trackedExecutables() const;

    // D-11: the addedExecutables group ONLY (never launch history) — the
    // default-list source. Manual picks are the curated-allowlist escape
    // hatch (CUR-04): they join the empty-query default list, launch-tracked
    // exes do not. Same shape as trackedExecutables' added half.
    QVector<AppEntry> addedExecutables() const;

    // Test + v2 recency accessor. 0 for unknown paths.
    int launchCount(const QString &path) const;
    // ms since epoch of last launch, 0 if never launched.
    qint64 lastLaunchMs(const QString &path) const;
    // Frecency boost: frequency * recency decay, capped < kTierGap (200) so
    // tier order is preserved. 0 if never launched.
    int frecencyBoost(const QString &path) const;

private:
    QString normalize(const QString &path) const; // QDir::toNativeSeparators
    QString keyFor(const QString &group, const QString &path) const;
    // WR-01: ONE QSettings instance is touched from TWO threads — the UI
    // thread (recordLaunch/addExecutable from the LaunchController reporter,
    // launchCount) and the file-search worker (trackedExecutables via the
    // TrackedSource seam). QSettings is not safe for concurrent use of one
    // instance (its cache/map layers are unlocked member state), so every
    // access is serialized under this mutex. mutable: the reader path is const.
    mutable QMutex m_mutex;
    QSettings m_settings; // non-copyable member (QSettings) — class is move-less by design
};
