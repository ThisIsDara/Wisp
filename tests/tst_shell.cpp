#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QColor>
#include <QTemporaryDir>

#include "core/AutostartManager.h"
#include "core/HotkeyManager.h"
#include "core/ScanService.h"
#include "core/SettingsStore.h"
#include "ui/HotkeyCaptureDialog.h"
#include "ui/SettingsWindow.h"

namespace {

QQuickWindow *findWindowByTitle(const QString &title)
{
    for (QWindow *w : QGuiApplication::topLevelWindows()) {
        auto *qw = qobject_cast<QQuickWindow *>(w);
        if (qw && qw->title() == title)
            return qw;
    }
    return nullptr;
}

} // namespace

class ShellTest : public QObject
{
    Q_OBJECT

private slots:
    void windowContract();
    void themeTokens();
    void settingsWindowContract();
};

void ShellTest::windowContract()
{
    QQmlApplicationEngine engine;
    engine.loadFromModule("wisp", "MainWindow");
    QVERIFY2(!engine.rootObjects().isEmpty(), "MainWindow must load from module wisp");
    QVERIFY2(engine.rootObjects().first(), "root object must exist");

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY2(window, "root object must be a QQuickWindow");

    QVERIFY2(window->flags().testFlag(Qt::Tool), "flags must include Qt::Tool (no taskbar button)");
    QVERIFY2(window->flags().testFlag(Qt::FramelessWindowHint), "flags must include Qt::FramelessWindowHint");

    QCOMPARE(window->width(), 680);     // Theme window canvas: surface 648 + 2x16 shadow margin
    QCOMPARE(window->height(), 472);
    QCOMPARE(window->title(), QStringLiteral("wisp"));
    QCOMPARE(window->color(), QColor(Qt::transparent));
}

void ShellTest::themeTokens()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import wisp\n"
        "import QtQuick\n"
        "QtObject {\n"
        "    property color surface: Theme.surface\n"
        "    property int animOpenDuration: Theme.animOpenDuration\n"
        "    property int animCloseDuration: Theme.animCloseDuration\n"
        "    property int easingOpen: Theme.easingOpen\n"
        "    property int rowHeight: Theme.rowHeight\n"
        "    property int radiusSurface: Theme.radiusSurface\n"
        "    property int windowWidth: Theme.windowWidth\n"
        "}\n",
        QUrl(QStringLiteral("qrc:/qt/qml/wisp/tst_shell_probe.qml")));

    QVERIFY2(component.isReady() || component.isError(),
             "Theme probe component must compile");
    if (component.isError()) {
        for (const auto &e : component.errors())
            qWarning() << e.toString();
        QFAIL("Theme probe component failed to compile");
    }

    QScopedPointer<QObject> obj(component.create());
    QVERIFY2(obj, "Theme probe must instantiate");

    QCOMPARE(obj->property("surface").value<QColor>(), QColor("#000000"));
    QCOMPARE(obj->property("animOpenDuration").toInt(), 150);
    QCOMPARE(obj->property("animCloseDuration").toInt(), 140);
    QCOMPARE(obj->property("easingOpen").toInt(), int(QEasingCurve::OutCubic));   // 6 in Qt 6.11
    QCOMPARE(obj->property("rowHeight").toInt(), 44);
    QCOMPARE(obj->property("radiusSurface").toInt(), 0);
    QCOMPARE(obj->property("windowWidth").toInt(), 680);
}

void ShellTest::settingsWindowContract()
{
    // 2026-08-12 regression: the phase-06 verification only inspected
    // SettingsWindow.qml ("token-only") — the surface was never actually
    // opened. Contract under test:
    //  - the settings window LOADS, shows, closes (controller open/close)
    //  - CR-01: the hotkey-row handoff surfaces the capture dialog
    //  - CR-02: a second handoff RE-SHOWS the same dialog (never single-use)
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    QQmlApplicationEngine engine;
    engine.loadFromModule("wisp", "MainWindow"); // module registration (app parity)
    QVERIFY2(!engine.rootObjects().isEmpty(), "MainWindow must load from module wisp");

    SettingsStore store(iniPath);
    AutostartManager autostart; // read-only here — never toggled
    HotkeyManager hotkeys(iniPath); // never started — no OS registration
    HotkeyCaptureDialog capture(&engine);
    ScanService scanService; // unwired — no pool/listFn; never started (no scans)
    SettingsWindow settings(&engine, &store, &autostart, &hotkeys, &capture, &scanService);

    settings.open();
    QTest::qWait(250); // 120ms fade + activation handshake
    QQuickWindow *settingsWin = findWindowByTitle(QStringLiteral("wisp — settings"));
    QVERIFY2(settingsWin, "settings window must exist");
    QVERIFY2(settingsWin->isVisible(), "settings window must be visible after open()");
    QCOMPARE(settingsWin->width(), 480);   // UI-SPEC geometry
    QCOMPARE(settingsWin->height(), 736); // Phase 8: +176 Updates section (bar+hint)
    QCOMPARE(settings.currentHotkey(), hotkeys.hotkey().toString());
    // The injected controller must reach the QML side: the currentHotkey
    // binding reads settingsController (readonly setProperty is a silent
    // no-op — 2026-08-12 regression gate).
    QCOMPARE(settingsWin->property("currentHotkey").toString(), hotkeys.hotkey().toString());

    settings.close();
    QTest::qWait(250); // 120ms close fade-out (Theme.animFade) + margin
    QVERIFY2(!settingsWin->isVisible(), "settings window must hide after the close fade");

    // CR-01: the hotkey row calls openHotkeyCapture → the capture dialog shows.
    // Invoke the QML-SIDE function (the bridge: QML → injected controller →
    // capture.open) — proves the injection landed, not just the C++ path.
    QVERIFY2(QMetaObject::invokeMethod(settingsWin, "openHotkeyCapture"),
             "QML hotkey-row function must be invokable");
    QTest::qWait(100);
    QQuickWindow *captureWin = findWindowByTitle(QStringLiteral("wisp — change hotkey"));
    QVERIFY2(captureWin, "capture dialog must exist after hotkey-row handoff");
    QVERIFY2(captureWin->isVisible(), "capture dialog must be visible after handoff");

    // CR-02: reuse — hiding and re-opening must work repeatedly.
    captureWin->hide();
    QTest::qWait(50);
    settings.openHotkeyCapture();
    QTest::qWait(100);
    QVERIFY2(captureWin->isVisible(), "capture dialog must REOPEN on the second handoff");
}

QTEST_MAIN(ShellTest)
#include "tst_shell.moc"
