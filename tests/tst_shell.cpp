#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QSignalSpy>
#include <QQuickWindow>
#include <QColor>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTemporaryDir>

#include <cmath>

#include "core/AutostartManager.h"
#include "core/HotkeyManager.h"
#include "core/ResultsModel.h"
#include "core/ScanService.h"
#include "core/SettingsStore.h"
#include "ui/HotkeyCaptureDialog.h"
#include "ui/SettingsWindow.h"

namespace {

// 2026-10-08: a resident settings surface keeps its QQuickWindow alive after
// close(), so a LATER test that builds a second one would get the previous
// test's stale hidden window here — the title matches, the contents don't, and
// the assertions quietly measure the wrong object (it cost a confusing 0-chips
// failure). Prefer a VISIBLE match; fall back to the first match so the
// "must hide after close" assertion still has something to find.
QQuickWindow *findWindowByTitle(const QString &title)
{
    QQuickWindow *fallback = nullptr;
    for (QWindow *w : QGuiApplication::topLevelWindows()) {
        auto *qw = qobject_cast<QQuickWindow *>(w);
        if (!qw || qw->title() != title)
            continue;
        if (qw->isVisible())
            return qw;
        if (!fallback)
            fallback = qw;
    }
    return fallback;
}

} // namespace

class ShellTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void windowContract();
    void themeTokens();
    void fitScaleContract();
    void settingsWindowContract();
    void settingsFoldersFillTheBlock_20261008();
    void duplicatesPanelContract();
};

// 2026-09-15: mirror the production control style (set in main.cpp before the
// engine loads). Without this the tests run under the native Windows style,
// where every customized contentItem/background is DISCARDED — so the shell
// tests would validate a rendering path the app never actually uses.
void ShellTest::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
}

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

void ShellTest::fitScaleContract()
{
    // 2026-09-15: Theme.fitScale() keeps a fixed-geometry window inside the
    // screen. The important property is not the exact number — it is that it
    // NEVER returns NaN/undefined. A NaN scale reaches QTransform::scale and
    // aborts the process ("ASSERT !std::isnan"), which is exactly how the
    // first implementation failed: Screen.availableWidth is UNDEFINED until
    // the window is attached to a screen, and NaN <= 0 is false, so a
    // magnitude-only guard let it through.
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(
        "import wisp\n"
        "import QtQuick\n"
        "QtObject {\n"
        "    function fitOk(w, h) {\n"
        "        var s = Theme.fitScale(w, h)\n"
        "        // NaN and undefined both fail this; so do non-positive scales.\n"
        "        return isFinite(s) && s > 0 && s <= 1\n"
        "    }\n"
        "    property bool normalInput: fitOk(480, 756)\n"
        "    property bool zeroWidth: fitOk(0, 756)\n"
        "    property bool zeroHeight: fitOk(480, 0)\n"
        "    property bool negative: fitOk(480, -756)\n"
        "    property bool tiny: fitOk(1, 1)\n"
        "    property bool huge: fitOk(100000, 100000)\n"
        "    property bool noArgs: fitOk(undefined, undefined)\n"
        "    property real scaleNow: Theme.fitScale(480, 756)\n"
        "}\n",
        QUrl(QStringLiteral("qrc:/qt/qml/wisp/tst_shell_fit_probe.qml")));

    QVERIFY2(component.isReady(), "fitScale probe component must compile");
    if (component.isError()) {
        for (const auto &e : component.errors())
            qWarning() << e.toString();
        QFAIL("fitScale probe component failed to compile");
    }
    QScopedPointer<QObject> obj(component.create());
    QVERIFY2(obj, "fitScale probe must instantiate");

    // Every shape of input must yield a usable (finite, > 0, <= 1) scale.
    QVERIFY2(obj->property("normalInput").toBool(), "480x756 must fit a usable screen");
    QVERIFY2(obj->property("zeroWidth").toBool(), "zero width must not divide by zero");
    QVERIFY2(obj->property("zeroHeight").toBool(), "zero height must not divide by zero");
    QVERIFY2(obj->property("negative").toBool(), "negative size must not produce a bad scale");
    QVERIFY2(obj->property("tiny").toBool(), "tiny window must not upscale past 1");
    QVERIFY2(obj->property("huge").toBool(), "oversized window must scale down, never NaN");
    QVERIFY2(obj->property("noArgs").toBool(), "undefined args must fall back to 1");

    // With no usable screen report (headless/never-shown window) the helper
    // must report the no-op scale of exactly 1, which is also what keeps the
    // token-geometry assertions above valid.
    const double scale = obj->property("scaleNow").toDouble();
    QVERIFY2(std::isfinite(scale), "scale must never be NaN/inf");
    QVERIFY2(scale > 0.0 && scale <= 1.0, "scale must be in (0, 1]");
}





void ShellTest::duplicatesPanelContract()
{
    // 2026-10-08: duplicate-app indicator + panel. End-to-end through QML,
    // because the wiring is the risky part and a compile-clean load proves
    // nothing about it: the chip's visibility is bound to
    // resultsModel.duplicateGroupCount, and the panel's delegates only ever
    // instantiate while it is OPEN, so neither path is exercised by loading.
    ResultsModel model;
    QSignalSpy dupSpy(&model, &ResultsModel::duplicatesChanged);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("resultsModel"), &model);
    engine.loadFromModule("wisp", "MainWindow");
    QVERIFY2(!engine.rootObjects().isEmpty(), "MainWindow must load from module wisp");

    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(win);
    win->show();
    // Wait for exposure so the window has a real surface before the chip/panel
    // bindings are asserted. The result is deliberately NOT asserted: headless
    // ctest runs are not guaranteed to expose, and the assertions below read
    // QML properties, which do not need it.
    (void)QTest::qWaitForWindowExposed(win);

    auto *chip = win->findChild<QQuickItem *>(QStringLiteral("dupChip"));
    auto *panel = win->findChild<QQuickItem *>(QStringLiteral("dupPanel"));
    auto *list = win->findChild<QQuickItem *>(QStringLiteral("dupPanelList"));
    QVERIFY2(chip, "footer duplicate indicator (objectName dupChip) must exist");
    QVERIFY2(panel, "duplicates overlay (objectName dupPanel) must exist");
    QVERIFY2(list, "duplicates list (objectName dupPanelList) must exist");

    // Assert the BINDING result (the "visible" property), not isVisible():
    // isVisible() is effective visibility and stays false unless the whole
    // window is exposed, which is not guaranteed in a headless ctest run. What
    // matters here is whether the chip's own visible expression follows the
    // model.
    const auto chipVisible = [chip] { return chip->property("visible").toBool(); };
    const auto panelVisible = [panel] { return panel->property("visible").toBool(); };

    // No duplicates -> the chip must stay out of the footer entirely.
    QVERIFY2(!chipVisible(), "dupChip must be hidden when there are no duplicates");
    QVERIFY2(!panelVisible(), "dupPanel must stay hidden until opened");

    // The real Start Menu shape that motivated the feature: an app linked twice,
    // once inside a vendor folder and once loose at the top level.
    AppEntry a;
    a.source = AppEntry::Source::File;
    a.displayName = QStringLiteral("Discord");
    a.targetPath = QStringLiteral("C:/Programs/Discord Inc/Discord.lnk");
    a.launchTarget = QStringLiteral("C:/Discord/Discord.exe");
    AppEntry b = a;
    b.targetPath = QStringLiteral("C:/Programs/Discord.lnk");
    model.setEntries({ a, b });

    QCOMPARE(model.duplicateGroupCount(), 1);
    QVERIFY2(dupSpy.count() > 0,
             "duplicatesChanged must fire, or the chip binding never re-evaluates");
    QVERIFY2(chipVisible(), "dupChip must appear once a duplicate group exists");
    QVERIFY2(!panelVisible(), "opening is an explicit click, not automatic");

    // Open it and confirm the panel actually populates — this is the only place
    // the Repeater-per-group delegate path gets built.
    win->setProperty("dupPanelOpen", true);
    QVERIFY2(panelVisible(), "dupPanel must show when dupPanelOpen is set");
    QCOMPARE(list->property("count").toInt(), 1);
    QVERIFY2(panel->width() > 0 && panel->height() > 0,
             "dupPanel must have real geometry, else its rows are invisible");

    // Hiding one member empties the panel again — the group leaves by itself.
    QVERIFY(model.hidePath(QStringLiteral("C:/Programs/Discord.lnk")));
    QVERIFY2(!chipVisible(), "chip must disappear once the last group is resolved");
    QCOMPARE(list->property("count").toInt(), 0);
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
    QCOMPARE(settingsWin->height(), 813); // breathing room: row gap 8, section gap 18, label 24 (was 756)

    // 2026-09-15 (settings regroup): the height assertions above only prove
    // the WINDOW is 480x756 — an exact-fit column that overflows just clips its
    // last row silently, which no window-size assertion can see. Assert the
    // content's implicitHeight against the space actually available: it must
    // fit, and stay near-exact so the surface keeps no dead space at the
    // bottom (the no-ScrollView growth-by-tokens contract).
    {
        auto *column = settingsWin->findChild<QQuickItem *>("settingsContentColumn");
        QVERIFY2(column, "settings content column must exist");
        // The column is anchored to fill, so height() is just the space it was
        // given; implicitHeight is what its children actually add up to. That is
        // the number that overflows (or wastes space) when tokens drift.
        const qreal contentH = column->implicitHeight();
        const qreal available = settingsWin->property("height").toReal()
                                - 2 * 16 /* shadow margin */
                                - 32 /* drag header */ - 8 /* top pad */ - 24 /* bottom pad */;
        qInfo().noquote() << "SETTINGS content implicitHeight =" << contentH
                          << " available =" << available
                          << " slack =" << (available - contentH);
        QVERIFY2(contentH <= available,
                 "settings content must not overflow its surface");
        QVERIFY2(available - contentH < 24,
                 "settings content should stay near-exact-fit (no dead space)");
    }
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

    // Phase 12: the Show-shortcuts row → openShortcuts → ShortcutsWindow.
    // Invoke the QML-side function (bridge parity with CR-01 above) — proves
    // the module import (ShortcutsWindow.qml registered in wisp_qml) and the
    // controller-injection both landed.
    QVERIFY2(QMetaObject::invokeMethod(settingsWin, "openShortcuts"),
             "QML show-shortcuts function must be invokable");
    QTest::qWait(100);
    QQuickWindow *shortcutsWin = findWindowByTitle(QStringLiteral("wisp — keyboard shortcuts"));
    QVERIFY2(shortcutsWin, "shortcuts window must exist after Show-shortcuts handoff");
    QVERIFY2(shortcutsWin->isVisible(), "shortcuts window must be visible after handoff");
    // Mirrors the settings geometry: the window is token-sized (Phase 12).
    QCOMPARE(shortcutsWin->width(), 560);
    QCOMPARE(shortcutsWin->height(), 580);

    // Reuse guarantee (CR-02 analog): closing and re-opening reuses the window.
    shortcutsWin->hide();
    QTest::qWait(50);
    settings.openShortcuts();
    QTest::qWait(100);
    QVERIFY2(shortcutsWin->isVisible(), "shortcuts window must REOPEN on the second handoff");
}

// 2026-10-08: the folders list was a ScrollView capped at two 36px rows, so a
// third folder was ONLY reachable by scrolling inside the list — the one scroll
// the settings surface should never have had ("i dont want the user to able to
// scroll anything in the settings"). It is now wrapping chips: the block grows,
// the window grows with it, and fitScale shrinks the lot on a short screen.
//
// What must hold, and what this asserts:
//   - every folder gets a chip (nothing dropped),
//   - the chips actually WRAP onto more than one row,
//   - no chip's bottom edge escapes the folders area (the no-clip guarantee
//     that replaced scrolling — a clipped chip is exactly the old bug),
//   - the window is taller than its 813px base, i.e. it grew rather than hid.
void ShellTest::settingsFoldersFillTheBlock_20261008()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    // 9 roots with deliberately long, similar names — enough to wrap well past
    // the 72px (2-row) budget the old list had, which is what used to force the
    // scroll.
    QStringList roots;
    for (int i = 0; i < 9; ++i)
        roots << QStringLiteral("C:/Users/me/Program Files/App Folder Number %1").arg(i + 1);
    {
        SettingsStore store(iniPath);
        store.setScanRoots(roots);
        QCOMPARE(store.scanRoots().size(), 9);
    }

    QQmlApplicationEngine engine;
    engine.loadFromModule("wisp", "MainWindow");
    QVERIFY2(!engine.rootObjects().isEmpty(), "MainWindow must load from module wisp");

    SettingsStore store(iniPath);
    AutostartManager autostart;
    HotkeyManager hotkeys(iniPath);
    HotkeyCaptureDialog capture(&engine);
    ScanService scanService;
    SettingsWindow settings(&engine, &store, &autostart, &hotkeys, &capture, &scanService);
    settings.open();
    QTest::qWait(250); // open fade + activation handshake
    QQuickWindow *settingsWin = findWindowByTitle(QStringLiteral("wisp — settings"));
    QVERIFY2(settingsWin, "settings window must exist");

    auto *area = settingsWin->findChild<QQuickItem *>("settingsFoldersArea");
    auto *flow = settingsWin->findChild<QQuickItem *>("settingsFolderChips");
    QVERIFY2(area, "folders area must exist");
    QVERIFY2(flow, "folder chips flow must exist");

    // Read implicitHeight FIRST: it forces the Flow/Positioner polish pass, and
    // it is that pass which instantiates the Repeater's delegates. Enumerating
    // childItems() before it reports 1, not 9 (measured) — the chips are not
    // "missing", they just don't exist as objects yet.
    const qreal chipsHeight = flow->implicitHeight();

    // childItems(), not findChildren(): a Repeater's delegates are VISUAL
    // children of the Flow, not QObject children of the window, so findChildren
    // reports 0 for them (verified — the chips exist and lay out fine). The
    // Repeater itself is also a visual child (QQuickRepeater is a QQuickItem),
    // hence the objectName filter.
    QList<QQuickItem *> chips;
    for (auto *child : flow->childItems()) {
        if (child->objectName() == QLatin1String("settingsFolderChip"))
            chips.append(child);
    }
    QCOMPARE(chips.size(), 9);

    // Wrapping really happened: more than one distinct row of chips.
    QSet<qreal> rows;
    for (auto *chip : chips)
        rows.insert(chip->y());
    QVERIFY2(rows.size() > 1,
             qPrintable(QStringLiteral("chips must wrap onto >1 row (saw %1)").arg(rows.size())));

    // The no-clip guarantee: every chip sits fully inside the folders area.
    // This is the assertion that would have failed on the old capped list.
    //
    // Positions MUST be mapped: a chip's parent is the Flow, not the area, so
    // comparing chip->y() against area->y() compares two unrelated coordinate
    // spaces (it passed by coincidence before — both were near 0).
    for (auto *chip : chips) {
        const QPointF top = chip->mapToItem(area, QPointF(0, 0));
        const QPointF bottom = chip->mapToItem(area, QPointF(0, chip->height()));
        QVERIFY2(top.y() >= -0.5 && bottom.y() <= area->height() + 0.5,
                 qPrintable(QStringLiteral("chip spans y=%1..%2 but the folders area is "
                                           "0..%3 — it would be clipped")
                                .arg(top.y()).arg(bottom.y()).arg(area->height())));
    }

    // 2026-10-09: the chips sat flush under the "Folders wisp searches…" subtitle
    // and read as mushed against it. Theme.settingsChipPadTop (10) is folded
    // INTO the area's height, so it costs the 813px window nothing. 8 is a
    // floor below the declared 10, not a restatement of it — this only has to
    // catch the padding being dropped back to 0.
    {
        const qreal topChip = chips.first()->mapToItem(area, QPointF(0, 0)).y();
        QVERIFY2(topChip >= 8,
                 qPrintable(QStringLiteral("chips need breathing room under the "
                                           "subtitle (got %1px, want >=8)").arg(topChip)));
    }

    // The block is tall enough for what it holds (the Flow's implicitHeight is
    // what the whole window-height model rests on).
    QVERIFY2(area->height() >= chipsHeight - 0.5,
             qPrintable(QStringLiteral("folders area (%1) must fit the chips' wrapped "
                                       "height (%2)")
                            .arg(area->height()).arg(chipsHeight)));

    // The window GREW rather than clipping: taller than the 813px base.
    QVERIFY2(settingsWin->height() > 813,
             qPrintable(QStringLiteral("window must grow past its 813px base for 9 "
                                       "folders (got %1)").arg(settingsWin->height())));

    // ── The scan progress bar must not overlap the block (2026-10-08) ──
    // Reported: pressing "Scan now" drew the bar across the "Last scan … 194
    // entries" summary and the Scan now button. It was anchored to the block's
    // bottom with a POSITIVE bottomMargin, and the block's 182px budget ends
    // flush with the action row — so 8px up from the bottom is squarely inside
    // it. It now sits in the inter-section gap below the block.
    //
    // Asserted on geometry, not on a screenshot: `scanning` is a read-only
    // property, so there is no way to make the bar visible here — but anchors
    // resolve whether or not it is visible, so the overlap is still measurable.
    {
        auto *block = settingsWin->findChild<QQuickItem *>("settingsScanBlock");
        auto *bar = settingsWin->findChild<QQuickItem *>("settingsScanBar");
        QVERIFY2(block, "scan block must exist");
        QVERIFY2(bar, "scan progress bar must exist");
        QVERIFY2(bar->y() >= block->height() - 0.5,
                 qPrintable(QStringLiteral("scan bar (y=%1) overlaps the block it belongs "
                                           "to (height %2) — it must sit in the gap below")
                                .arg(bar->y()).arg(block->height())));
        // …and it must FIT that gap, so it can't reach the next section either.
        QVERIFY2(bar->height() <= 8,
                 qPrintable(QStringLiteral("scan bar (%1px) must fit the 8px row gap")
                                .arg(bar->height())));
    }

    settings.close();
    QTest::qWait(250);
}

QTEST_MAIN(ShellTest)
#include "tst_shell.moc"
