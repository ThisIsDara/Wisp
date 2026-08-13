# Phase 4 - Pattern Map (File Search)

> Analog files and code excerpts extracted from the current codebase on 2026-08-10. Executors: replicate these patterns - do not invent parallel conventions.

## 1. src/win firewall pattern (WinStartMenuEnumerator.h is the canonical analog for WinSearchQuery)

```cpp
#pragma once
#include <QStringList>
#include <QVector>
#include "core/AppEntry.h"
namespace WinStartMenuEnumerator {
QVector<AppEntry> scanStartMenu();          // live COM walk (dev-machine smoke)
QVector<AppEntry> scanRoots(const QStringList &rootDirs); // test seam
} // namespace WinStartMenuEnumerator
```

**Rules:** pure C++ interface, all Win32/COM detail confined to the .cpp, namespace-level free functions (no classes), doc comment stating threading/COM-apartment contract on the caller. `WinLaunch` (namespace + enum `LaunchResult`) is the other analog — return-value classification instead of exceptions. WinSearchQuery follows: namespace + `queryFiles(query)` + `indexStatus()` + testable pure helpers exposed for tst_search.

## 2. Worker-thread + COM-init pattern (AppCatalog.h/.cpp is the canonical analog for FileSearch)

```cpp
// AppCatalog.h essentials:
using Scanner = std::function<QVector<AppEntry>()>;
void setScanners(std::vector<Scanner> scanners); // std::function DI — the SAME mechanism as LaunchController::setLauncher
void start();      // kick first build (called ONCE in main.cpp, NEVER from hotkey path)
void ensureFresh(); // cheap age check on show
QVector<AppEntry> entries() const;  // immutable snapshot, QMutex-guarded, implicit-shared copy
signals: void refreshed();  // emitted after swap → ResultsModel::setEntries
// .cpp: worker lambda OWNS the thread's COM apartment:
//   CoInitializeEx(nullptr, COINIT_MULTITHREADED) at batch start, CoUninitialize at end
//   (S_FALSE/RPC_E_CHANGED_MODE reuse tolerated); QtConcurrent::run + QFutureWatcher;
//   dedupe/sort/swap happen on the UI thread in the watcher-completion handler.
```

**FileSearch additions beyond this pattern:** `QTimer m_debounce` (150ms, restart-on-change, singleShot), `quint64 m_generation` (++ on each dispatched query; stale results dropped in the completion handler), injectable `QueryFn` (std::function<QVector<FileResult>(const QString&)>) + injectable `StatusFn` for tests, injectable tracked-source `std::function<QVector<AppEntry>()>` for the D-06 second source.

## 3. LaunchController DI policy pattern (tst_launch is the test analog)

```cpp
using ResultReporter = std::function<void(const AppEntry &, WinLaunch::LaunchResult)>;
using Launcher = std::function<void(const AppEntry &, bool elevated, const ResultReporter &report)>;
void setLauncher(Launcher fn);            // default = WinLaunch bridge
void setResultReporter(ResultReporter fn);
void setDismissHandler(std::function<void()> fn);  // default no-op; main.cpp wires controller.hideNow()
Q_INVOKABLE void launchSelected(bool elevated = false);  // D-12 snapshot at keypress
Q_INVOKABLE void launchIndex(int index, bool elevated = false);
signals: void adminRequestRefused(const QString &displayName); void launchFailed(const QString &displayName);
```

**Rules:** policy in the controller, OS calls behind injectable seams, tests inject counting fakes — zero OS calls in CI. WinLaunch::LaunchResult { Launched, CancelledByUser, Failed }; ERROR_CANCELLED(1223)/SE_ERR_ACCESSDENIED(5) → CancelledByUser (quiet). **LaunchHistory (new) uses the same DI:** `setHistory(std::function<void(const AppEntry&)> recordFn)` or a small class with injectable QSettings path — tests point QSettings at QTemporaryDir.

## 4. ResultsModel roles + selection pattern (tst_model analog)

```cpp
enum Roles { DisplayNameRole = Qt::UserRole + 1, SubtitleRole, MatchRangesRole, AumidRole };
Q_PROPERTY(QString query READ query NOTIFY queryChanged)
Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectionChanged)
void setEntries(QVector<AppEntry> entries);          // full snapshot swap, query reset
Q_INVOKABLE void setQuery(const QString &query);     // filter+rank, cache ranges, clamp selection
AppEntry snapshotSelected() const;                   // D-12 value-copy freeze
Q_INVOKABLE void moveSelection(int delta); Q_INVOKABLE void selectIndex(int index);
static constexpr int kVisibleRows = 7;
```

**Rules:** m_order permutation over m_entries; m_ranges aligned with m_order for O(1) data(); selection clamped [0, rowCount-1]; snapshot = value copy under no lock (single-threaded UI). **Phase-4 additions:** `setFileResults(quint64 generation, QVector<AppEntry> files)` (stores latest gen, drops stale), `IsFolderRole` (new enum value), merge into m_order by score desc then displayName asc, file-cap 5, `kPathMatchScore = 100` constant, SubtitleRole = full path for Source::File (branch on entry.source).

## 5. QSettings INI pattern (HotkeyManager.cpp is the analog)

```cpp
m_settings(settingsPath.isEmpty()
               ? new QSettings(QSettings::IniFormat, QSettings::UserScope,
                               QStringLiteral("TID"), QStringLiteral("wisp"))
               : new QSettings(settingsPath, QSettings::IniFormat))
```

**Rule:** IniFormat + UserScope + "TID"/"wisp" → `%APPDATA%\TID\wisp\wisp.ini`. Test seam: constructor takes an explicit path (QTemporaryDir). LaunchHistory mirrors this exactly.

## 6. MainWindow.qml structure (integration points — do NOT restructure)

- search TextField `onTextChanged: resultsModel.setQuery(text)` — file pipeline hooks onto the same signal; QML stays lean (debounce lives in C++ FileSearch).
- results ListView `model: resultsModel` + `delegate: ResultsRow {}` + `currentIndex: resultsModel.selectedIndex` + `highlightFollowsCurrentItem: false` + keyboardActive/lastKbPressMs arbitration.
- emptyState Item `visible: resultsView.count === 0` — the status row (D-18) slots here as a sibling overlay.
- shell Keys block: Return/Enter → `shell.launchFromKey(event.modifiers)`; Ctrl+Shift → `launchSelected(true)` else `launchSelected(false)`. Phase-4 addition: Ctrl+Enter (Control without Shift) → `launchController.revealSelected()`.
- Transient-hint pattern (Connections → hintText + Timer 2500ms) exists for reuse — status row is NOT a hint (persistent while troubled).

## 7. CMakeLists.txt wiring rules

```cmake
qt_add_library(wisp_core STATIC ... src/win/WinSearchQuery.cpp src/core/FileSearch.cpp src/core/LaunchHistory.cpp ...)
target_link_libraries(wisp_core PRIVATE shell32 ole32 Qt6::Quick)  # ole32 already linked (CoCreateInstance)
qt_add_executable(tst_search tests/tst_search.cpp)
target_link_libraries(tst_search PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
add_test(NAME tst_search COMMAND tst_search)
```

**Rules:** every new src/* source in wisp_core's list BEFORE use (LNK2019 lesson); test targets inside `if(BUILD_TESTING)`; QML files in `QML_FILES` of the wisp_qml module; no new link libs expected (ole32 + windowsapp already there; oledb.h/msdasc.h are header-only SDK includes — no lib needed for raw OLE DB COM beyond ole32).

## 8. Build/verify loop (build.ps1)

`powershell -ExecutionPolicy Bypass -File build.ps1` (must run from vcvars64 context — build.ps1 handles it) then `ctest --test-dir build/dev --output-on-failure`. QtTest diagnostics: use `-o <file>,txt` when console output is swallowed. Never `QDir::NoSymLinks` on Windows. MSVC: no braced-init-list inside QCOMPARE — hoist named consts.
