#include "core/FileIndex.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

// D-06 fixed skip list, case-insensitive leaf match. Applied to BOTH file and
// directory leaves — a legit "windows.exe" next to C:\Windows is pathological
// enough that the filter cost beats the result noise (CONTEXT D-06).
const QStringList kSkipNames = {
    QStringLiteral("windows"),        QStringLiteral("programdata"),
    QStringLiteral("appdata"),        QStringLiteral("windowsapps"),
    QStringLiteral("node_modules"),   QStringLiteral(".git"),
    QStringLiteral(".svn"),           QStringLiteral(".hg"),
    QStringLiteral(".gradle"),        QStringLiteral(".m2"),
    QStringLiteral(".cargo"),         QStringLiteral("$recycle.bin"),
    QStringLiteral("system volume information"), QStringLiteral("msocache"),
    QStringLiteral("config.msi"),
};

bool skipLeaf(const QString &name)
{
    return kSkipNames.contains(name.toCaseFolded());
}

// D-02: v1 indexes .exe executables + directories only.
bool isExe(const QString &name)
{
    return name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive);
}

// %APPDATA%\TID\wisp\wisp-index.dat — same AppData base the wisp.ini uses,
// but a SEPARATE binary file, never the INI (D-07).
QString defaultIndexPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + QStringLiteral("/wisp-index.dat");
}

} // namespace

FileIndex::FileIndex(const QString &indexPath)
    : m_path(indexPath.isEmpty() ? defaultIndexPath() : indexPath)
{
}

FileIndex::WalkOutcome FileIndex::walkAndDelta(
    const QStringList &roots,
    const std::function<WinDirectoryWalk::WinDirListing(const QString &)> &listFn) const
{
    WalkOutcome outcome;

    // Snapshot the memo + entries under lock, then walk WITHOUT the lock —
    // the walk is the slow part (listFn blocks on Win32) and apply() only
    // runs after this returns (single-flight scan contract).
    QVector<IndexEntry> snapshot;
    {
        const QMutexLocker lock(&m_mutex);
        outcome.mtimes = m_dirMtimes;
        snapshot = m_entries;
    }

    // No roots → explicit WIPE (D-09 "no locations" semantics): every entry
    // is removed and the memo cleared — a re-added root must re-walk from
    // scratch (a stale memo would skip its re-list and the index would stay
    // empty forever). Removals otherwise only occur in re-listed dirs, so
    // the empty-roots case needs this dedicated path.
    if (roots.isEmpty()) {
        for (const auto &e : snapshot)
            outcome.removed.insert(e.matchKey);
        outcome.mtimes.clear();
        return outcome;
    }

    // Parent → direct-child lookup built once from the snapshot: removals are
    // computed per re-listed dir against its own DIRECT children only — the
    // recursion decides subtree fate one level at a time, so a dir's removal
    // only ever needs the direct-child set (O(1) per dir after this pass).
    QHash<QString, QSet<QString>> childrenByParent;
    QSet<QString> isDirByPath;
    for (const auto &e : snapshot) {
        const int sep = e.path.lastIndexOf(QLatin1Char('\\'));
        if (sep < 0)
            continue;
        QString parent = e.path.left(sep);
        if (parent.endsWith(QLatin1Char(':')))
            parent += QLatin1Char('\\'); // drive root "C:" → "C:\" (memo key shape)
        childrenByParent[parent].insert(e.path);
        if (e.isFolder)
            isDirByPath.insert(e.path);
    }

    for (const QString &root : roots) {
        const QString normRoot = normalize(root);
        if (normRoot.isEmpty())
            continue;
        const auto listing = listFn(normRoot);
        if (!listing.ok) {
            outcome.failedListings++;
            continue; // memo untouched — retry on a later scan, never crash
        }
        outcome.dirsListed++;
        outcome.mtimes.insert(normRoot, listing.lastWriteMs);
        walkDir(normRoot, 0, listing, outcome, snapshot, childrenByParent, isDirByPath, listFn);
    }
    return outcome;
}

void FileIndex::walkDir(const QString &dir, int depth, const WinDirectoryWalk::WinDirListing &listing,
                        WalkOutcome &outcome, const QVector<IndexEntry> &snapshot,
                        const QHash<QString, QSet<QString>> &childrenByParent,
                        const QSet<QString> &isDirByPath,
                        const std::function<WinDirectoryWalk::WinDirListing(const QString &)> &listFn) const
{
    // Fresh INDEXABLE direct children of THIS dir — one pass, O(n) for the
    // listing. Same filter as the add pass: a child that turned hidden or
    // skipped is treated as absent → its old entry + subtree get removed.
    QSet<QString> freshNames;
    for (const auto &entry : listing.entries)
        if (!skipLeaf(entry.name) && !entry.hidden && !entry.system && !entry.reparse)
            freshNames.insert(entry.name);

    // Removal pass (direct children only, T-07-01): a stale direct child
    // means the name vanished from the fresh listing. A stale DIR child
    // additionally sweeps its whole old subtree from the index — deleted
    // dirs leave no orphans.
    const auto oldChildrenIt = childrenByParent.constFind(dir);
    if (oldChildrenIt != childrenByParent.constEnd()) {
        for (const QString &oldChild : *oldChildrenIt) {
            QString name = oldChild.mid(dir.size());
            if (name.startsWith(QLatin1Char('\\')))
                name.remove(0, 1);
            if (freshNames.contains(name))
                continue;
            outcome.removed.insert(matchKeyOf(oldChild));
            if (isDirByPath.contains(oldChild)) {
                const QString prefix = oldChild + QLatin1Char('\\');
                for (const auto &e : snapshot)
                    if (e.path.startsWith(prefix))
                        outcome.removed.insert(e.matchKey);
            }
        }
    }

    // Add + recurse pass. Dirs are always indexed (D-02); a dir whose memo
    // mtime matches is skipped WITHOUT a listing call — its subtree stays
    // intact (D-08 incremental; RESEARCH Pitfall 3 avoided).
    for (const auto &entry : listing.entries) {
        if (skipLeaf(entry.name) || entry.hidden || entry.system || entry.reparse)
            continue;
        const QString full = dir.endsWith(QLatin1Char('\\')) ? dir + entry.name
                                                             : dir + QLatin1Char('\\') + entry.name;
        if (entry.isDir) {
            outcome.added.append(IndexEntry{full, matchKeyOf(full), true});
            const auto memoIt = outcome.mtimes.constFind(full);
            if (memoIt != outcome.mtimes.constEnd() && memoIt.value() == entry.lastWriteMs)
                continue; // unchanged subtree — keep existing entries
            if (depth + 1 >= kMaxDepth)
                continue; // T-07-01 defense-in-depth (walker caps at 64)
            const auto sub = listFn(full);
            if (!sub.ok) {
                outcome.failedListings++;
                continue; // keep old subtree — retry next scan
            }
            outcome.dirsListed++;
            outcome.mtimes.insert(full, sub.lastWriteMs);
            walkDir(full, depth + 1, sub, outcome, snapshot, childrenByParent, isDirByPath, listFn);
        } else if (isExe(entry.name)) {
            outcome.added.append(IndexEntry{full, matchKeyOf(full), false});
        }
    }
}

void FileIndex::apply(const WalkOutcome &outcome)
{
    const QMutexLocker lock(&m_mutex);

    // Removals first, then additions: a re-listed dir produces removed
    // (stale) and added (fresh) entries that never overlap — the removed set
    // is computed as old-minus-fresh per dir — but apply() is order-immune
    // regardless (dedupe on insert below).
    QSet<QString> existing;
    existing.reserve(m_entries.size());
    for (const auto &e : m_entries)
        existing.insert(e.matchKey);
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (outcome.removed.contains(it->matchKey)) {
            existing.remove(it->matchKey);
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto &e : outcome.added) {
        if (existing.contains(e.matchKey))
            continue; // overlapping roots (C:\ + C:\Users) — first wins
        existing.insert(e.matchKey);
        m_entries.append(e);
    }
    for (auto it = outcome.mtimes.constBegin(); it != outcome.mtimes.constEnd(); ++it)
        m_dirMtimes.insert(it.key(), it.value());
}

bool FileIndex::save() const
{
    const QMutexLocker lock(&m_mutex);
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_5); // pinned — persisted format stability
    ds << kMagic << kFormatVersion;
    ds << quint32(m_entries.size());
    for (const auto &e : m_entries)
        ds << e.path << e.isFolder;
    ds << quint32(m_dirMtimes.size());
    for (auto it = m_dirMtimes.constBegin(); it != m_dirMtimes.constEnd(); ++it)
        ds << it.key() << it.value();
    return file.commit(); // atomic rename — crash-safe (D-07)
}

bool FileIndex::load()
{
    QFile file(m_path);
    if (!file.exists())
        return true; // first run — empty index is a valid state
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_5);
    quint32 magic = 0, version = 0;
    ds >> magic >> version;
    if (ds.status() != QDataStream::Ok || magic != kMagic || version != kFormatVersion)
        return false; // foreign/truncated file — start empty, never crash

    quint32 count = 0;
    ds >> count;
    if (ds.status() != QDataStream::Ok)
        return false;
    QVector<IndexEntry> entries;
    entries.reserve(int(count));
    for (quint32 i = 0; i < count; ++i) {
        IndexEntry e;
        ds >> e.path >> e.isFolder;
        if (ds.status() != QDataStream::Ok)
            return false;
        e.matchKey = matchKeyOf(e.path);
        entries.append(e);
    }

    QHash<QString, qint64> mtimes;
    ds >> count;
    if (ds.status() != QDataStream::Ok)
        return false;
    for (quint32 i = 0; i < count; ++i) {
        QString key;
        qint64 value = 0;
        ds >> key >> value;
        if (ds.status() != QDataStream::Ok)
            return false;
        mtimes.insert(key, value);
    }

    const QMutexLocker lock(&m_mutex);
    m_entries = std::move(entries);
    m_dirMtimes = std::move(mtimes);
    return true;
}

QVector<FileIndex::IndexEntry> FileIndex::queryCandidates(const QString &query) const
{
    if (query.isEmpty())
        return {}; // no empty-query default list from the scan (D-14)
    const QString folded = query.toCaseFolded();
    const QMutexLocker lock(&m_mutex);
    QVector<IndexEntry> out;
    out.reserve(kCandidateCap);
    for (const auto &e : m_entries) {
        // Case-insensitive subsequence two-pointer prefilter — a superset of
        // FuzzyMatcher acceptance (A3): anything FuzzyMatcher would accept is
        // already a subsequence of the folded path.
        int qi = 0;
        const int ql = folded.size();
        for (int mi = 0; mi < e.matchKey.size() && qi < ql; ++mi)
            if (e.matchKey.at(mi) == folded.at(qi))
                ++qi;
        if (qi == ql) {
            out.append(e);
            if (out.size() >= kCandidateCap)
                break;
        }
    }
    return out;
}

int FileIndex::entryCount() const
{
    const QMutexLocker lock(&m_mutex);
    return m_entries.size();
}

QVector<AppEntry> FileIndex::toEntries(const QVector<IndexEntry> &candidates, int cap)
{
    const int n = qMin(candidates.size(), cap);
    QVector<AppEntry> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        AppEntry e;
        e.source = AppEntry::Source::File;
        e.displayName = QFileInfo(candidates[i].path).fileName(); // derived, never stored
        e.targetPath = candidates[i].path;
        e.isFolder = candidates[i].isFolder;
        out.append(e);
    }
    return out;
}

QString FileIndex::normalize(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

QString FileIndex::matchKeyOf(const QString &path)
{
    return normalize(path).toCaseFolded();
}
