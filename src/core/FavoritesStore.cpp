#include "core/FavoritesStore.h"

#include <QDebug>

namespace {

const QString kGroup = QStringLiteral("favorites");

// 05.1 review (L-04): INI-hostile characters — QSettings' INI parser treats
// '=' as the key/value separator and '[' ']' as group delimiters; an id
// containing them would truncate or corrupt the registry line. Windows paths
// CAN legally contain '='. Such ids are skipped: the row still favorites
// in-session (model set membership), only persistence is forfeited — safer
// than corrupting the store.
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

} // namespace

FavoritesStore::FavoritesStore(const QString &settingsPath)
    : m_settings(makeSettings(settingsPath))
{
}

void FavoritesStore::setFavorite(const QString &id, bool favorite)
{
    if (id.isEmpty())
        return; // identity contract — never write a key without an id
    if (!isIniSafe(id))
        return;
    const QString key = kGroup + QLatin1Char('/') + id;
    if (favorite)
        m_settings.setValue(key, 1);
    else
        m_settings.remove(key);
    m_settings.sync(); // PATTERNS P2 — sync after EVERY write
    if (m_settings.status() != QSettings::NoError)
        qWarning() << "FavoritesStore: write failed for" << id;
}

QSet<QString> FavoritesStore::favoriteIds() const
{
    const QString prefix = kGroup + QLatin1Char('/');
    const QStringList keys = m_settings.allKeys();
    QSet<QString> ids;
    for (const QString &key : keys)
        if (key.startsWith(prefix))
            ids.insert(key.mid(prefix.size()));
    return ids;
}