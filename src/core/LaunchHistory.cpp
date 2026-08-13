#include "core/LaunchHistory.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace {

// QSettings groups (PATTERNS §5 — HotkeyManager IniFormat precedent).
// "launchHistory": path → launch count (D-10). "addedExecutables": path → 1,
// the never-pruned manual store (D-11). '/' is the QSettings group separator,
// so group names are slash-free and every path key is native-normalized.
const QString kLaunchHistoryGroup = QStringLiteral("launchHistory");
const QString kAddedExecutablesGroup = QStringLiteral("addedExecutables");

// PATTERNS §5 (HotkeyManager analog): IniFormat + UserScope + "TID"/"wisp" →
// %APPDATA%\TID\wisp\wisp.ini; an explicit path is the QTemporaryDir test
// seam. A factory is required because QSettings is neither copyable nor
// movable — a conditional-expression member-init would need a deleted copy
// (C2280); returning a prvalue uses guaranteed elision instead. The value
// member keeps the class move-less, destructor-less (contract).
QSettings makeSettings(const QString &settingsPath)
{
    if (settingsPath.isEmpty())
        return QSettings(QSettings::IniFormat, QSettings::UserScope,
                         QStringLiteral("TID"), QStringLiteral("wisp"));
    return QSettings(settingsPath, QSettings::IniFormat);
}

// Shared entry builder for both accessors: Source::File, displayName derived
// from the path filename (never stored — D-10 single source of truth), path
// normalized at write time. Deduped by path across the caller's set.
void appendEntry(QVector<AppEntry> &result, QSet<QString> &seen, const QString &path)
{
    if (seen.contains(path))
        return;
    seen.insert(path);
    AppEntry e;
    e.source = AppEntry::Source::File;
    e.displayName = QFileInfo(path).fileName(); // derived, never stored (D-10)
    e.targetPath = path;
    result.append(e);
}

} // namespace

LaunchHistory::LaunchHistory(const QString &settingsPath)
    : m_settings(makeSettings(settingsPath))
{
}

void LaunchHistory::recordLaunch(const AppEntry &entry)
{
    if (entry.targetPath.isEmpty())
        return; // UWP rows have no path — only executables are tracked (D-10)
    // WR-01: read + write under ONE lock — launchCount is NOT called here
    // (it would re-lock the non-recursive mutex); the count read is inlined.
    const QMutexLocker locker(&m_mutex);
    const QString key = keyFor(kLaunchHistoryGroup, entry.targetPath);
    m_settings.setValue(key, m_settings.value(key, 0).toInt() + 1);
    m_settings.sync();
}

void LaunchHistory::addExecutable(const QString &path)
{
    if (path.isEmpty())
        return;
    // WR-01: serialized with trackedExecutables (worker) and launchCount.
    const QMutexLocker locker(&m_mutex);
    m_settings.setValue(keyFor(kAddedExecutablesGroup, path), 1);
    m_settings.sync();
}

QVector<AppEntry> LaunchHistory::trackedExecutables() const
{
    // WR-01: the file-search worker calls this from the QtConcurrent pool
    // while the UI thread records launches — QSettings needs the lock.
    const QMutexLocker locker(&m_mutex);
    // QSettings has no group-list API (PATTERNS §5) — iterate allKeys and
    // filter by the group prefixes. Group names are slash-free by contract,
    // so "addedExecutables/" can never collide with a path key.
    const QString addedPrefix = kAddedExecutablesGroup + QLatin1Char('/');
    const QString historyPrefix = kLaunchHistoryGroup + QLatin1Char('/');
    const QStringList keys = m_settings.allKeys();

    QVector<AppEntry> result;
    QSet<QString> seen; // union-dedupe by path — first store wins (added first)

    // D-11 manual picks lead (the distinct store IS the never-pruned marker);
    // FileSearch re-scores the union anyway, so this order is cosmetic.
    for (const QString &key : keys)
        if (key.startsWith(addedPrefix))
            appendEntry(result, seen, key.mid(addedPrefix.size()));

    // D-10 counted launches, most-frequent first (cosmetic — re-scored).
    QVector<QPair<QString, int>> counted;
    for (const QString &key : keys)
        if (key.startsWith(historyPrefix))
            counted.append({key.mid(historyPrefix.size()), m_settings.value(key).toInt()});
    std::sort(counted.begin(), counted.end(),
              [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                  return a.second > b.second;
              });
    for (const auto &entry : counted)
        appendEntry(result, seen, entry.first);

    return result;
}

QVector<AppEntry> LaunchHistory::addedExecutables() const
{
    // Same shape as trackedExecutables' added half — default-list escape
    // hatch only, launch history never leaks in here.
    const QMutexLocker locker(&m_mutex);
    const QString addedPrefix = kAddedExecutablesGroup + QLatin1Char('/');
    const QStringList keys = m_settings.allKeys();

    QVector<AppEntry> result;
    QSet<QString> seen; // path-dedupe (union-collision safety with itself)
    for (const QString &key : keys)
        if (key.startsWith(addedPrefix))
            appendEntry(result, seen, key.mid(addedPrefix.size()));
    return result;
}

int LaunchHistory::launchCount(const QString &path) const
{
    // WR-01: may run while the worker iterates trackedExecutables — lock.
    const QMutexLocker locker(&m_mutex);
    return m_settings.value(keyFor(kLaunchHistoryGroup, path), 0).toInt();
}

QString LaunchHistory::normalize(const QString &path) const
{
    return QDir::toNativeSeparators(path);
}

QString LaunchHistory::keyFor(const QString &group, const QString &path) const
{
    return group + QLatin1Char('/') + normalize(path);
}
