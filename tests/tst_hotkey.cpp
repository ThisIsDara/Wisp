#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "core/HotkeyManager.h"
#include "win/WinFullscreenGuard.h"
#include "win/WinHotkey.h"

#include <chrono>
#include <thread>
#include <windows.h>

// Exposes the protected nativeEventFilter for direct MSG-driven testing.
class TestableWinHotkey : public WinHotkey
{
public:
    using WinHotkey::nativeEventFilter;
};

namespace {
// Dev-session-safe combo: never touch the user's real Alt+Space (RESEARCH §7).
constexpr int kMods = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
constexpr quint32 kVkF9 = VK_F9;
constexpr quint32 kVkF8 = VK_F8;
} // namespace

class TstHotkey : public QObject
{
    Q_OBJECT

private slots:
    void hotkeyRegistrationRoundTrip();
    void conflictDetection_HOTK02();
    void fullscreenGuardMapping();
    void fullscreenGuardForegroundRefinement();
    void hotkeyManagerPersistence();
    void hotkeyManagerRejectsInvalid();
    void hotkeyManagerReRegister();
    void hotkeyRealtimeDelivery();
};

void TstHotkey::hotkeyRegistrationRoundTrip()
{
    TestableWinHotkey hotkey;
    if (!hotkey.registerCombo(1, kMods, kVkF9)) {
        QSKIP("RegisterHotKey unavailable in this environment");
    }

    QSignalSpy spy(&hotkey, &WinHotkey::hotkeyTriggered);

    // Synthesize the exact MSG Qt's dispatcher would deliver.
    MSG msg{};
    msg.message = WM_HOTKEY;
    msg.wParam = 1;
    hotkey.nativeEventFilter(QByteArrayLiteral("windows_dispatcher_MSG"), &msg, nullptr);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toUInt(), 1u);

    // Ids we never registered must not fire.
    spy.clear();
    msg.wParam = 99;
    hotkey.nativeEventFilter(QByteArrayLiteral("windows_dispatcher_MSG"), &msg, nullptr);
    QCOMPARE(spy.count(), 0);

    // Other event types must be ignored (and never consumed).
    spy.clear();
    msg.message = WM_KEYDOWN;
    hotkey.nativeEventFilter(QByteArrayLiteral("windows_dispatcher_MSG"), &msg, nullptr);
    QCOMPARE(spy.count(), 0);
}

void TstHotkey::conflictDetection_HOTK02()
{
    TestableWinHotkey first;
    TestableWinHotkey second;

    if (!first.registerCombo(1, kMods, kVkF9)) {
        QSKIP("RegisterHotKey unavailable in this environment");
    }

    // Same combo, second registration must fail with 1409.
    QVERIFY(!second.registerCombo(2, kMods, kVkF9));
    const auto err = GetLastError();
    QCOMPARE(err, DWORD(ERROR_HOTKEY_ALREADY_REGISTERED));
    QVERIFY(WinHotkey::errorString(err).contains(QStringLiteral("already in use")));
}

void TstHotkey::fullscreenGuardMapping()
{
    QCOMPARE(WinFullscreenGuard::fromQuns(2), WinFullscreenGuard::FullscreenActive);
    QCOMPARE(WinFullscreenGuard::fromQuns(3), WinFullscreenGuard::FullscreenActive);
    QCOMPARE(WinFullscreenGuard::fromQuns(4), WinFullscreenGuard::FullscreenActive);
    QCOMPARE(WinFullscreenGuard::fromQuns(5), WinFullscreenGuard::AcceptsNotifications);
    QCOMPARE(WinFullscreenGuard::fromQuns(6), WinFullscreenGuard::AcceptsNotifications);
    QCOMPARE(WinFullscreenGuard::fromQuns(7), WinFullscreenGuard::AcceptsNotifications);
    QCOMPARE(WinFullscreenGuard::fromQuns(1), WinFullscreenGuard::Other);
    QCOMPARE(WinFullscreenGuard::fromQuns(999), WinFullscreenGuard::Other);

    // Live OS query smoke: must return one of the enum values without crashing.
    const auto state = WinFullscreenGuard::currentState();
    QVERIFY(state == WinFullscreenGuard::AcceptsNotifications
            || state == WinFullscreenGuard::FullscreenActive
            || state == WinFullscreenGuard::Other);
}

void TstHotkey::fullscreenGuardForegroundRefinement()
{
    // Softened guard (D-02.3 amendment): a blocking QUNS verdict defers only
    // when a fullscreen window is actually foreground. A minimized/alt-tabbed
    // game (QUNS says D3D fullscreen, observer says no fullscreen window)
    // must NOT lock the launcher out.
    QCOMPARE(WinFullscreenGuard::fromQunsWithForeground(2, true),
             WinFullscreenGuard::FullscreenActive);
    QCOMPARE(WinFullscreenGuard::fromQunsWithForeground(3, true),
             WinFullscreenGuard::FullscreenActive);
    QCOMPARE(WinFullscreenGuard::fromQunsWithForeground(4, true),
             WinFullscreenGuard::FullscreenActive);
    QCOMPARE(WinFullscreenGuard::fromQunsWithForeground(2, false),
             WinFullscreenGuard::AcceptsNotifications);
    QCOMPARE(WinFullscreenGuard::fromQunsWithForeground(3, false),
             WinFullscreenGuard::AcceptsNotifications);
    QCOMPARE(WinFullscreenGuard::fromQunsWithForeground(4, false),
             WinFullscreenGuard::AcceptsNotifications);

    // Non-blocking verdicts are unaffected by the foreground observation.
    QCOMPARE(WinFullscreenGuard::fromQunsWithForeground(5, true),
             WinFullscreenGuard::AcceptsNotifications);
    QCOMPARE(WinFullscreenGuard::fromQunsWithForeground(6, false),
             WinFullscreenGuard::AcceptsNotifications);
    QCOMPARE(WinFullscreenGuard::fromQunsWithForeground(7, true),
             WinFullscreenGuard::AcceptsNotifications);
    QCOMPARE(WinFullscreenGuard::fromQunsWithForeground(1, false),
             WinFullscreenGuard::Other);
    QCOMPARE(WinFullscreenGuard::fromQunsWithForeground(999, true),
             WinFullscreenGuard::Other);
}

void TstHotkey::hotkeyManagerPersistence()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    {
        HotkeyManager manager(iniPath);
        // Default when the file is empty.
        QCOMPARE(manager.hotkey().toString(), QStringLiteral("Alt+Space"));

        QSignalSpy failedSpy(&manager, &HotkeyManager::registrationFailed);
        QVERIFY(manager.start()); // Alt+Space must be free on the dev machine
        QCOMPARE(failedSpy.count(), 0);

        manager.setHotkey(QKeySequence(QStringLiteral("Ctrl+Shift+F9")));
        QCOMPARE(manager.hotkey().toString(), QStringLiteral("Ctrl+Shift+F9"));
    } // dtor → unregisterAll releases the combo

    // Persistence = QSettings read-back; a fresh manager must see the saved
    // sequence WITHOUT re-registering (a second start() while the first is
    // alive would legitimately hit 1409 — that trap is avoided here).
    HotkeyManager second(iniPath);
    QCOMPARE(second.hotkey().toString(), QStringLiteral("Ctrl+Shift+F9"));
}

void TstHotkey::hotkeyManagerRejectsInvalid()
{
    QTemporaryDir dir;
    HotkeyManager manager(dir.filePath(QStringLiteral("wisp.ini")));
    QVERIFY(manager.start());

    // Persist a valid combo first — rejections must leave it untouched
    // (in memory AND on disk).
    manager.setHotkey(QKeySequence(QStringLiteral("Ctrl+Alt+F9")));
    QSignalSpy failedSpy(&manager, &HotkeyManager::registrationFailed);

    const QString original = manager.hotkey().toString();

    // F12 is kernel-reserved (RESEARCH §1).
    manager.setHotkey(QKeySequence(QStringLiteral("F12")));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.takeFirst().at(0).toString(), QStringLiteral("F12"));
    QCOMPARE(manager.hotkey().toString(), original);

    // Modifier-only.
    manager.setHotkey(QKeySequence(QStringLiteral("Alt")));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(manager.hotkey().toString(), original);

    // Empty sequence.
    manager.setHotkey(QKeySequence());
    QCOMPARE(failedSpy.count(), 2);
    QCOMPARE(manager.hotkey().toString(), original);

    // Settings must be untouched by rejections.
    QSettings settings(dir.filePath(QStringLiteral("wisp.ini")), QSettings::IniFormat);
    QCOMPARE(settings.value(QStringLiteral("hotkey/sequence")).toString(), original);
}

void TstHotkey::hotkeyManagerReRegister()
{
    QTemporaryDir dir;
    HotkeyManager manager(dir.filePath(QStringLiteral("wisp.ini")));

    // Boot sequence: start() registers whatever is loaded (default Alt+Space).
    // setHotkey() is only called while the manager is live (HOTK-03 capture
    // dialog) — it swaps the registration in place.
    QVERIFY(manager.start());
    QCOMPARE(manager.hotkey().toString(), QStringLiteral("Alt+Space"));

    QSignalSpy changedSpy(&manager, &HotkeyManager::hotkeyChanged);
    QSignalSpy failedSpy(&manager, &HotkeyManager::registrationFailed);

    // Swap to a different combo — old combo released, new persisted.
    manager.setHotkey(QKeySequence(QStringLiteral("Ctrl+Alt+F9")));
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(manager.hotkey().toString(), QStringLiteral("Ctrl+Alt+F9"));

    QSettings settings(dir.filePath(QStringLiteral("wisp.ini")), QSettings::IniFormat);
    QCOMPARE(settings.value(QStringLiteral("hotkey/sequence")).toString(),
             QStringLiteral("Ctrl+Alt+F9"));

    // The OLD combo (Alt+Space) must now be free — registering it elsewhere succeeds.
    TestableWinHotkey probe;
    QVERIFY(probe.registerCombo(1, MOD_ALT | MOD_NOREPEAT, VK_SPACE));
    probe.unregisterAll();

    // Swap again — the previous combo must be free after the new one is live.
    manager.setHotkey(QKeySequence(QStringLiteral("Ctrl+Alt+F8")));
    QCOMPARE(changedSpy.count(), 2);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(manager.hotkey().toString(), QStringLiteral("Ctrl+Alt+F8"));
    QVERIFY(probe.registerCombo(2, kMods, kVkF9));
}

void TstHotkey::hotkeyRealtimeDelivery()
{
    // LIVE OS path: real registration + real event loop + real injected keys.
    // WM_HOTKEY is posted to the REGISTERING thread's queue regardless of
    // foreground, so a helper thread can inject with keybd_event from THIS
    // process — the registration happened on our internal pump thread, which
    // is exactly the production architecture (Qt 6.11 dispatcher swallows
    // WM_HOTKEY — see WinHotkey.h). If Qt ever delivers again, the native
    // filter path may supersede; until then this proves the OS→pump→signal
    // chain end-to-end.
    TestableWinHotkey hotkey;
    if (!hotkey.registerCombo(11, kMods, kVkF9)) {
        QSKIP("RegisterHotKey unavailable in this environment");
    }

    std::thread injector([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        const auto press = [](BYTE vk) {
            keybd_event(VK_CONTROL, 0, 0, 0);
            keybd_event(VK_MENU, 0, 0, 0);
            keybd_event(vk, 0, 0, 0);
            keybd_event(vk, 0, KEYEVENTF_KEYUP, 0);
            keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
            keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
        };
        press(kVkF9);
        press(kVkF9);
    });

    QSignalSpy spy(&hotkey, &WinHotkey::hotkeyTriggered);
    spy.wait(5000);
    injector.join();
    QVERIFY2(spy.count() >= 1, "no WM_HOTKEY reached the pump thread live");
}

QTEST_MAIN(TstHotkey)
#include "tst_hotkey.moc"