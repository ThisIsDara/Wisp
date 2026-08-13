#include "core/SettingsStore.h"

namespace {

// PATTERNS Shared Pattern 1 — LaunchHistory.cpp:24-30 VERBATIM: IniFormat +
// UserScope + "TID"/"wisp" → %APPDATA%\TID\wisp\wisp.ini; an explicit path
// is the QTemporaryDir test seam. A factory is required because QSettings
// is neither copyable nor movable — a conditional-expression member-init
// would need a deleted copy (C2280); returning a prvalue uses guaranteed
// elision instead (LaunchHistory precedent, Phase 04 lesson).
QSettings makeSettings(const QString &settingsPath)
{
    if (settingsPath.isEmpty())
        return QSettings(QSettings::IniFormat, QSettings::UserScope,
                         QStringLiteral("TID"), QStringLiteral("wisp"));
    return QSettings(settingsPath, QSettings::IniFormat);
}

} // namespace

SettingsStore::SettingsStore(const QString &settingsPath, QObject *parent)
    : QObject(parent)
    , m_settings(makeSettings(settingsPath))
{
}

QColor SettingsStore::accent() const
{
    // Live read — always fresh (an external INI edit shows up on the next
    // read); the QML binding consumes once at startup + via accentChanged.
    return readAccent();
}

void SettingsStore::setAccent(const QColor &c)
{
    if (!c.isValid())
        return; // D-16: an invalid color is silently ignored, no notify
    // c.name(): canonical "#rrggbb", or "#aarrggbb" when alpha < 255 — full
    // fidelity for Phase 6's picker; round-trips through QColor::fromString.
    m_settings.setValue(QStringLiteral("theme/accent"), c.name());
    m_settings.sync(); // LaunchHistory.cpp:48/58 discipline — sync after EVERY write
    emit accentChanged(c);
}

QColor SettingsStore::readAccent() const
{
    // D-16 semantics: missing key, corrupt string, and unparseable color ALL
    // silently return the default (no warnings, no toasts). Stored values are
    // canonical c.name() forms, so fromString succeeds on everything we write.
    const QString raw = m_settings.value(QStringLiteral("theme/accent"),
                                         QStringLiteral("#0078D4")).toString();
    QColor c = QColor::fromString(raw);
    return c.isValid() ? c : QColor(QStringLiteral("#0078D4"));
}
