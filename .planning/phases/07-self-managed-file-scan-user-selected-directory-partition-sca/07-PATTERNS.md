# Phase 7: Self-Managed File Scan — Pattern Map

**Mapped:** 2026-08-14
**Files analyzed:** 16 (6 new, 8 modified, 2 deleted)
**Analogs found:** 13 / 16 (3 with no direct analog — see "No Analog Found")

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/win/WinDirectoryWalk.{h,cpp}` (NEW) | win-firewall primitive (namespace + plain structs) | file-I/O (per-dir listing) | `src/win/WinSearchQuery.h` (namespace firewall + pure helper contract); `src/win/WinStartMenuEnumerator.h` (walker + test seam) | role-match |
| `src/core/FileIndex.{h,cpp}` (NEW) | store (persistent index) | CRUD + serialization | `src/core/LaunchHistory.{h,cpp}` (persistence + makeSettings factory + normalize) | role-match |
| `src/core/ScanService.{h,cpp}` (NEW) | service (orchestrator) | event-driven (interval + worker batch) | `src/core/AppCatalog.{h,cpp}` (worker/watcher, single-flight, interval) + `src/core/FileSearch.h` (seams + Q_PROPERTY) | exact |
| `src/core/FileSearch.{h,cpp}` (MODIFIED) | coordinator | request-response (debounced) | itself (no analog read needed — self-pattern) | self |
| `src/core/ResultsModel.{h,cpp}` (MODIFIED) | model | CRUD / display | itself (mergeFiles) + `src/core/AppCatalog.cpp` dedupeLnkOverUwp (path-set dedupe shape) | self |
| `src/core/AppEntry.h` (MODIFIED, likely no-op) | contract | — | itself — `Source::File` already exists (line 13) | self |
| `src/core/LaunchController.{h,cpp}` (MODIFIED, likely no-op) | controller | request-response | itself — file/folder launch + reveal already handled | self |
| `src/ui/SettingsWindow.{h,cpp}` (MODIFIED) | controller | request-response | itself — injected-collaborator pattern | self |
| `qml/SettingsWindow.qml` (MODIFIED) | QML view | — | itself — row/section pattern (hotkey + autostart rows) | self |
| `src/app/main.cpp` (MODIFIED) | wiring | — | itself — seam wiring section 04-05 (lines 94-151) | self |
| `qml/MainWindow.qml` (MODIFIED) | QML view | — | itself — emptyState/statusRow items (lines 701-761) | self |
| `tests/tst_scan.cpp` (NEW) | test | — | `tests/tst_filesearch.cpp` (dedicated pool + fake seams + QSignalSpy::wait) | exact |
| `tests/tst_index.cpp` (NEW) | test | — | `tests/tst_history.cpp` / `tst_settings.cpp` (QTemporaryDir real-file seam) | exact |
| `tests/tst_model.cpp`, `tests/tst_filesearch.cpp` (EXTENDED) | test | — | themselves | self |
| `src/win/WinSearchQuery.{h,cpp}` (DELETED) | — | — | — | — |
| `tests/tst_search.cpp` (DELETED) | — | — | — | — |

---

## Pattern Assignments

### `src/win/WinDirectoryWalk.{h,cpp}` (new, win-firewall primitive, file-I/O)

**Analog:** `src/win/WinSearchQuery.h` (namespace firewall with plain Qt structs — being deleted but its SHAPE is the convention to replicate), `src/win/WinStartMenuEnumerator.h` (walker + test-seam), `src/win/WinLaunch.h` (namespace + enum result classification).

**Imports / namespace pattern** — copy the firewall convention from `src/win/WinLaunch.h` lines 1-13 and `WinSearchQuery.h` lines 1-9:
```cpp
#pragma once
#include <QString>
#include <QVector>

// [contract comment: what lives here, what stays out]
namespace WinDirectoryWalk {   // namespace, NOT a class — free functions (WinLaunch/WinSearchQuery precedent)
```

**Interface shape** — the research-specified seam (07-RESEARCH.md Pattern 1, lines 160-169) mirrors `WinSearchQuery::FileResult` (WinSearchQuery.h:11-15, plain struct) exactly. Win32 types must never appear in the header:
```cpp
struct WinDirEntry {
    QString name;
    bool isDir = false, hidden = false, system = false, reparse = false;
    qint64 lastWriteMs = 0; // FILETIME → ms — one shared helper (memo comparability)
};
struct WinDirListing { QVector<WinDirEntry> entries; qint64 lastWriteMs = 0; bool ok = false; };
WinDirListing winListDirectory(const QString &path);
```

**Test-seam convention (IMPORTANT for tst_scan):** `WinStartMenuEnumerator` ships a test seam `scanRoots(const QStringList &rootDirs)` (WinStartMenuEnumerator.h:26-30) that tests call with a QTemporaryDir fixture. WinDirectoryWalk does NOT need a second entry point — its `winListDirectory(QString)` IS already seam-shaped (single pure function, tst_scan fakes it with a `QHash<QString, WinDirListing>` map instead of invoking it). tst_scan should follow tst_enum's "REAL walk against a QTemporaryDir fixture" (tst_enum.cpp:17,33) plus the map-fake for delta tests.

**Implementation notes** (from RESEARCH Common Operation 1, lines 268-299 — the only new Win32 code in the phase):
- `FindFirstFileExW(pattern, FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH)`; skip `.`/`..`; fill all four bools + mtime from `WIN32_FIND_DATA` fields (NO per-entry `GetFileAttributesExW`/`status()` calls — Pitfall 1/2).
- `INVALID_HANDLE_VALUE` → return `out` with `ok=false` (no exception, no throw).
- Long-path prefix `\\?\` when `path + "\\*"` exceeds MAX_PATH (Pitfall 7 — qtbase precedent cited in research).
- **NO COM**: no `CoInitializeEx`, no `objbase.h` — the AppCatalog.cpp COM block (lines 149-154) must NOT be copied here (research: "COM-not-required for FindFirstFile-style walking", D-08).

**Error handling pattern:** silent `ok=false` flag, never throws (headers warn "failure → Error state, never a hang" — Pitfall 7/9). qWarning on FindClose/failure paths per WinSearchQuery.cpp qWarning discipline (lines 218-221 style).

---

### `src/core/FileIndex.{h,cpp}` (new, store, CRUD + serialization)

**Analog:** `src/core/LaunchHistory.{h,cpp}` (persistence store with makeSettings factory + path normalize + dedupe helper), `src/core/SettingsStore.h` (UI-thread store, Q_PROPERTY surface).

**Class shape** — copy the store contract of `LaunchHistory.h` lines 16-27 (plain class, explicit settingsPath ctor for the test seam, no QObject needed unless QML binds directly — planner's call):
```cpp
class FileIndex {
public:
    explicit FileIndex(const QString &indexPath = {}); // {} → %APPDATA%\TID\wisp\index.dat (LaunchHistory test-seam precedent, LaunchHistory.h:19-20)
    bool save() const;                                  // QSaveFile + QDataStream (RESEARCH Common Operation 2)
    bool load();                                        // magic+version guard; failure → empty + rescan (Pitfall 8)
    QVector<IndexEntry> queryCandidates(const QString &query) const; // subsequence prefilter (RESEARCH Common Operation 3)
    void applyDelta(...);                               // incremental walk result (pure; RECURSION + skip-list + mtime memo live HERE, never in src/win)
private:
    QString normalize(const QString &path) const;       // QDir::toNativeSeparators — LaunchHistory.cpp:135-138
    QString m_path;
    QHash<QString, qint64> m_dirMtimes;                 // per-directory memo
    QVector<IndexEntry> m_entries;                      // paths + match keys only (D-07)
};
```

**Path normalization** — copy `LaunchHistory::normalize` / `keyFor` verbatim (LaunchHistory.cpp:135-143): `QDir::toNativeSeparators` before using a path as a KEY. Pitfall 5: QFileDialog returns `/`-separated paths; dedupe key = case-folded native path (use `.toCaseFolded()` — dedupeLnkOverUwp precedent, AppCatalog.cpp:30).

**Dedupe-helper shape** — copy `appendEntry` (LaunchHistory.cpp:35-45): `QSet<QString> seen` guard + displayName derived via `QFileInfo(path).fileName()` (never stored). Same derivation rule as phase 4 (D-10 single-source-of-truth).

**Serialization** — NO existing analog (see "No Analog Found"): use `QDataStream` + `QSaveFile` with magic `0x57535031` ("WSP1") + version header; `file.commit()` for atomicity (RESEARCH Common Operation 2, lines 301-317). Load failure → START EMPTY, never crash (Pitfall 8: same silent-fallback spirit as SettingsStore D-16 header comment, SettingsStore.h:20-21).

**Threading contract:** index is mutated ONLY on the scan worker (single writer — RESEARCH Pattern 2); readers (FileSearch workers via queryCandidates) get an immutable snapshot — AppCatalog's `m_snapshotMutex` + implicit-shared `entries()` copy (AppCatalog.h:96-100, AppCatalog.cpp:126-130) is the exact pattern if a mutex is needed; alternatively swap-on-apply under the same lock. Do NOT copy the SettingsStore no-mutex stance — that store is UI-thread-only by contract (SettingsStore.h:14-18).

---

### `src/core/ScanService.{h,cpp}` (new, service/orchestrator, event-driven)

**Analog:** `src/core/AppCatalog.{h,cpp}` (THE worker/watcher pattern) + `src/core/FileSearch.h` (seam injection + Q_PROPERTY contract).

**Class shape** — merge the two analogs; AppCatalog provides the worker lifecycle, FileSearch provides the seam/state surface:

```cpp
// From AppCatalog.h:42-53 — scanner injection, interval, single-flight
explicit ScanService(QObject *parent = nullptr);
using ListFn = std::function<WinDirListing(const QString &path)>;   // seam — production: &WinDirectoryWalk::winListDirectory, tests: map-fake
void setListFn(ListFn fn);
using SettingsSource = std::function<ScanSettings()>;               // UI-thread snapshot — NEVER the store on a worker (Pitfall 4)
void setSettingsSource(SettingsSource fn);
void setIndex(FileIndex *index);        // single writer — the scan worker (RESEARCH Pattern 2)
void setPool(QThreadPool *pool);        // WR-05 test seam — copy FileSearch::setPool (FileSearch.h:50-53)
void start();                           // call once at boot; starts interval timer if roots exist (Open Question 6)
void requestScan();                     // "Scan now" / first-root-added funnel — single-flight gate (AppCatalog::ensureFresh shape)
int stateOrdinal() const;               // → StatusFn seam (RESEARCH Pattern 2: Idle=0, NoRoots, Scanning, Error)
Q_PROPERTY(QString lastScanSummary READ lastScanSummary NOTIFY scanStateChanged)   // "last scan time + entry count" (D-10)
```

**Q_PROPERTY + NOTIFY contract** — copy FileSearch.h:25-26 and FileSearch.cpp:193-196 exactly (NOTIFY emitted only when the value actually changes — "no spurious NOTIFY" per ResultsModel.cpp:356-358):
```cpp
Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
Q_PROPERTY(bool indexerOk READ indexerOk NOTIFY stateChanged)
```

**Worker dispatch** — copy `AppCatalog::buildAsync` structure (AppCatalog.cpp:132-183) with TWO deliberate changes:
1. `QtConcurrent::run(m_pool ? m_pool : QThreadPool::globalInstance(), worker)` — the FileSearch pool-select form (FileSearch.cpp:176-178), because scans must use a DEDICATED pool, never the global one (Pitfall 9).
2. Inside the worker: `QThread::currentThread()->setPriority(QThread::LowPriority)` (RESEARCH Pattern 2) and NO CoInitializeEx block — remove AppCatalog.cpp lines 149-154 and 177-178 (COM not needed for FindFirstFileExW).
3. Keep the everything-caught discipline (AppCatalog.cpp:156-175): a throwing seam → counted error, never a crash; belt-and-braces outer catch (QtConcurrent rethrows at `result()` on the UI thread).
4. Worker payload struct + `QFutureWatcher<T>` connected to `finished` → UI-thread completion (AppCatalog.h:85-91, AppCatalog.cpp:89-91). UI-thread completion applies the delta + persists (QSaveFile is thread-agnostic; writing from the worker is also fine — planner's call, RESEARCH: "persist on the scan worker").
5. Single-flight + interval age-check: copy `m_buildInFlight` + `m_lastBuilt` + `ensureFresh` (AppCatalog.h:98-100, AppCatalog.cpp:113-124) for the interval QTimer tick. Generation-counter on scan results (interval result discarded if a newer scan started) — FileSearch's generation pattern (FileSearch.h:100, FileSearch.cpp:117, 183-191) extends per RESEARCH Pattern 2 / Pitfall 9.

**Snapshot discipline (Pitfall 4):** the settings snapshot lambda is invoked ON THE UI THREAD at dispatch time; the worker copies `ScanSettings` by value (RESEARCH Pattern 2 wiring sketch lines 177-186). Never capture `SettingsStore*` into the worker.

---

### `src/core/FileSearch.{h,cpp}` (modified, coordinator — self-pattern)

No analog search needed — the file IS the pattern. Extraction for the planner (all verified):

- **Seam surface stays identical** (FileSearch.h:36-53): `QueryFn`, `StatusFn`, `TrackedSource`, `AddedSource`, `AddExeDialog`, `AddEntryStore`, `setPool`. Only the WIRING of QueryFn/StatusFn changes (main.cpp, below). The `QueryResult` struct (FileSearch.h:36) is the contract the FileIndex-backed query lambda must return.
- **State enum rename (planner's call, Open Question 2):** `FileSearchState { Ok, Disabled, Building, Unavailable }` (FileSearch.h:31) → `{ Idle, NoRoots, Scanning, Error }`. The ordinal mapping in `stateFromOrdinal` (FileSearch.cpp:18-31) is the single place that translates the ScanService ordinal — main.cpp maps explicitly, never casts blindly (FileSearch.h:29-30 contract).
- **Locked copy single home:** `statusText()` (FileSearch.cpp:200-215) — replace the three Windows-Search strings with the new scan-state copy (RESEARCH Pattern 3 suggestions: "No scan locations yet — add folders in Settings to search files" / "Scanning — files appear as they're found" / "Scan unavailable — check your scan locations in Settings"). QML renders this verbatim (MainWindow.qml:756).
- **Dead-end guard preserved:** D-16/D-17 skip-branch (FileSearch.cpp:145-147) keeps its shape — `NoRoots` and `Error` skip the query, `Scanning` still queries (FileSearch.cpp:156 "Building stays Building — the query still runs" comment remapped).
- **tst_filesearch asserts the locked strings** (tst_filesearch.cpp:224-226, 246, 272-273, 317) — those expectations must be updated in lockstep.

---

### `src/core/ResultsModel.{h,cpp}` (modified, model — D-03 dedupe in mergeFiles)

**Analog:** itself (mergeFiles) + `AppCatalog::dedupeLnkOverUwp` (AppCatalog.cpp:22-50) for the path-set dedupe shape.

- **The dedupe insertion point** is `mergeFiles()` (ResultsModel.cpp:165-209), AFTER the candidate union loop and BEFORE/AFTER the sort (planner's call — research recommends building the set first, dropping suppressed rows during the final emit loop, ResultsModel.cpp:200-208 region). Shape copy:

```cpp
// AppCatalog.cpp:25-30 — fold + QHash taken-set is the established identity pattern
QHash<QString, int> taken;
for (const AppEntry &e : raw) { taken.insert(e.displayName.toCaseFolded(), out.size()); ... }
```

  D-03 variant: build `QSet<QString>` from every catalog/app row's `targetPath.toCaseFolded()` (`m_entries` — Lnk/tracked are path-bearing; UWP has EMPTY targetPath → never collides, Pitfall 10), then skip `fromFiles` rows whose case-folded `targetPath` is in the set. Scan-row-vs-scan-row dupes are handled in FileIndex's delta apply (path QSet during insertion) — not here.
- **Row/entry plumbing already handles scanned rows:** `Row{int entryIndex; bool fromFiles; bool fromAdded}` (ResultsModel.h:112), `entryAt` three-branch resolution (ResultsModel.cpp:370-377), `SubtitleRole` = full path for File rows (ResultsModel.cpp:293-295), `IsFolderRole` (ResultsModel.cpp:301-303), `IconKeyRole` `"path:" + targetPath` (ResultsModel.cpp:313-314 — works for scanned files AND folders as-is). No role changes required.
- **kMaxFileRows = 5, kPathMatchScore = 100** (ResultsModel.h:140-141) unchanged — the cap/tier logic in mergeFiles (ResultsModel.cpp:179-182, 200-208) stays.
- **tst_model extension pattern:** tst_model.cpp already exercises setFileResults/mergeFiles (see CMakeLists.txt:118-120); add D-03 cases following its existing structure (feed catalog rows via setEntries + file rows via setFileResults with same-path entries, assert one row and catalo-gsource wins).

---

### `src/core/AppEntry.h` (modified — likely NO changes)

Verified: `Source::File` already exists (AppEntry.h:13); file rows carry `displayName` (filename), `targetPath` (full path), `isFolder` (AppEntry.h:20), `arguments`/`aumid` empty. Scanned entries reuse this shape verbatim — `AppEntry::Source::File { displayName = filename, targetPath = full native path, isFolder = dir rows }`. **No enum change needed.** (Planner could add a `Source::Scan` if a later phase needs to distinguish scan rows from tracked/added File rows — explicitly not required by D-02/D-03 since dedupe is path-based and rows are display-identical.)

---

### `src/core/LaunchController.{h,cpp}` (modified — likely NO changes)

Verified: `launchSelected`/`launchIndex` snapshot at keypress (LaunchController.cpp:85-101), `launchEntry` maps File rows to silent-normal (LaunchController.cpp:113-120), `revealSelected` is File-only and reuses `WinLaunch::revealInExplorer` (LaunchController.cpp:122-145). Folder rows (isFolder) already flow through the same path. **The Ctrl+Enter Explorer-select is already implemented and tested (tst_launch)** — nothing to add for LAUN-03; regression only.

---

### `src/ui/SettingsWindow.{h,cpp}` (modified — scan-locations section)

**Analog:** itself (self-pattern). Extraction:

- **Q_PROPERTY/Q_INVOKABLE surface** (SettingsWindow.h:32-58): add roots/interval/scanNow/lastScan props following the existing two:
```cpp
Q_PROPERTY(QString currentHotkey READ currentHotkey NOTIFY currentHotkeyChanged)   // lines 32-33 template
Q_PROPERTY(QStringList scanRoots READ scanRoots NOTIFY scanRootsChanged)           // NEW (D-10)
Q_PROPERTY(int scanIntervalMinutes READ scanIntervalMinutes NOTIFY scanIntervalChanged) // NEW
Q_INVOKABLE void addScanRoot();        // QFileDialog::getExistingDirectory → store → emit (D-05)
Q_INVOKABLE void removeScanRoot(int index);
Q_INVOKABLE void scanNow();            // → ScanService::requestScan
Q_INVOKABLE void setScanInterval(int minutes);
```
- **State-refresh-on-open:** `open()` re-emits `autostartEnabledChanged()`/`currentHotkeyChanged()` (SettingsWindow.cpp:61-63) — the scan section must do the same (roots/interval/lastScan refresh every open, D-10: "state read when settings opens").
- **Accent-picker precedent for folder picker:** the dialog seam pattern `setAddExeDialog([] { return QFileDialog::getOpenFileName(...); })` (main.cpp:124-127) is the QFileDialog precedent; `getExistingDirectory` is the D-05 call. The controller method calls the dialog directly (controller-owned policy, thin QML) per the existing Q_INVOKABLE style.
- **Persist roots/interval:** SettingsStore is UI-thread-only, NO mutex (SettingsStore.h:14-18) — roots/interval as new INI keys is fine as long as the scanner reads them ONLY via the UI-thread snapshot lambda (Pattern 2; CONTEXT explicitly flags "may need a sibling store or a mutex before worker threads touch it" — snapshot at dispatch is the research-recommended answer, no mutex needed).

### `qml/SettingsWindow.qml` (modified — "Scan locations" section)

**Analog:** itself. The section is a new `Rectangle` row INSIDE the content `Column` (SettingsWindow.qml:113-117), following the hotkey-row template (SettingsWindow.qml:126-192: title+subtitle Column on the left, control on the right, 1px hairline for rows below the first — autostart row lines 305-311). D-10 controls: root list (Repeater in a height-capped ScrollView — never a full-height ListView in a fixed window, research Pitfall 3), interval selector, "Scan now" button, last-scan summary bound to controller props.

**Geometry landmine (verified):** window is fixed `Theme.settingsWindowHeight: 360` (Theme.qml:87); content budget 318 ≤ 328 fully consumed (SettingsWindow.qml:111-112 comment: 24+18+12+64+12+88+12+64+24). **The section requires growing the token** — research recommends `Theme.settingsWindowHeight` → ~560 (Theme.qml:86-89 family: settingsWindowWidth/Height + settingsSurfaceWidth/Height move together; add `settingsRowScan` token next to `settingsRowHotkey/Accent/Autostart`, Theme.qml:109-113). The window must still center on show (SettingsWindow.cpp:241-250 — re-applied every show).

---

### `src/app/main.cpp` (modified — seam re-wiring)

Verified removal points and insertion points:

- **DELETE:** `#include "win/WinSearchQuery.h"` (line 24); the QueryFn lambda (lines 98-112) and StatusFn lambda (lines 114-122); **KEEP** the tracked/added/dialog wiring (lines 123-128) — they are launch-history seams, not Windows Search.
- **REPLACE with** (research Pattern 2 wiring sketch, lines 177-186):
```cpp
fileSearch.setQueryFn([&index](const QString &q) {
    const QVector<AppEntry> rows = FileIndex::toEntries(index.queryCandidates(q), /*cap=*/100);
    return FileSearch::QueryResult{ rows, /*failed=*/false };
});
fileSearch.setStatusFn([&scanService] { return scanService.stateOrdinal(); });  // explicit map via stateFromOrdinal
```
- **NEW construction block** alongside the other controllers (main.cpp:77-85 stack pattern): `FileIndex index; ScanService scanService;` — stack order keeps dependencies alive (documented load-bearing order comment, main.cpp:157-162). Wire `scanService.setListFn(&WinDirectoryWalk::winListDirectory)`, `setIndex(&index)`, `setSettingsSource([&store]{ return ScanSettings{ store.scanRoots(), store.scanIntervalMinutes() }; })`.
- **Startup load (D-09):** load index.dat synchronously before/around engine load (A4: measure; must land well under launch budget) — in the main.cpp:173-179 region; **NO scan at boot**, interval timer starts with root count (Open Question 6).
- **SettingsWindow injection:** add the scan props' store/collaborator to the SettingsWindow ctor call (lines 245-246) — SettingsWindow takes all collaborators injected (SettingsWindow.h:39-41).
- **Binding hygiene:** resultsReady connect (main.cpp:204-208) and setEntries connect (186-189) unchanged.

---

### `qml/MainWindow.qml` (modified — no-roots prompt)

**Landmine (verified):** lines 705-761:

```qml
// line 708: empty-state gate
visible: resultsView.count === 0 && fileSearch.indexerOk
// line 752: status row gate — only while a query is active AND troubled
visible: resultsModel.query !== "" && !fileSearch.indexerOk && resultsView.count === 0
```

D-04 requires a new **empty-query no-roots branch**: `resultsModel.query === "" && !fileSearch.indexerOk && resultsView.count === 0` → prompt row ("No scan locations yet — add folders in Settings to search files" — copy follows the empty-state Item pattern lines 705-739: centered Column, glyph + two Texts, textSecondary, theme tokens only). The three branches must stay mutually exclusive via the query-text split — exactly how 708 vs 752 already partition the space. The status row renders `fileSearch.statusText` verbatim (line 756) — unchanged contract, only the source state remap differs.

---

### `tests/tst_scan.cpp` (new) + `tests/tst_index.cpp` (new)

**Analog: `tests/tst_filesearch.cpp`** — the seam-test fixture convention to replicate wholesale:
- Dedicated `QThreadPool m_pool` member + `setPool(&m_pool)` per instance (tst_filesearch.cpp:73, 87) — **ScanService must expose the same setPool seam** or tst_scan cannot follow this convention.
- `QSignalSpy::wait(kWaitGenerous)` with 5000ms timeout instead of wall-clock asserts (tst_filesearch.cpp:76, 103).
- Injected fakes with `std::atomic<int>` call counters (tst_filesearch.cpp:88-94, 182-191).
- `Q_DECLARE_METATYPE(AppEntry)` + `qRegisterMetaType<QVector<AppEntry>>()` for QSignalSpy of container args (tst_filesearch.cpp:13, 81).
- Hoisted named consts — no braced-init-lists inside QCOMPARE (tst_filesearch.cpp:42-46; tst_search.cpp:13-17).
- `QTEST_MAIN(TstScan)` + `#include "tst_scan.moc"` (tst_search.cpp:72-73).

**tst_scan specifics (RESEARCH Wave-0):** fake the enumeration seam with `QHash<QString, WinDirListing>` map (no filesystem) for delta tests (changed/new/deleted dirs, skip-list, hidden/system/reparse, single-flight); plus one REAL-walk test against a QTemporaryDir tree (tst_enum.cpp:33 fixture style) for `winListDirectory`.

**tst_index analog: `tests/tst_history.cpp`/`tst_settings.cpp`** — QTemporaryDir real-file round-trips (tst_history.cpp:17 "Every suite round-trips through a REAL temp INI (QTemporaryDir)"; tst_settings.cpp:13). For tst_index the seam is the explicit `indexPath` ctor arg (LaunchHistory.cpp:21-22 factory pattern). Cases: round-trip, truncated file, wrong magic/version → empty + no crash (research Pitfall 8), queryCandidates subsequence superset vs FuzzyMatcher (A3 cross-check).

---

### Deleted files (D-01)

- `src/win/WinSearchQuery.{h,cpp}` — delete entirely (624+44 lines; `tst_search` covered only the three pure helpers `classifyCatalogStatus` / `isAllowedResult` / `buildWhereRestriction`, tst_search.cpp:26-28 — those three slots die with it; no coverage of the live COM walk existed).
- `tests/tst_search.cpp` — delete.
- `CMakeLists.txt`: remove `src/win/WinSearchQuery.cpp` (line 19); remove the tst_search block (lines 139-141); add the new sources to `wisp_core` (lines 13-39 template, alphabetical position `src/win/WinDirectoryWalk.cpp` after WinFullscreenGuard line 15 / `src/core/FileIndex.cpp` + `src/core/ScanService.cpp` in the core block); add tst_scan/tst_index blocks copying the tst_filesearch block (lines 143-145: `qt_add_executable` + `target_link_libraries(... Qt6::Core Qt6::Gui Qt6::Test wisp_core)` + `add_test`); add both to the `set_tests_properties` ENVIRONMENT_MODIFICATION list (lines 175-183). Suite count: 19 → 20 (remove tst_search, add tst_scan + tst_index).

---

## Shared Patterns

### 1. Injectable std::function seams (setter-injected, defaulted to no-op)
**Source:** `src/core/FileSearch.h:36-53`, `AppCatalog.h:49-63`, `LaunchController.h:37-44`, `ResultsModel.h:82-83`
**Apply to:** ScanService (ListFn/SettingsSource), FileIndex users, SettingsWindow scan section.
```cpp
using QueryFn = std::function<QueryResult(const QString &query)>;
void setQueryFn(QueryFn fn);                          // FileSearch.h:37,44 — set before dispatch, never mutated
using Scanner = std::function<QVector<AppEntry>()>;   // AppCatalog.h:49
```
Setters are empty-call-safe (`if (fn) m_fn = std::move(fn);` — LaunchController.cpp:56-60); worker lambdas CAPTURE COPIES of the seams at dispatch (FileSearch.cpp:121-124, AppCatalog.cpp:138) — the worker never touches UI-thread member state.

### 2. Q_PROPERTY + NOTIFY for QML-bound state
**Source:** `FileSearch.h:25-26` (`statusText`/`indexerOk` READ + NOTIFY stateChanged); `SettingsWindow.h:32-33`; `ResultsModel.h:26-28`
**Apply to:** ScanService state props; SettingsWindow scan props.
Rules verified in the codebase: NOTIFY fires only when the value changed (ResultsModel.cpp:356-358; FileSearch.cpp:193-196), getters read live state (SettingsWindow.h:52-53 "always fresh"), QML renders strings verbatim and never invents copy (FileSearch.cpp:200 comment; MainWindow.qml:756).

### 3. Worker/watcher discipline (AppCatalog)
**Source:** `AppCatalog.cpp:132-203`; `FileSearch.cpp:115-198`
**Apply to:** ScanService scans; FileIndex delta apply + persist.
- Worker owns the batch; outcome travels via `QFutureWatcher<T>`, completion handler on UI thread.
- Nothing may escape the worker: inner per-scanner catch + outer belt-and-braces (AppCatalog.cpp:156-175).
- Single-flight: `if (m_buildInFlight) return;` + completion clears (AppCatalog.cpp:133-136, 187).
- Generation staleness: `quint64` counter incremented on UI thread at dispatch, stale-drop on completion (FileSearch.cpp:117, 183-191).
- Parallelism: `QtConcurrent::run(m_pool ? m_pool : QThreadPool::globalInstance(), worker)` (FileSearch.cpp:176-178) — scans use a DEDICATED low-priority pool (Pitfall 9).

### 4. Test seams (making the untestable testable)
**Source:** `FileSearch::setPool` (FileSearch.h:50-53); LaunchHistory explicit settingsPath ctor (LaunchHistory.h:19-20); WinStartMenuEnumerator::scanRoots (WinStartMenuEnumerator.h:26-30); AppCatalog setRefreshInterval (AppCatalog.h:51).
**Apply to:** ScanService::setPool + SettingsSource; FileIndex indexPath ctor.

### 5. Path normalization for keys
**Source:** `LaunchHistory.cpp:135-143` — `normalize()` = `QDir::toNativeSeparators`; keys = `group + '/' + normalize(path)`.
**Apply to:** FileIndex match keys, scan roots storage, dedupe set (case-folded native path per Pitfall 5).
```cpp
QString LaunchHistory::normalize(const QString &path) const { return QDir::toNativeSeparators(path); }
```

### 6. Dedupe by case-folded identity
**Source:** `AppCatalog.cpp:22-50` (dedupeLnkOverUwp — folded name hash, first-wins pass order).
**Apply to:** D-03 in ResultsModel::mergeFiles (folded targetPath set from app rows; UWP empty-path rows never enter the set — Pitfall 10); FileIndex delta insertion (path QSet).

### 7. Status copy single-homing
**Source:** `FileSearch.cpp:200-215` + MainWindow.qml:756 (QML renders verbatim, never invents).
**Apply to:** the new scan state strings. The D-04 prompt copy follows the 03/04 empty-state tone (MainWindow.qml:722-737 style: plain declarative, textSecondary, no accents).

### 8. Native dialogs
**Source:** `main.cpp:124-127` (`QFileDialog::getOpenFileName` for "Add executable…").
**Apply to:** D-05 root picker = `QFileDialog::getExistingDirectory(nullptr, QStringLiteral(...), QDir::homePath())` — returns `/`-separated → normalize before storage (Pitfall 5).

---

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `src/win/WinDirectoryWalk.cpp` (the Win32 body) | win primitive | file-I/O | No raw `FindFirstFileExW` listing exists in the codebase (WinSearchQuery.cpp is the COM OLE DB walk — wrong pattern, being deleted). Use RESEARCH Common Operation 1 (lines 268-299) + qtbase qfilesystemiterator_win.cpp (cited) as the reference implementation; header shape follows WinSearchQuery.h/WinLaunch.h. |
| `src/core/FileIndex` persistence (QDataStream + QSaveFile binary file) | store | serialization | No binary-file persistence exists — everything persists via QSettings INI (LaunchHistory/SettingsStore/HotkeyManager). Only the FACTORY + normalize + dedupe shapes transfer; the serialization itself comes from RESEARCH Common Operation 2 (lines 301-317) + Pitfall 8 guards. |
| `src/core/FileIndex::queryCandidates` prefilter | store | transform | No in-memory index query exists. RESEARCH Common Operation 3 (lines 319-339) is the reference; correctness anchored to FuzzyMatcher subsequence semantics (tst_index cross-check vs tst_matcher, A3). |

**Planner note (research Open Questions to decide in-plan):** state enum rename {Idle,NoRoots,Scanning,Error} vs ordinal reuse (OQ2); Theme.settingsWindowHeight growth ~560 vs ScrollView (OQ1); skip-list contents (OQ3); interval bounds 1min-24h default 10 (OQ4); candidate cap ~100 (OQ5); first-root-added trigger via roots-changed signal (OQ6); per-root failure granularity (OQ7).

## Metadata

**Analog search scope:** `src/win/`, `src/core/`, `src/ui/`, `src/app/`, `qml/`, `tests/`, `CMakeLists.txt` — full codebase (structure read via glob; 18 files read in full; targeted grep for MainWindow.qml wiring lines).
**Files scanned:** 18 source/header files + 3 QML + 3 test suites + CMakeLists
**Pattern extraction date:** 2026-08-14
**Key verification:** FileSearch seam surface intact (h:36-53); statusText single home (cpp:200-215); worker discipline in AppCatalog.cpp:132-203; dedupe shape AppCatalog.cpp:22-50; QML exclusivity MainWindow.qml:708/752; settings budget Theme.qml:86-89 + SettingsWindow.qml:111-112; CMake blocks lines 13-39/139-145/175-183; main.cpp wiring lines 24/98-128/204-208/245-246.