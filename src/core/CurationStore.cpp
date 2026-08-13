#include "core/CurationStore.h"

#include <QDebug>

namespace {

const QString kHiddenGroup = QStringLiteral("curationHidden");
const QString kShownGroup  = QStringLiteral("curationShown");

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
