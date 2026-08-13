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
private:
    QSettings m_settings;           // non-copyable member (LaunchHistory precedent)
};
