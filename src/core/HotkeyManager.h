#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>

class QSettings;
class WinHotkey;

// Settings-backed global-hotkey lifecycle (D-02.5): loads from INI, registers,
// rejects invalid combos (F12 / modifier-only), hot-re-registers on change.
// Owns the WinHotkey Win32 firewall; both die together (UnregisterOnQuit —
// RESEARCH.md §1, PITFALLS #1).
class HotkeyManager : public QObject
{
    Q_OBJECT

public:
    explicit HotkeyManager(const QString &settingsPath = {}, QObject *parent = nullptr);
    ~HotkeyManager() override;

    // Load the persisted sequence (default "Alt+Space") and register it
    // globally. Returns true when the combo is live; on failure emits
    // registrationFailed() first (HOTK-02 — never silent).
    bool start();

    QKeySequence hotkey() const;

    // Validate, persist, and atomically swap the registered combo. Empty /
    // modifier-only / F12 sequences are rejected with registrationFailed()
    // and the current combo stays untouched; on an OS conflict the OLD
    // combo remains registered.
    void setHotkey(const QKeySequence &seq);

    // Capture-dialog support: release the global combo while the capture
    // dialog is open so the combo being captured (e.g. the current one,
    // Alt+Space) is NOT swallowed by our own system-wide registration —
    // otherwise WM_HOTKEY fires and the keystroke never reaches QML.
    // resume() re-registers the current combo (availability-left: on an OS
    // conflict the pre-capture combo is restored and registrationFailed
    // fires — never silent).
    void suspend();
    void resume();

signals:
    void hotkeyPressed();
    void registrationFailed(const QString &combo);
    void hotkeyChanged(const QKeySequence &seq);

private:
    WinHotkey *m_winHotkey;
    QSettings *m_settings;
    QKeySequence m_hotkey;
    QKeySequence m_preSuspendCombo;
    bool m_suspended = false;
};