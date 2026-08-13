---
phase: 02
slug: global-hotkey-toggle
status: complete
researched: 2026-08-09
---

# Phase 2 Research — Global Hotkey & Toggle

> Phase-scoped research for "The product's muscle memory": global hotkey (HOTK-01), conflict surfacing (HOTK-02), dismissal (HOTK-03), fullscreen guard (HOTK-04).
> Verified against Microsoft Learn + Qt 6.11 docs on 2026-08-09. Replaces no prior artifacts — supplements `research/STACK.md`, `research/ARCHITECTURE.md`, `research/PITFALLS.md` with the win32/win focus facts and the decisions this phase must lock.

## Verified Facts (confidence HIGH unless noted)

### 1. Global hotkey registration (Win32)
- `RegisterHotKey(NULL, id, fsModifiers, vk)` with `hWnd = NULL` posts `WM_HOTKEY` to the **calling thread's** message queue — exactly what Qt's dispatcher delivers to a `QAbstractNativeEventFilter` watching `eventType == "windows_dispatcher_MSG"` (Qt doc explicitly names this pattern; MS doc confirms NULL-hWnd semantics).
- `MOD_NOREPEAT` (0x4000, Vista+) prevents toggling spam when the key is held.
- **Failure semantics (critical for HOTK-02):** `RegisterHotKey` returns 0 and `GetLastError() == ERROR_HOTKEY_ALREADY_REGISTERED (1409)` when the combo is taken. F12 is reserved by the kernel for the debugger and must never be offered.
- Hotkey IDs must stay in the app-reserved range 0x0000–0xBFFF for Win32; Qt itself does not use this space.
- **UnregisterOnQuit:** `UnregisterHotKey` must be called on exit (ideally in `aboutToQuit`) — leaked registrations accumulate until logoff (PITFALLS #1).
- Decision inputs: STACK.md locked the raw `RegisterHotKey` + `QAbstractNativeEventFilter` mechanism (verified Qt6 pattern; `QWidget::nativeEvent`, the Qt5 pattern, silently fails in Qt 6). No QHotkey/MOD_WIN dependency.

### 2. Fullscreen guard — SHQueryUserNotificationState (verified against shellapi.h docs)
- `SHQueryUserNotificationState(&pquns)` — shellapi.h, shell32.lib, Vista+, usable from a desktop app, fast (no COM init required — pure Win32).
- **Enum (values verified against official `QUERY_USER_NOTIFICATION_STATE`):**
  - `QUNS_NOT_PRESENT = 1` — screen saver / locked / FUS session switching
  - `QUNS_BUSY = 2` — full-screen app or presentation settings
  - `QUNS_RUNNING_D3D_FULL_SCREEN = 3` — exclusive-mode D3D game (the classic "don't touch me" state)
  - `QUNS_PRESENTATION_MODE = 4` — presenter mode
  - `QUNS_ACCEPTS_NOTIFICATIONS = 5` — normal
  - `QUNS_QUIET_TIME = 6` — Win7+ first-hour quiet time
  - `QUNS_APP = 7` — Win8+ Store app running
- Guard decision (locked for this phase): treat `QUNS_BUSY`, `QUNS_RUNNING_D3D_FULL_SCREEN`, `QUNS_PRESENTATION_MODE` as "do NOT show" — the launcher stays hidden and the game keeps focus (STATE.md blocker resolved: **defer popup while fullscreen**; matches PowerToys/SoundSwitch behavior and PITFALLS-2's "don't fight the user").
- Note (: borderless fullscreen games usually report QUNS_BUSY; exclusive D3D reports 3. Checking both 2/3/4 covers both classes.

### 3. QSystemTrayIcon — dependency consequences (HOTK-02)
- `QSystemTrayIcon` lives in **Qt Widgets** (`Qt6::Widgets`), requires a `QApplication` (QGuiApplication alone is insufficient — the class asserts a QApplication event dispatcher for Windows messages).
- Therefore Phase 2 **must** add `Qt6::Widgets` to CMake and convert `main.cpp` from `QGuiApplication` to `QApplication`. A QtQuick window is fully compatible with a QApplication-based host (this is the standard hybrid app structure).
- `QSystemTrayIcon::showMessage(title, msg, icon, ms)` = Windows balloon/toast notification — exactly what HOTK-02 means by "tray notification". Icon must be shown first (`QSystemTrayIcon::show()`); messages may not appear if the system is busy — this is why the notification is a *path* into the fix (context menu), not the only fix.
- Minimal menu for Phase 2: `Open wisp` (toggle), `Change hotkey…`, `Quit` — Quit is mandatory now because the app becomes resident (Phase-1 D-08 "Escape quits" is superseded: Escape now only hides).

### 4. Dismissal mechanism (blocks from STATE.md resolved)
- **Click-outside = deactivation-based + grace timer (locked).** Window is `Qt.Tool | FramelessWindowHint` (Phase-1 contract) and needs keyboard focus, so `WS_EX_NOACTIVATE` is wrong for the full lifecycle (PITFALLS-2: "the correct pattern is activate normally on show... deactivation-based dismissal"). Track QML `Window.active` going false → grace `QTimer` (~150 ms) → if still inactive → hide with close animation. No global mouse hook (overkill; the window is the only interactive surface in Phase 2).
- **Escape:** Phase 1 quits on Escape (D-08). Phase 2 changes semantics: **Escape → animated dismiss → window hidden, process stays resident** (the hotkey path summons it again). The close lifecycle stays `closing`-flag + `closeAnim.onFinished → hide()`, minus the `Qt.quit()`.
- **Launch dismissal (HOTK-03 "instant, no animation wait"):** controller exposes `hideNow()` — plain `window->hide()` **without** running the close animation. Consumed in Phase 3 on Enter-launch; Phase 2 implements/proves the API with a direct call from the controller unit test.
- **Focus sequence (STATE.md locked):** show → raise → `requestActivate()` **deferred off the WM_HOTKEY handler** (QMetaObject::invokeMethod/QueuedConnection or QTimer::singleShot(0)); the NativeEventFilter must return immediately. QML-side: `forceActiveFocus` when window active changes. Never re-activate after show (PITFALLS-3).

### 5. Settings layout for the hotkey (STATE.md open decision — locked here)
- Decision: `QSettings` in **IniFormat** at `%APPDATA%\TID\wisp\wisp.ini` (org "TID", app "wisp") — the ARCHITECTURE% APPDATA%/INI recommendation, chosen over registry for **testability** (unit tests write to QSettings pointed at a temp file — no registry pollution on dev machines) and **clean-machine requirement** (no registry writes needed at all for the launcher itself; HKCU Run autostart in Phase 6 uses the Run key directly, not QSettings).
- Key: `hotkey/sequence` — stored as a `QKeySequence` portable string (e.g. "Alt+Space"), parsed by Qt, mapped to (MOD_*, VK) at registration time.
- API surface implemented in plan 02-01: `HotkeyManager::registeredHotkey()`, `setHotkey(QKeySequence)`, `prev/rejected` signal for invalid F12 / modifier-only combos.

### 6. Interface contracts created in plan 02-01 (consumed by 02-02/02-03)
```cpp
// src/win/WinHotkey.h
class WinHotkey : public QAbstractNativeEventFilter {
public:
    bool registerCombo(uint id, quint32 mods, quint32 vk);          // mods = MOD_ALT|MOD_NOREPEAT...; vk = VK_*
    void unregisterAll();
    static QString errorString(uint lastError);
    // nativeEventFilter: eventType=="windows_dispatcher_MSG" && MSG.message==WM_HOTKEY && wParam==registeredId → emit hotkeyTriggered()
signals: void hotkeyTriggered(uint id);
};

// src/win/WinFullscreenGuard.h
class WinFullscreenGuard {
public:
    enum State { AcceptsNotifications = 0, FullscreenActive = 1, Other = 2 };
    static State currentState();   // maps QUNS_BUSY|QUNS_RUNNING_D3D_FULL_SCREEN|QUNS_PRESENTATION_MODE -> FullscreenActive
};

// src/core/HotkeyManager.h
class HotkeyManager : public QObject {
    Q_OBJECT
public:
    explicit HotkeyManager(const QString &settingsPath = {}, QObject *parent = nullptr);
    bool start();                    // load settings + register; emits registrationFailed on error
    QKeySequence hotkey() const;
    void setHotkey(const QKeySequence &seq);   // save+unregister+re-register atomically; rejects F12/modifier-only
signals:
    void hotkeyPressed();
    void registrationFailed(const QString &combo);
};
```

### 7. Validation Architecture (consumed by 02-VALIDATION.md)
- Framework: **Qt Test (Qt6::Test)** — already wired in Phase 1 (`tst_shell` + CTest). Target modules for this phase: `tst_hotkey` (plan 01: WinHotkey round-trip, conflict detection via double-registration, QUNS mapping, HotkeyManager settings round-trip + F12 rejection), `tst_launcher` (plan 02: controller toggle against a null-capacity faker, deactivation timer, instant-dismiss API), extended in plan 03 with tray-surfacing tests (registrationFailed → notification signals).
- GUI/env tests stay **manual** (documented in VALIDATION.md Manual-Only): real Alt+Space against an exclusive-fullscreen game; real conflict with another launcher owning Alt+Space; tray balloon click path.
- Quick run: `ctest --test-dir build/dev --output-on-failure` (~3-§s). Full: `pssh -File build.ps1 && ctest`.
- Windows note: `tst_hotkey` must register a **non-conflicting** combo (e.g. Ctrl+Alt+F9) in CI/dev so the dev's own Alt+Space stays untouched; conflict path tested by registering the same combo twice in-process.

## Decisions This Phase Imposes (locked, mirror into plans)
1. **D-02.1:** App becomes resident after first launch: start hidden, hotkey toggles visibility; Escape/click-away hide (no quit); Quit only via tray menu.
2. **D-02.2:** Tray must exist in Phase 2 (HOTK-02) → **Qt6::Widgets + QApplication conversion** in main.cpp (plan 02-03). Minimal menu: Open wisp / Change hotkey… / Quit (Phase 6 extends it).
3. **D-02.3:** Fullscreen guard: defer show when QUNS Fuller; the hotkey while fullscreen is a silent no-op (launcher NOT shown).
4. **D-02.4:** Dismissal = deactivation + ~150ms grace timer (no WS_EX_NOACTIVATE); Escape → hidden resident; `hideNow()` for instant (launch) dismissal.
5. **D-02.5:** Hotkey stored in QSettings IniFormat `%APPDATA%\TID\wisp\wisp.ini` (org TID, app wisp), key `hotkey/sequence`, default Alt+Space.
6. **D-02.6:** Hotkey change path: tray → Change hotkey… → small QML capture dialog (Theme-driven) → validated → `HotkeyManager::setHotkey` re-registers immediately (HOTK-01 user-config + HOTK-02 "path with settings path").

## Remaining Unknowns (accepted, not blockers)
- Exact behavior of `QQuickWindow::requestActivate()` on first-show **after** Win7+ foreground-lock (the "allow" granted to the process that owns the hotkey makes it work — verified in STACK as Launchy's approach; fallback `SetForegroundWindow` if flaky in UAT).
- Windows 11 tray balloon vs toast rendering — cosmetic only; `showMessage` handles both.
- Borderless-fullscreen games might report QUNS_BUSY at times; guard treats them the same (safe direction: don't show).