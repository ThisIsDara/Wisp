#pragma once
#include <QSet>
#include <QSettings>
#include <QString>

// 2026-08-15: persisted per-row favorites (the "Favorites" tab). Persists to
// the existing wisp INI under the ADDITIVE group "favorites" (id → 1) via the
// makeSettings factory (CurationStore precedent, PATTERNS P1). Identity
// contract mirrors curation: Lnk/added → targetPath, UWP → aumid, File rows →
// their targetPath. An id whose app/path vanished is inert (nothing matches
// it on the next build). setFavorite(id, false) removes the key.
//
// THREADING CONTRACT: UI-thread-only (CurationStore precedent — NO mutex; if
// a worker ever reads this store, add the LaunchHistory WR-01 QMutex
// discipline first).
class FavoritesStore {
public:
    explicit FavoritesStore(const QString &settingsPath = {}); // "" → TID/wisp INI
    void setFavorite(const QString &id, bool favorite); // "favorites/{id}" = 1, or remove + sync()
    QSet<QString> favoriteIds() const;                  // missing group → empty set, silent (D-16)
private:
    QSettings m_settings;           // non-copyable member (LaunchHistory precedent)
};