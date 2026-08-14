#pragma once

#include <QHash>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

#include "core/AppEntry.h"
#include "win/WinDirectoryWalk.h"

// Scan index (07-01): the pure core-side store behind the self-managed file
// scan. Holds .exe + folder entries (D-02), a per-directory mtime memo for
// incremental re-walks (D-08), and persists to a SEPARATE binary file — never
// the wisp INI (D-07). QML never binds this class directly; FileSearch /
// ScanService / ResultsModel consume it (controller-owned policy).
//
// Threading: m_entries + m_dirMtimes are guarded by m_mutex. walkAndDelta /
// save are read-locked const methods (callable from a scan worker); apply /
// swap are write-locked. Unlike QSettings stores, this class owns no
// cross-thread state beyond the two vectors — a single mutex suffices.
class FileIndex
{
public:
    struct IndexEntry {
        QString path;
        QString matchKey; // path.toCaseFolded() — pre-folded at insert
        bool isFolder = false;
    };

    struct WalkOutcome {
        QVector<IndexEntry> added;
        QSet<QString> removed;         // case-folded native paths
        QHash<QString, qint64> mtimes; // full memo after walk (copy + updates)
        int dirsListed = 0;
        int failedListings = 0;
    };

    // Empty indexPath → %APPDATA%\TID\wisp\wisp-index.dat (AppDataLocation,
    // TID/wisp). Explicit path = test seam (QTemporaryDir pattern).
    explicit FileIndex(const QString &indexPath = {});

    // Pure incremental walk (D-08): re-lists only directories whose mtime
    // changed since the stored memo. listFn is the injectable enumeration
    // seam (WinDirectoryWalk::winListDirectory in production, a fake map in
    // tests). Const + read-locked: safe to call from a worker thread.
    WalkOutcome walkAndDelta(const QStringList &roots,
                             const std::function<WinDirectoryWalk::WinDirListing(const QString &)> &listFn) const;

    // Apply a walk outcome on the UI/owner thread (write-lock; swaps state).
    void apply(const WalkOutcome &outcome);

    // Atomic persistence: QSaveFile::commit (magic 0x57535031 + version 1).
    bool save() const; // read-locked serialization
    bool load();       // corruption/tampering → false, index stays empty

    // Case-insensitive subsequence prefilter (A3 superset of FuzzyMatcher),
    // capped at kCandidateCap. Empty query → {} (D-14).
    QVector<IndexEntry> queryCandidates(const QString &query) const;

    int entryCount() const;

    // Build AppEntry rows (Source::File) from candidates; stop at cap.
    static QVector<AppEntry> toEntries(const QVector<IndexEntry> &candidates, int cap);

    static QString normalize(const QString &path); // QDir::toNativeSeparators

private:
    // Recursive per-directory pass (called with a fresh listing of `dir`).
    // childrenByParent/isDirByPath are the precomputed snapshot lookups for
    // the removal pass; listFn drives recursion.
    void walkDir(const QString &dir, int depth, const WinDirectoryWalk::WinDirListing &listing,
                 WalkOutcome &outcome, const QVector<IndexEntry> &snapshot,
                 const QHash<QString, QSet<QString>> &childrenByParent,
                 const QSet<QString> &isDirByPath,
                 const std::function<WinDirectoryWalk::WinDirListing(const QString &)> &listFn) const;

    static QString matchKeyOf(const QString &path); // normalize + toCaseFolded

    QString m_path;
    mutable QMutex m_mutex;
    QVector<IndexEntry> m_entries;
    QHash<QString, qint64> m_dirMtimes; // native-separator path → lastWriteMs

    static constexpr quint32 kMagic = 0x57535031; // ASCII "WSP1"
    static constexpr quint32 kFormatVersion = 1;
    static constexpr int kCandidateCap = 100; // research OQ5
    static constexpr int kMaxDepth = 64;      // T-07-01 defense-in-depth
};