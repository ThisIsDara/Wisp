#pragma once

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QString>

class QEvent;
class QPropertyAnimation;
class QQmlEngine;
class QQuickWindow;
class QTimer;
class AutostartManager;
class HotkeyCaptureDialog;
class HotkeyManager;
class SettingsStore;

// SYS-03 / D-01: the settings surface controller — a small QML host
// (HotkeyCaptureDialog analog, RESEARCH Pattern 3). Owns the
// SettingsWindow.qml + ColorDialog.qml QQuickWindows, injects itself as the
// `settingsController` context property (per-instance beginCreate/setProperty
// — capture-dialog precedent), and owns the D-02 dismissal contract:
// Esc → instant hide; click-away → deactivation-hide with a 150ms grace —
// EXEMPT when the newly-active window is one of OUR OWN top-level windows
// (launcher pop-over, D-04; modal color dialog — RESEARCH Pitfall 4).
// Show sequence: center on primary screen (re-applied EVERY show, UI-SPEC
// Geometry contract) → 120ms fade-in (Theme.animFade) → requestActivate.
// NOT an extension of HotkeyCaptureDialog, NOT Qt Widgets (D-01).
class SettingsWindow : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentHotkey READ currentHotkey NOTIFY currentHotkeyChanged)
    Q_PROPERTY(bool autostartEnabled READ autostartEnabled NOTIFY autostartEnabledChanged)

public:
    // All collaborators are injected (no store reach-in, no service lookup):
    // the shared engine, the three stores, and the EXISTING capture dialog
    // (main.cpp owns it — the hotkey-row click reopens it unchanged).
    explicit SettingsWindow(QQmlEngine *engine, SettingsStore *settingsStore,
                            AutostartManager *autostart, HotkeyManager *hotkeys,
                            HotkeyCaptureDialog *capture, QObject *parent = nullptr);

    // Center → fade-in (120ms) → requestActivate; refreshes autostart +
    // hotkey state first (D-10: state read when settings opens).
    void open();

    // Instant hide (no close animation — UI-SPEC hard rule 2); the window is
    // hidden, never destroyed (state persists across opens).
    void close();

    // ── QML surface API (SettingsWindow.qml + ColorDialog.qml, 06-02) ──
    Q_INVOKABLE QString currentHotkey() const;            // HotkeyManager live combo
    Q_INVOKABLE bool autostartEnabled() const;            // AutostartManager live state
    Q_INVOKABLE void applyAccent(const QColor &c);        // → SettingsStore::setAccent (D-06)
    Q_INVOKABLE void commitCustomColor(const QString &hex); // staged-dialog commit (T-06-01)
    Q_INVOKABLE void toggleAutostart();                   // → AutostartManager::setEnabled(!isEnabled)
    Q_INVOKABLE void openHotkeyCapture();                 // → existing HotkeyCaptureDialog
    Q_INVOKABLE void openColorDialog();                   // staged ColorDialog (06-02)

signals:
    void currentHotkeyChanged();
    void autostartEnabledChanged();
    void settingsVisibleChanged(bool visible);

protected:
    // Window-level Esc → close() (the QML surface has no key handler of its
    // own — dismissal is controller-owned per 06-02).
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QQuickWindow *ensureWindow();              // lazy-load SettingsWindow.qml + inject
    void centerOnPrimary(QQuickWindow *win);   // UI-SPEC Geometry — every show
    void startGraceTimer();
    void onGraceTimeout();
    bool anotherOfOurWindowsIsActive() const;  // launcher / color dialog exemption

    QQmlEngine *m_engine;
    SettingsStore *m_settingsStore;
    AutostartManager *m_autostart;
    HotkeyManager *m_hotkeys;
    HotkeyCaptureDialog *m_capture;
    QPointer<QQuickWindow> m_window;
    QPointer<QQuickWindow> m_colorDialog;
    QTimer *m_graceTimer;
    QPropertyAnimation *m_fade;
    // Open-race guard: the click-away grace arms only AFTER the first
    // activation. A deactivation before that (tray-menu close / focus
    // handoff racing the show) would otherwise close a freshly opened
    // window 150ms later — the user sees a flash, at best (2026-08-12).
    bool m_hasBeenActive = false;
};
