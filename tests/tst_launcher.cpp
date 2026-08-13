#include <QtTest>

#include "core/LauncherController.h"
#include "win/WinFullscreenGuard.h"

// Window-light controller tests: no QQuickWindow is ever set — the policy
// layer runs against a null window (LauncherController contract), with the
// fullscreen guard injected as a counting fake. No real OS hotkey or window
// is registered anywhere in this suite.

class TstLauncher : public QObject
{
    Q_OBJECT

private slots:
    void toggleShowsWhenHidden();
    void fullscreenDefersShow_HOTK04();
    void clickawayGraceWindow();
    void hideNowInstant();
    void hideAnimatedTracksState();
    void guardRecheckOnShow();
    void showUserRequestedBypassesGuard_D023();
};

void TstLauncher::toggleShowsWhenHidden()
{
    LauncherController ctrl;
    QCOMPARE(ctrl.state(), LauncherController::Hidden);

    int guardCalls = 0;
    ctrl.setFullscreenGuard([&guardCalls] {
        ++guardCalls;
        return WinFullscreenGuard::AcceptsNotifications;
    });

    ctrl.toggle();
    QCOMPARE(ctrl.state(), LauncherController::Visible);
    QCOMPARE(guardCalls, 1); // guard consulted exactly once on the show path
}

void TstLauncher::fullscreenDefersShow_HOTK04()
{
    LauncherController ctrl;
    int guardCalls = 0;
    ctrl.setFullscreenGuard([&guardCalls] {
        ++guardCalls;
        return WinFullscreenGuard::FullscreenActive;
    });

    ctrl.toggle();
    QCOMPARE(ctrl.state(), LauncherController::Hidden); // deferred — NOT shown
    QCOMPARE(guardCalls, 1);                            // guard was consulted

    // canShow() mirrors the guard verdict.
    QVERIFY(!ctrl.canShow());
}

void TstLauncher::clickawayGraceWindow()
{
    LauncherController ctrl;
    ctrl.setFullscreenGuard([] { return WinFullscreenGuard::AcceptsNotifications; });
    ctrl.toggle();
    QCOMPARE(ctrl.state(), LauncherController::Visible);

    // Deactivation starts the 150ms grace timer; the window stays "inactive"
    // (no real window → treated as not re-activated) → hide after timeout.
    ctrl.onWindowActiveChanged(false);
    QTest::qWait(200); // 150ms grace + margin — needs the event loop for QTimer
    QCOMPARE(ctrl.state(), LauncherController::Hidden);

    // Re-activation within the grace window cancels the dismissal.
    ctrl.toggle();
    QCOMPARE(ctrl.state(), LauncherController::Visible);
    ctrl.onWindowActiveChanged(false);
    ctrl.onWindowActiveChanged(true); // focus back before the 150ms elapse
    QTest::qWait(200);
    QCOMPARE(ctrl.state(), LauncherController::Visible);
}

void TstLauncher::hideNowInstant()
{
    LauncherController ctrl;
    ctrl.setFullscreenGuard([] { return WinFullscreenGuard::AcceptsNotifications; });
    ctrl.toggle();
    QCOMPARE(ctrl.state(), LauncherController::Visible);

    ctrl.hideNow();
    QCOMPARE(ctrl.state(), LauncherController::Hidden);

    // hideNow on an already-hidden controller is a harmless no-op.
    ctrl.hideNow();
    QCOMPARE(ctrl.state(), LauncherController::Hidden);

    // No grace timer may run once hidden: a deactivation event on a hidden
    // controller must not start anything (nothing to dismiss).
    ctrl.onWindowActiveChanged(false);
    QTest::qWait(200);
    QCOMPARE(ctrl.state(), LauncherController::Hidden);
}

void TstLauncher::hideAnimatedTracksState()
{
    LauncherController ctrl;
    ctrl.setFullscreenGuard([] { return WinFullscreenGuard::AcceptsNotifications; });
    ctrl.toggle();
    QCOMPARE(ctrl.state(), LauncherController::Visible);

    ctrl.hideAnimated();
    QCOMPARE(ctrl.state(), LauncherController::Hidden);
}

void TstLauncher::guardRecheckOnShow()
{
    LauncherController ctrl;
    ctrl.setFullscreenGuard([] { return WinFullscreenGuard::AcceptsNotifications; });
    ctrl.toggle();
    QCOMPARE(ctrl.state(), LauncherController::Visible);

    // Guard flips to fullscreen while the launcher is visible — hides are
    // state-only and must not consult the guard (the guard gates SHOW only).
    ctrl.setFullscreenGuard([] { return WinFullscreenGuard::FullscreenActive; });
    ctrl.hideNow();
    QCOMPARE(ctrl.state(), LauncherController::Hidden);
}

void TstLauncher::showUserRequestedBypassesGuard_D023()
{
    LauncherController ctrl;
    int guardCalls = 0;
    ctrl.setFullscreenGuard([&guardCalls] {
        ++guardCalls;
        return WinFullscreenGuard::FullscreenActive;
    });

    // Fullscreen active + explicit user intent → show anyway (D-02.3 scope:
    // the guard protects only passive hotkey summons).
    ctrl.showUserRequested();
    QCOMPARE(ctrl.state(), LauncherController::Visible);
    QVERIFY(guardCalls >= 1); // guard still consulted — verdict ignored
}

QTEST_MAIN(TstLauncher)
#include "tst_launcher.moc"