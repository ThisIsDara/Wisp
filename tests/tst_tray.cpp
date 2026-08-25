#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QtTest>

#include "tray/TrayIcon.h"

// TrayIcon menu + signal wiring in the offscreen platform: no real tray is
// touched, but QSystemTrayIcon's menu/actions and signal plumbing are real.
class TstTray : public QObject
{
    Q_OBJECT

private slots:
    void menuActionsAndSignals();
    void settingsActionWired();
    void accentSetterKeepsMenuIntact();
    void conflictNotificationSmoke();
};

void TstTray::menuActionsAndSignals()
{
    TrayIcon tray;
    tray.show(); // offscreen — must not crash, must not require a real tray

    // The QMenu is owned by TrayIcon (parent-free — widgets cannot be
    // object-parented to non-widgets); reached via the accessor.
    QMenu *menu = tray.menu();
    QVERIFY(menu);
    // Locked order (D-03): Open wisp / Settings / Change hotkey… / sep / Quit.
    // / sep / Quit - Phase 8 inserts a HIDDEN pending-update entry between
    // the locked top block and the separator (visible only when pending).
    const QStringList labels = { QStringLiteral("Open wisp"),
                                 QStringLiteral("Settings"),
                                 QStringLiteral("Change hotkey…"),
                                 QStringLiteral("Download update"),
                                 QStringLiteral("Quit") };
    auto actions = menu->actions();
    QCOMPARE(actions.size(), labels.size() + 1); // + separator before Quit
    QCOMPARE(actions.at(0)->text(), labels.at(0));
    QCOMPARE(actions.at(1)->text(), labels.at(1));
    QCOMPARE(actions.at(2)->text(), labels.at(2));
    QVERIFY(actions.at(3)->text().startsWith(QStringLiteral("Download update")));
    QVERIFY(!actions.at(3)->isVisible());        // hidden until update pending (D-03)
    QVERIFY(actions.at(4)->isSeparator());
    QCOMPARE(actions.at(5)->text(), labels.at(4));

    // Signal wiring: each action triggers its TrayIcon signal.
    QSignalSpy openSpy(&tray, &TrayIcon::openWisp);
    QSignalSpy changeSpy(&tray, &TrayIcon::changeHotkeyRequested);
    QSignalSpy quitSpy(&tray, &TrayIcon::quitRequested);
    QSignalSpy dlSpy(&tray, &TrayIcon::updateDownloadRequested);

    actions.at(0)->trigger();
    QCOMPARE(openSpy.count(), 1);

    actions.at(2)->trigger();
    QCOMPARE(changeSpy.count(), 1);

    actions.at(5)->trigger();
    QCOMPARE(quitSpy.count(), 1);

    // setUpdatePending(true) reveals the item with the version label (D-03).
    tray.setUpdatePending(true, QStringLiteral("0.1.3"));
    QVERIFY(actions.at(3)->isVisible());
    actions.at(3)->trigger();
    QCOMPARE(dlSpy.count(), 1);
    tray.setUpdatePending(false, QString());
    QVERIFY(!actions.at(3)->isVisible());
}

void TstTray::settingsActionWired()
{
    TrayIcon tray;
    tray.show();

    QMenu *menu = tray.menu();
    QVERIFY(menu);
    auto actions = menu->actions();
    QCOMPARE(actions.size(), 6); // 4 actions + hidden update entry + separator

    // "Settings" sits between "Open wisp" and "Change hotkey…" and emits
    // settingsRequested when triggered.
    QCOMPARE(actions.at(0)->text(), QStringLiteral("Open wisp"));
    QCOMPARE(actions.at(1)->text(), QStringLiteral("Settings"));
    QCOMPARE(actions.at(2)->text(), QStringLiteral("Change hotkey…"));

    QSignalSpy settingsSpy(&tray, &TrayIcon::settingsRequested);
    actions.at(1)->trigger();
    QCOMPARE(settingsSpy.count(), 1);
}

void TstTray::accentSetterKeepsMenuIntact()
{
    TrayIcon tray;
    tray.show();

    // setAccent must repaint the disc without disturbing the menu structure
    // (disc pixel check optional — assert no crash + icon set).
    tray.setAccent(QColor("#2EA043"));

    // QSystemTrayIcon was created parented to the TrayIcon — reachable via
    // findChild; the icon must still be set after the repaint.
    QSystemTrayIcon *icon = tray.findChild<QSystemTrayIcon *>();
    QVERIFY(icon);
    QVERIFY(!icon->icon().isNull());

    QMenu *menu = tray.menu();
    QVERIFY(menu);
    auto actions = menu->actions();
    QCOMPARE(actions.size(), 6); // 4 actions + hidden update entry + separator
    QCOMPARE(actions.at(1)->text(), QStringLiteral("Settings"));

    // Invalid accent is silently ignored (D-16) — no crash, menu intact.
    tray.setAccent(QColor());
    QCOMPARE(tray.menu()->actions().size(), 6);
}

void TstTray::conflictNotificationSmoke()
{
    TrayIcon tray;
    tray.show();
    // HOTK-02 path: must complete without crashing under offscreen (the real
    // balloon needs a live tray; the wiring itself is exercised in the app).
    tray.notifyHotkeyConflict(QStringLiteral("Alt+Space"));
    QTest::qWait(50);
}

int main(int argc, char *argv[])
{
    // Offscreen BEFORE QApplication so CI/headless runs never spawn a tray.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TstTray tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_tray.moc"
