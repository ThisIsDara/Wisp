#include "core/CurationStore.h"

#include <QDebug>
#include <QDir>

namespace {

const QString kHiddenGroup = QStringLiteral("curationHidden");
const QString kShownGroup  = QStringLiteral("curationShown");
const QString kRemovedGroup = QStringLiteral("curationRemoved");   // 2026-10-08

// 05.1 review (L-04): INI-hostile characters — QSettings' INI parser treats
// '=' as the key/value separator and '[' ']' as group delimiters; an id
// containing them would truncate or corrupt the registry line, making the
// hide silently inert. Windows paths CAN legally contain '='. Such ids are
// skipped: the row still hides in-session (model mark), only persistence is
// forfeited — safer than corrupting the store.
bool isIniSafe(const QString &id)
{
    return !id.contains(QLatin1Char('=')) && !id.contains(QLatin1Char('['))
        && !id.contains(QLatin1Char(']'));
}

// PATTERNS P1 — SettingsStore.cpp:11-17 VERBATIM.
QSettings makeSettings(const QString &settingsPath)
{
    if (settingsPath.isEmpty())
        return QSettings(QSettings::IniFormat, QSettings::UserScope,
                         QStringLiteral("TID"), QStringLiteral("wisp"));
    return QSettings(settingsPath, QSettings::IniFormat);
}

// 05.1 review (L-03/L-04): a failed sync silently lost the override —
// surface it; INI-hostile ids are skipped before touching the store.
void writeOverride(QSettings &s, const QString &group, const QString &id, bool isShown)
{
    if (id.isEmpty())
        return; // identity contract — never write a key without an id
    if (!isIniSafe(id))
        return;
    const QString key = group + QLatin1Char('/') + id;
    const QString other = (isShown ? kHiddenGroup : kShownGroup) + QLatin1Char('/') + id;
    s.setValue(key, 1);
    s.remove(other); // last action wins — fresh override cancels the opposite
    s.sync();        // PATTERNS P2 — sync after EVERY write
    if (s.status() != QSettings::NoError)
        qWarning() << "CurationStore: write failed for" << group << id;
}

} // namespace

CurationStore::CurationStore(const QString &settingsPath)
    : m_settings(makeSettings(settingsPath))
{
}

void CurationStore::hide(const QString &id)
{
    writeOverride(m_settings, kHiddenGroup, id, false);
}

void CurationStore::show(const QString &id)
{
    writeOverride(m_settings, kShownGroup, id, true);
}

QSet<QString> CurationStore::hiddenIds() const
{
    const QString prefix = kHiddenGroup + QLatin1Char('/');
    const QStringList keys = m_settings.allKeys();
    QSet<QString> ids;
    for (const QString &key : keys)
        if (key.startsWith(prefix))
            ids.insert(key.mid(prefix.size()));
    return ids;
}

QSet<QString> CurationStore::shownIds() const
{
    const QString prefix = kShownGroup + QLatin1Char('/');
    const QStringList keys = m_settings.allKeys();
    QSet<QString> ids;
    for (const QString &key : keys)
        if (key.startsWith(prefix))
            ids.insert(key.mid(prefix.size()));
    return ids;
}

// 2026-10-08: writeOverride's two-group "last action wins" pairing does not
// apply here — removal is its own axis, not the opposite of showing. So it does
// not reuse writeOverride: it clears the hidden/shown entries for the same id
// (a removed row must never linger in hiddenCount) and then writes its own key.
//
// The VALUE is the owning scan root, which is what makes a removal
// root-bound: removing the root drops the records it owned, and re-adding it
// brings those apps back.
void CurationStore::remove(const QString &id, const QString &owningRoot)
{
    if (id.isEmpty() || !isIniSafe(id))
        return;
    m_settings.remove(kHiddenGroup + QLatin1Char('/') + id);
    m_settings.remove(kShownGroup + QLatin1Char('/') + id);
    // An INI-hostile root (a path may legally contain '=' on Windows) would make
    // the VALUE ambiguous, so degrade to an owner-less record rather than risk a
    // corrupt line. Owner-less records are never pruned, so the removal simply
    // behaves like the pre-scoping behaviour for that one entry.
    const bool rootSafe = owningRoot.isEmpty() || isIniSafe(owningRoot);
    m_settings.setValue(kRemovedGroup + QLatin1Char('/') + id,
                        rootSafe ? owningRoot : QString());
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError)
        qWarning() << "CurationStore: write failed for" << kRemovedGroup << id;
}

void CurationStore::restore(const QString &id)
{
    if (id.isEmpty() || !isIniSafe(id))
        return;
    m_settings.remove(kRemovedGroup + QLatin1Char('/') + id);
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError)
        qWarning() << "CurationStore: write failed for" << kRemovedGroup << id;
}

void CurationStore::pruneRemovals(const QStringList &activeRoots)
{
    // Cheap gate: this is reached from the read side (removedIds), which the
    // model calls on every result batch. Roots almost never change, so the scan
    // only runs when they actually did.
    const QString signature = activeRoots.join(QLatin1Char('\n'));
    if (m_prunedOnce && signature == m_lastPrunedRoots)
        return;
    m_lastPrunedRoots = signature;
    m_prunedOnce = true;

    const QString prefix = kRemovedGroup + QLatin1Char('/');
    QSet<QString> active;
    active.reserve(activeRoots.size());
    for (const QString &r : activeRoots)
        active.insert(r.trimmed().toCaseFolded());

    const QStringList keys = m_settings.allKeys();
    int dropped = 0;
    for (const QString &key : keys) {
        if (!key.startsWith(prefix))
            continue;
        const QVariant owner = m_settings.value(key);
        const QString ownerText = owner.toString();
        // Owner-less (a manual pick, or a legacy record) is NOT root-bound and
        // must survive any root change.
        if (ownerText.isEmpty())
            continue;
        if (active.contains(ownerText.trimmed().toCaseFolded()))
            continue;
        m_settings.remove(key);
        ++dropped;
    }
    if (dropped > 0) {
        m_settings.sync();
        if (m_settings.status() != QSettings::NoError)
            qWarning() << "CurationStore: prune write failed";
    }
}

QString CurationStore::owningRoot(const QStringList &roots, const QString &path)
{
    if (path.isEmpty())
        return QString();
    const QString needle = QDir::toNativeSeparators(path).toCaseFolded();
    QString best;
    for (const QString &raw : roots) {
        if (raw.isEmpty())
            continue;
        const QString root = QDir::toNativeSeparators(raw.trimmed()).toCaseFolded();
        if (root.isEmpty() || needle.size() <= root.size())
            continue;
        if (!needle.startsWith(root))
            continue;
        // Respect the separator boundary so "C:\Program" never owns
        // "C:\ProgramData\...".
        const QChar next = needle.at(root.size());
        if (next != QLatin1Char('\\') && next != QLatin1Char('/'))
            continue;
        // Longest wins, so a nested root beats its parent.
        if (root.size() > best.size())
            best = raw.trimmed();
    }
    return best;
}

QSet<QString> CurationStore::removedIds() const
{
    const QString prefix = kRemovedGroup + QLatin1Char('/');
    const QStringList keys = m_settings.allKeys();
    QSet<QString> ids;
    for (const QString &key : keys)
        if (key.startsWith(prefix))
            ids.insert(key.mid(prefix.size()));
    return ids;
}
