# Phase 3 — Pattern Map (App Search)

> Analog files and code excerpts extracted from the current codebase on 2026-08-09. Executors: replicate these patterns — do not invent parallel conventions.

## 1. wisp_core static lib pattern (CMakeLists.txt)

```cmake
qt_add_library(wisp_core STATIC
    src/win/WinHotkey.cpp
    src/win/WinFullscreenGuard.cpp
    src/core/HotkeyManager.cpp
    src/core/LauncherController.cpp
    src/ui/HotkeyCaptureDialog.cpp
)
target_include_directories(wisp_core PUBLIC src)
target_link_libraries(wisp_core PUBLIC Qt6::Gui Qt6::Qml)
target_link_libraries(wisp_core PRIVATE shell32 Qt6::Quick)
```

**Rule:** every new `src/core/*` and `src/win/*` source must be added to `wisp_core`'s source list (forgotten sources produce LNK2019s — STATE.md phase-2 lesson). Tests link `wisp_core` and test targets are `qt_add_executable(tst_X ...) + add_test(NAME tst_X ...)` inside `if(BUILD_TESTING)`. `tst_shell` additionally links `wisp_qml wisp_qmlplugin` (QML module targets) — the QML results list test (if any) needs the same.

## 2. Test pattern (tests/tst_launcher.cpp is the canonical analog)

```cpp
#include <QtTest>
#include "core/LauncherController.h"
class TstX : public QObject { Q_OBJECT
private slots:
    void someBehavior();   // one private slot per behavior
};
void TstX::someBehavior() { /* QCOMPARE / QVERIFY */ }
QTEST_MAIN(TstX)
#include "tst_x.moc"
```

**Key patterns:**
- **Dependency injection via `std::function`** (fake-injectable): `LauncherController::setFullscreenGuard(std::function<WinFullscreenGuard::State()>)` — window-light tests run the policy against a null window with counting fakes. AppCatalog (scanner fn) and LaunchController (launcher fn) MUST use the same mechanism.
- QtTest + event loop for timers: `QTest::qWait(200)` (grace timer test in tst_launcher).
- Tests are **build-gated**: `ctest` runs them; behavior proven per-task in the task's test file.

## 3. QML module + singleton pattern

```cmake
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_library(wisp_qml STATIC)
qt_add_qml_module(wisp_qml
    URI wisp
    VERSION 0.1.0
    QML_FILES qml/MainWindow.qml qml/HotkeyCaptureDialog.qml qml/Theme.qml
    RESOURCES qml/assets/shadow.png
)
```

**Rule:** new QML files MUST be added to `QML_FILES` (else silent "No module named wisp" / missing file at runtime). Theme.qml tokens are the ONLY visual values — never literals (01-UI-SPEC hard rule; qmllint-verified).

## 4. Theme.qml token surface (exact names, consumed by Phase 3 UI)

`surface #1E1E1E`, `surfaceSecondary #2D2D30`, `border #3F3F46`, `accent #0078D4`, `accentLight #58A6FF`, `textPrimary #F5F5F5`, `textSecondary #A0A0A0`, `spaceXs 4`, `spaceSm 8`, `spaceMd 12`, `spaceLg 16`, `spaceXl 24`, `space2xl 32`, `rowHeight 44`, `radiusSurface 12`, `fontSizeQuery 18`, `fontSizeTitle 15`, `fontSizeSubtitle 12`, `fontSizeKeycap 12`, `fontWeightRegular 400`, `fontWeightSemibold 600`, `surfaceWidth 640`, `surfaceHeight 400`.

## 5. MainWindow.qml structure (search field slots in below the surface Rectangle)

- `Window { flags: Qt.Tool | Qt.FramelessWindowHint; color: "transparent"; visible: false }`
- `shell` Item (objectName "shell", focus: true, `Keys.onEscapePressed: dismiss()`), Scale transform, shadow Image, surface Rectangle (clip: true).
- Resident lifecycle: `closing` flag + `closeAnim.onFinished → hide()`, `hideNow()` instant dismiss, `onVisibleChanged` re-arms Escape, `onActiveChanged → shell.forceActiveFocus()`.
- **Inertia:** the `shell` Item holds `focus: true` → new Keys handlers (Up/Down/PageUp/PageDown/Home/End/Return) attach to the same shell Item; the search TextField sits inside the surface Rectangle above the new ListView.

## 6. Controller-owned policy (architectural rule from CONTEXT.md + 02)

- QML never holds logic; C++ never touches UI directly (invokeMethod/dismiss contract from 02).
- Windows detail lives behind `src/win/` with pure C++ interfaces (WinHotkey/WinFullscreenGuard precedent) — new `WinStartMenuEnumerator`, `WinUwpEnumerator`, `WinLaunch` follow it exactly.
- Catalog worker + model updates marshal via Qt queued signals/slots; COM objects created and used on the same worker thread (`CoInitializeEx` per thread — PITFALLS #3).

## 7. main.cpp wiring pattern (from 02-03)

`QApplication` → `engine.loadFromModule("wisp","MainWindow")` → `qobject_cast<QQuickWindow *>(engine.rootObjects().first())` → `controller.setWindow(window)` → tray branch (`tray.show()` < `connect(registrationFailed…)` < `hotkeys.start()` — ORDER LOAD-BEARING) → tray-less fallback → `app.exec()`. Phase 3 appends: `AppCatalog` start (worker), `ResultsModel` (context property on engine root context), `LaunchController` wiring, query flow TextField → model, launch → `hideNow()`.

## 8. C++/WinRT include wiring (new this phase)

```cmake
file(GLOB _sdk_cppwinrt "$ENV{WindowsSdkDir}Include/*/cppwinrt")
list(SORT _sdk_cppwinrt ORDER DESCENDING)
target_include_directories(wisp_core SYSTEM PRIVATE "${_sdk_cppwinrt[0]}")
```

Code: `#include <winrt/Windows.Management.Deployment.h>` etc.; `winrt::init_apartment(winrt::apartment_type::multi_threaded)` at worker-thread start, `winrt::uninit_apartment()` at thread end.