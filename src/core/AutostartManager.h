#pragma once

#include <QObject>
#include <QSettings>

// HKCU Run-key store (D-10/D-12, SYS-02): "start with Windows" state lives in
// the registry value HKCU\Software\Microsoft\Windows\CurrentVersion\Run\wisp
// with the EXACT quoted value "\"<exe>\" --autostart" (D-12). QSettings
// NativeFormat IS the registry wrapper (STACK.md) — no raw RegSetValueEx
// (RESEARCH "Don't Hand-Roll").
//
// THREADING CONTRACT: UI-thread-only (SettingsStore discipline) — no mutex,
// no worker access. The settings window reads isEnabled() when it opens and
// writes setEnabled() on toggle.
//
// T-06-01 (tampering): the value is built from
// QCoreApplication::applicationFilePath() — never user input — and the quotes
// are enforced by construction. HKCU only, never HKLM.
class AutostartManager : public QObject
{
    Q_OBJECT

public:
    // Empty registryKeyPath -> the real HKCU Run key (NativeFormat); non-empty
    // -> the tst_autostart test seam (scoped test-only key).
    explicit AutostartManager(const QString &registryKeyPath = {},
                              QObject *parent = nullptr);

    // True iff the "wisp" value exists in the Run key. Missing/invalid ->
    // false (silent — D-16 discipline: no warnings, no toasts).
    bool isEnabled() const;

    // enable: setValue("wisp", "\"<exe>\" --autostart") then sync() — the
    // exact D-12 format (quoted path + space + --autostart; never unquoted,
    // never a bare path). disable: remove("wisp") then sync().
    void setEnabled(bool enabled);

private:
    QSettings m_settings; // non-copyable member — class is move-less by design
};
