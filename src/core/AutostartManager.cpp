#include "core/AutostartManager.h"

#include <QCoreApplication>

namespace {

// The registry wrapper path (STACK.md): QSettings NativeFormat pointed at the
// HKCU Run key is literally a registry editor — the same API as the settings
// INI stores. A factory mirrors LaunchHistory::makeSettings: empty path -> the
// real per-user Run key; non-empty -> the tst_autostart test seam. QSettings
// is neither copyable nor movable, so the factory returns a prvalue
// (guaranteed elision) for the non-copyable member.
QSettings makeRunKey(const QString &registryKeyPath)
{
    if (registryKeyPath.isEmpty())
        return QSettings(QStringLiteral(
                             "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                         QSettings::NativeFormat);
    return QSettings(registryKeyPath, QSettings::NativeFormat);
}

const QString kRunValueName = QStringLiteral("wisp");

} // namespace

AutostartManager::AutostartManager(const QString &registryKeyPath, QObject *parent)
    : QObject(parent)
    , m_settings(makeRunKey(registryKeyPath))
{
}

bool AutostartManager::isEnabled() const
{
    // D-16: missing value -> false, silently. A value that exists but was
    // malformed is still "enabled" per the SYS-02 contract (the OS runs it);
    // only the toggle writes it, and the toggle only ever writes the D-12
    // quoted form (T-06-01).
    return m_settings.contains(kRunValueName);
}

void AutostartManager::setEnabled(bool enabled)
{
    if (enabled) {
        // D-12 exact value: "\"<exe>\" --autostart" — quoted path from Qt
        // (never user input, T-06-01) + space + --autostart, enforced by
        // construction.
        const QString value = QLatin1Char('"')
            + QCoreApplication::applicationFilePath() + QStringLiteral("\" --autostart");
        m_settings.setValue(kRunValueName, value);
    } else {
        m_settings.remove(kRunValueName);
    }
    m_settings.sync();
}
