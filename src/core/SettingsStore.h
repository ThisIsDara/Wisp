#pragma once
#include <QColor>
#include <QObject>
#include <QSettings>

// Single source of truth for the accent value (D-13/D-14). Persists to the
// existing wisp INI (%APPDATA%\TID\wisp\wisp.ini) under the non-colliding
// key "theme/accent" via the LaunchHistory makeSettings factory (PATTERNS
// Shared Pattern 1). All accent usages (selection bg, left bar, highlight,
// chips) bind to Theme.accent, which 05-05 feeds from this store at startup;
// Phase 6's accent picker calls setAccent() — the NOTIFY path is built and
// tested now (research Pattern 4 sketch; D-15 derivation lives in Theme.qml).
//
// THREADING CONTRACT (Pitfall 7): UI-thread-only. accent() is read once at
// startup (QML Component.onCompleted) and setAccent() is called from the
// Phase-6 picker on the UI thread. Unlike LaunchHistory (WR-01), no worker
// touches this store, so there is NO mutex — if a worker ever needs the
// accent, add the LaunchHistory QMutex discipline before doing so.
//
// Silent fallback (D-16): a missing, corrupt, or unparseable value returns
// #0078D4 with no warnings and no toasts.
class SettingsStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor accent READ accent NOTIFY accentChanged)

public:
    // Empty settingsPath → the default UserScope TID/wisp INI via the
    // makeSettings factory; non-empty → the tst_settings QTemporaryDir seam.
    explicit SettingsStore(const QString &settingsPath = {}, QObject *parent = nullptr);

    // Live read via readAccent() — always fresh, no staleness after external
    // edits; the QML binding consumes the value once at startup + via signal.
    QColor accent() const;

    // Phase-6 picker entry point: persist + sync + notify. Invalid colors
    // are silently ignored (D-16).
    Q_INVOKABLE void setAccent(const QColor &c);

signals:
    void accentChanged(const QColor &accent);

private:
    QColor readAccent() const;
    QSettings m_settings; // non-copyable member (QSettings) — class is move-less by design (LaunchHistory precedent)
};
