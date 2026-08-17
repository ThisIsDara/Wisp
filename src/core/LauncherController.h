#pragma once

#include <QObject>
#include <QPointer>
#include <functional>

#include "win/WinFullscreenGuard.h"

class QQuickWindow;
class QTimer;

// Visibility/dismissal policy for the resident launcher (02-02):
// hotkey toggle with fullscreen defer (HOTK-04), deactivation-based click-away
// grace (D-02.4), animated hide (Escape) vs instant hideNow() (launch dismiss,
// HOTK-03). No Win32, no QML dependency — a null window keeps it purely
// unit-testable; every policy branch is fake-injectable.
class LauncherController : public QObject
{
    Q_OBJECT

public:
    enum State { Hidden, Visible };

    explicit LauncherController(QObject *parent = nullptr);

    State state() const { return m_state; }

    // The window the controller commands. May stay null in unit tests
    // (window-light: policy still runs, window calls are no-ops).
    void setWindow(QQuickWindow *win);

    // Inject a fullscreen-state probe; default = live QUNS query.
    void setFullscreenGuard(std::function<WinFullscreenGuard::State()> guard);

    // 07-06 seam: the empty-state "Select a folder to scan" action. The
    // WHOLE flow (native picker → SettingsStore append → ScanService
    // requestScan) is wired in main.cpp — QFileDialog lives in QtWidgets,
    // which wisp_core never links (FileSearch::setAddExeDialog precedent).
    // Default = no-op → the QML row does nothing until main.cpp wires it.
    using ScanFolderAdder = std::function<void()>;
    void setScanFolderAdder(ScanFolderAdder fn);

    // QML entry point (MainWindow.qml empty state): pick a folder to scan.
    Q_INVOKABLE void addScanRoot();

    // 2026-08-15: settings-opener seam — same pattern as setScanFolderAdder.
    // The footer-row gear button calls openSettings(); main.cpp wires it to
    // SettingsWindow::open (previously tray-only, D-04 — user redesign).
    using SettingsOpener = std::function<void()>;
    void setSettingsOpener(SettingsOpener fn);

    // QML entry point (MainWindow.qml footer row): open the settings surface.
    Q_INVOKABLE void openSettings();

    // True when the passive hotkey path may show: fullscreen content defers
    // (HOTK-04 / D-02.3). AcceptsNotifications and Other are showable.
    bool canShow() const;

    // Guard-checked show + locked focus sequence show → raise →
    // requestActivate (deferred) — CONTEXT.md, taken verbatim.
    void show();

    // Explicit user intent (tray "Open wisp", 02-03): shows regardless of the
    // guard (D-02.3). Guard is still consulted but its verdict is ignored —
    // the guard protects only passive hotkey summons.
    void showUserRequested();

    // Animated hide via the QML close path (Escape / click-away).
    void hideAnimated();

    // Instant hide, no animation (HOTK-03 launch dismissal; Phase 3 API).
    void hideNow();

    // Hidden → show() (guard-checked); Visible → hideAnimated() (HOTK-01).
    void toggle();

public slots:
    // Window deactivation starts the 150ms grace timer; re-activation within
    // the window cancels it. Timeout with the window still inactive → hide
    // (click-away, D-02.4).
    void onWindowActiveChanged(bool active);

    // Diagnostic event trail (2026-08-11): appends an ISO-timestamped tag to
    // %TEMP%\wisp-events.log. Traces the QML key/ticker lifecycle (release
    // delivery, clamp/pin states) — UI-thread calls only, no-op safe.
    void stateNote(const QString &tag);

private:
    void showWindow();

    State m_state = Hidden;
    QPointer<QQuickWindow> m_win;
    std::function<WinFullscreenGuard::State()> m_guard = &WinFullscreenGuard::currentState;
    ScanFolderAdder m_scanFolderAdder;
    SettingsOpener m_settingsOpener;
    QTimer *m_graceTimer;
};