# Phase 7: Self-Managed File Scan - Research

**Researched:** 2026-08-14
**Domain:** Windows directory indexing, incremental background scanning, persistent index, Qt6/C++ codebase integration
**Confidence:** HIGH (codebase-verified contracts); MEDIUM on two Qt-perf claims (flagged in Assumptions Log)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** **Windows Search is removed, not fallbacked.** `src/win/WinSearchQuery.*` (OLE DB COM, ISearchQueryHelper, AQS) is deleted; `FileSearch`'s `QueryFn`/`StatusFn` seams are re-wired to the local index instead (the 04 status-row states Disabled/Building/Unavailable are superseded by scan states — exact state set is planner's call, must never show a blank dead-end per 04 D-16..D-18 spirit).
- **D-02:** **.exe-only file entries** (04 D-09 continues) **plus folder entries** (04 D-04 continues — folders rendered with the distinct text glyph, Enter opens in Explorer). No other file types.
- **D-03:** **Dedupe across sources, app row wins:** if a scanned .exe duplicates an app-catalog entry (Start Menu lnk/UWP/tracked/added) at the same resolved path, one row renders — the catalog row (icon, display name) survives, the scanned-file row is suppressed. Catalog-vs-UWP dedupe (03) remains as-is.
- **D-04:** **No default roots.** Fresh install / empty roots = empty file index + a friendly non-selectable status row prompting the user to open Settings and add scan locations (follows 04 D-18 status-row pattern; copy follows 03 empty-state style).
- **D-05:** Roots are **user-selected directories/drives only** (no wildcards, no file patterns). Native folder picker — `QFileDialog::getExistingDirectory` precedent exists at `main.cpp` (04 D-11 used a native file dialog for "Add executable…").
- **D-06:** **Fixed internal skip-list** (e.g. `Windows`, `ProgramData`-style system/noisy dirs, hidden & system attribute dirs skipped) during the walk. NOT user-editable in v1.
- **D-07:** **Persistent index on disk** (e.g. `%APPDATA%\TID\wisp\` — JSON or compact format, planner's call; must load in well under launch budget). In-RAM: paths + match keys only. Existing `QSettings` INI stays the small-settings store; the index is a separate file, NOT in the INI.
- **D-08:** **Incremental re-walk:** per-directory `LastWriteTime` memoization — only changed directories re-walk on interval scans. Live adds via "Scan now" and intervals run on a low-priority worker (COM-not-required for `FindFirstFile`-style walking); typing/query path never blocked (04 worker + generation-counter pattern extends).
- **D-09:** **Interval + manual:** default **10 minutes** (adjustable in Settings; min/max planner's call), plus a **"Scan now"** button. Background behavior: first scan after roots are added and on interval; app relaunch loads the persisted index instantly (D-07) — no full re-walk at startup.
- **D-10:** **"Scan locations" section lives in the existing Settings window** (SettingsWindow.qml + C++ controller, injected-collaborator pattern per Phase 6): root list with add/remove, interval selector, "Scan now", last-scan time + entry count. No separate page/window.

### OpenCode's Discretion
- Index file format + layout (JSON vs compact binary; atomic writes), exact incremental-walk data structure (mtime table), skip-list exact contents, scan state set + copy (4/7 states mapped to the 04 status-row slot), worker/pool mechanics, Settings-section layout details, dedupe key (normalized full path), how "Add executable…" (D-11 04) interacts with scanned roots (planner: keep as-is — it remains available for unscanned dirs).

### Deferred Ideas (OUT OF SCOPE)
- **Non-.exe file search** (documents/media) — still deferred; the .exe+folder filter is unchanged.
- **User-editable skip lists / ignore patterns** — fixed internal list only in v1 (D-06).
- **Drive/partition wildcard roots** ("scan D:\") — v1 roots are explicit directories/drives only; picking a drive in the folder picker covers whole partitions.
- **Recency ranking + recent apps on empty query** (LAUN-07/08, v2) — unchanged; wisp launch-tracking from 04 D-10 still feeds this later.
- **Windows Search re-adoption as an optional backend** — knowingly rejected (D-01); revisit only if user asks.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description (REQUIREMENTS.md) | Research Support |
|----|-------------------------------|------------------|
| LAUN-02 | File search: typing fuzzy-matches indexed files; Enter launches with the default app | Local index replaces the Windows Search backend: `src/core/FileIndex` (persisted entries) + index query candidates prefilter (FuzzyMatcher subsequence guarantee, verified) feeding the existing `FileSearch` → `ResultsModel::mergeFiles` pipeline; launch path unchanged (`LaunchController`, tst_launch regression) |
| LAUN-03 | Ctrl+Enter opens the containing folder in Explorer | Folder rows keep `isFolder=true` (04 D-04); scan produces folder entries; folder-open behavior unchanged in `LaunchController` (tst_launch regression) |
</phase_requirements>

## Summary

Phase 7 replaces the Windows Search COM/OLE DB backend (removed, not fallbacked — D-01) with wisp's own index of user-selected directories. The existing Phase-4 pipeline — `FileSearch` (150ms debounce, generation counter, worker/watcher) → `ResultsModel::setFileResults`/`mergeFiles` (rank, `kMaxFileRows=5` cap, `kPathMatchScore` tier) → QML rows — survives untouched except the seam targets in `main.cpp` and the locked status copy. Everything new sits behind two seams: a **per-directory enumeration seam** (raw Win32 `FindFirstFileExW`, because Qt 6.8+ `QDirIterator`/`QDirListing` regressed to a per-entry `GetFileAttributesExW` call making it ~15× slower than platform code — Qt Forum 161349, and `std::filesystem` benchmarks at par with `GetFileAttributesEx`, ~130× slower than `FindFirstFileEx` uncached — cppstories 2024), and the existing **`QueryFn`/`StatusFn` seams** re-wired to the index.

Architecture: `src/win/WinDirectoryWalk.{h,cpp}` returns plain per-directory listings (path, attrs, `ftLastWriteTime` — all free from `WIN32_FIND_DATA`, no COM, no `CoInitializeEx`); `src/core/FileIndex.{h,cpp}` owns the in-RAM entries (paths + match keys only, D-07) plus the per-directory mtime memo and the pure delta logic (changed/new/deleted dirs); `ScanService` (or the planner's chosen orchestrator) does single-flight, low-priority worker scans on interval / "Scan now" / first-root-added, persisting via **`QDataStream` + `QSaveFile`** (atomic; ~2× faster than JSON per Qt's own serialization blog) to a separate file in `%APPDATA%\TID\wisp\` — never the INI (D-07). Dedupe (D-03) lands in `ResultsModel::mergeFiles` where the full merged view already exists: suppress scan rows whose case-folded path matches a catalog/tracked/added row path; UWP rows have no path and can never collide.

Two verified integration facts the planner must handle: (1) `qml/MainWindow.qml` lines 708/752 make the empty-state and the status row **mutually exclusive** — the D-04 no-roots prompt needs a new empty-query branch, since the status row currently only renders when `query !== "" && !fileSearch.indexerOk`; (2) `qml/SettingsWindow.qml` is a **fixed 480×360 window** whose 318px content budget is fully consumed (24+18+12+64+12+88+12+64+24) — the D-10 scan-locations section requires growing the window height (Theme tokens) or scrolling; growth is recommended (see Open Questions).

**Primary recommendation:** Implement the self-managed index as a per-directory Win32 enumeration seam (`src/win/WinDirectoryWalk` — `FindFirstFileExW`, `FindExInfoBasic`, `FIND_FIRST_EX_LARGE_FETCH`) + pure delta/storage logic in `src/core` (FileIndex + ScanService) + `QDataStream`/`QSaveFile` persistence + model-side dedupe in `mergeFiles`; delete `src/win/WinSearchQuery.*` and `tests/tst_search.cpp`; re-wire the existing seams in `main.cpp`. No new third-party or Qt-module dependencies.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Directory enumeration (attrs, mtimes, names) | `src/win/` (WinDirectoryWalk) | — | Raw Win32 `FindFirstFileExW` gives attrs+time free; Qt 6.8+ QDirIterator regression (per-entry GetFileAttributesExW); firewall keeps Win32 out of core |
| Incremental delta (mtime memo compare, skip-list, recursion) | `src/core/` (FileIndex/ScanService) | — | Pure logic over the enumeration seam — unit-testable with a fake listDir; no OS calls |
| Index persistence (save/load, atomic) | `src/core/` (FileIndex) | — | QDataStream + QSaveFile are pure Qt; versioned header; load on UI thread at startup (well under launch budget) |
| Scan orchestration (interval timer, single-flight, low-priority worker, state) | `src/core/` (ScanService) | — | AppCatalog worker/watcher discipline; UI-thread timer, worker execution, watcher handoff |
| Query candidates (prefilter over index) | `src/core/` (FileIndex::queryCandidates) | — | Pure subsequence prefilter (superset of model acceptance — verified against FuzzyMatcher); model re-ranks |
| Rank + cap + dedupe (D-03) | `src/core/` (ResultsModel::mergeFiles) | — | mergeFiles already owns the merged display order; path-set dedupe is a `QSet<QString>` pass |
| Roots/interval config + scan-locations UI | `src/ui/` SettingsWindow controller + SettingsStore | qml/SettingsWindow.qml | UI-thread-only INI store (verified: no mutex, per CONTEXT); worker gets snapshot copies, never touches the store |
| Status/state contract | `src/core/` (FileSearch StatusFn remap) | qml/MainWindow.qml | QML consumes only statusText/indexerOk (verified lines 708-756); copy single-homed in FileSearch.cpp (verified lines 200-215) |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| **No new libraries** | — | — | Everything needed ships in the existing toolchain: Qt 6.11.1 (Core/Concurrent, already linked), Windows SDK (FindFirstFileExW in kernel32) |
| Qt Core: QDataStream | Qt 6.11.1 (present) | Index serialization | Binary, fast, documented; with a magic header + u32 version it is a stable internal format. Qt's own serialization benchmark: `QJsonDocument::toBinaryData` ~2× faster than `toJson` (125ms vs 263ms per 10k messages) |
| Qt Core: QSaveFile | Qt 6.11.1 (present) | Atomic index writes | The Qt-idiomatic temp-file + commit; no hand-rolled atomic-write logic |
| Qt Core: QThreadPool + QRunnable | Qt 6.11.1 (present) | Low-priority scan worker | Dedicated pool (WR-05 precedent in FileSearch) — QThreadPool-managed thread lowers its own priority inside the job |
| Qt Gui: QFileDialog | Qt 6.11.1 (present) | Native folder picker (D-05) | Precedent at main.cpp:124-127 ("Add executable…"); `getExistingDirectory` is the native dialog |
| Win32 FindFirstFileExW | Windows SDK (present) | Directory enumeration | `FINDEX_INFO_LEVELS` Basic + `FIND_FIRST_EX_LARGE_FETCH`; fastest documented enumeration (see Sources) — this is the "FindFirstFile-style walking" D-08 names |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| QTimer | Qt 6.11.1 (present) | Interval tick (D-09) | UI-thread single-shot; triggers a scan dispatch with roots snapshot |
| QStandardPaths::AppDataLocation | Qt 6.11.1 (present) | Index file location | Resolves to `%APPDATA%\TID\wisp\` (org TID / app wisp, main.cpp:53-54) — same folder as wisp.ini |
| Qt Test (existing) | Qt 6.11.1 (present) | Unit/integration tests | 17 suites registered in CMakeLists.txt; ctest 17/17 baseline (STATE.md) |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Raw `FindFirstFileExW` walker (src/win/) | `QDirIterator` / `QDirListing` (Qt 6.8+) | **Qt 6.8+ regression:** QDirListing makes a per-entry `GetFileAttributesExW` call on top of FindNextFileW — measured ~15× slower than platform code (298ms vs 4600ms for 85k files, Qt Forum 161349; project runs Qt 6.11.1). Qt's own `QFileSystemIterator` (the fast 6.5 path) is internal, not public API. |
| Raw `FindFirstFileExW` walker | `std::filesystem::recursive_directory_iterator` | Per-entry `status()`/`is_directory()` calls; benchmarked at par with `GetFileAttributesEx`, up to ~130× slower than FindFirstFileEx uncached (cppstories 2024); 603ms vs 17,893ms for 25k files (SO 66517871 — cache caveat applies) |
| QDataStream binary index | QJsonDocument JSON | JSON is ~2× slower to write and ~20-30% larger, per Qt's serialization blog; only wins debuggability — a `qDebug`-dump option covers that |
| QDataStream binary index | Qt SQL / SQLite | Adds the Qt SQL plugin to the deployment (not currently linked) for a path+mtime table — YAGNI at this scale (a few MB binary file) |
| QSaveFile | Hand-rolled temp+rename | QSaveFile handles existence-check, permissions and atomic commit correctly across edge cases — never hand-roll |

**Installation:** None. No new Qt components, NuGet packages, or third-party libraries. CMakeLists.txt changes are: remove `src/win/WinSearchQuery.cpp` from `wisp_core` (line 19), add the new source files, replace the `tst_search` block (lines 139-141).

**Version verification:** `find_package(Qt6 6.11 ...)` in CMakeLists.txt line 7 and `C:\Qt\6.11.1\msvc2022_64` (build.ps1 default) — verified on this machine 2026-08-14. Windows SDK: vcvars64 sets `WindowsSdkDir` (CMakeLists.txt lines 48-56 consume it for C++/WinRT headers).

## Architecture Patterns

### System Architecture Diagram

```
                        ┌───────────────────────── SCAN FLOW (worker, low priority) ─────────────────────────┐
                        │                                                                                      │
 Settings (UI thread)   │   ScanService                     src/win seam (per-directory)                        │
 SettingsStore (INI)    │   ┌──────────────┐  snapshot      ┌───────────────────────┐                          │
 roots + interval ──────┼──▶│ dispatch()   │── roots copy ─▶│ winListDirectory(path)│── FindFirstFileExW ──────┼──► NTFS
   (UI-thread only,     │   │ single-flight │  + memo?      │   DirListing{entries, │   (Basic + LARGE_FETCH;   │   (dirs + .exe files)
    no mutex)           │   └──────┬───────┘                │    lastWriteMs, ok}   │    attrs+time from find   │
                        │          │ worker job             └───────────┬───────────┘    data)                  │
 Scan now / interval /  │          ▼                                     │                                       │
 first-root-added ──────┼──▶ FileIndex (pure): recursion + memo compare  │                                       │
                        │   · dir mtime == memo → skip listing/descend    │                                       │
                        │   · changed/new → list + walk; deleted → prune  │                                       │
                        │   · skip-list names + hidden/system/reparse     │                                       │
                        │   · apply delta to entries                       │                                       │
                        │   · QSaveFile(QDataStream) persist  ────────────┼──► index.dat (%APPDATA%\TID\wisp\)   │
                        │          │                                       │                                       │
                        │   watcher → UI: state, lastScan, entryCount  ◀───┘                                       │
                        └──────────┼──────────────────────────────────────────────────────────────────────────────┘
                                   │ stateOrdinal() (atomic) → StatusFn seam
        ┌──────────────────────────▼────────────────────── QUERY FLOW (unchanged, 04 D-12..D-15) ─────────────────┐
 typing  │  FileSearch (UI thread): 150ms debounce → generation++ → worker                                      │
        ─┼──▶  worker: StatusFn → state remap (Idle/NoRoots/Scanning/Error)                                      │
        ┌─┼──▶  QueryFn(q) → FileIndex::queryCandidates(q): subsequence prefilter on lowercased path              │
   QML  │ │    ResultsModel::setFileResults (generation + query-text guards) → mergeFiles:                       │
        │ │    FuzzyMatcher rank (name/path tiers) + kMaxFileRows=5 cap + D-03 dedupe (path set               ┌──┤
        │ └──  from m_entries; UWP has no path, never collides)                                              │  │
        └──────────────────────────────►  rows render  ◄──────────────────── MultiEffect/glyphs (unchanged) ───┘  │
                                                                                                               │
 Startup: main() loads index.dat synchronously (well under launch budget, D-07) — NO scan at boot (D-09) ───────┘
```

### Recommended Project Structure (new/modified/removed)
```
src/
├── win/
│   ├── WinDirectoryWalk.{h,cpp}   # NEW — per-directory FindFirstFileExW listing; plain Qt structs out (no Win32 in core)
│   └── WinSearchQuery.{h,cpp}     # DELETED under D-01 (with tests/tst_search.cpp + CMakeLists.txt line 19)
├── core/
│   ├── FileIndex.{h,cpp}          # NEW — entries (path + match key + isFolder), dir-mtime memo, load/save, queryCandidates
│   ├── ScanService.{h,cpp}        # NEW — interval QTimer, single-flight, low-priority worker, stateOrdinal, lastScan/entryCount
│   ├── FileSearch.{h,cpp}         # MODIFIED — enum renames {Ok,Disabled,Building,Unavailable}→{Idle,NoRoots,Scanning,Error} (optional, see Pitfall 6); locked statusText copy replaced; skip-branch mapping only
│   └── ResultsModel.cpp           # MODIFIED — mergeFiles gains D-03 path-set dedupe
├── ui/SettingsWindow.{h,cpp}      # MODIFIED — roots/interval/scanNow/lastScan props via injected controller
└── app/main.cpp                   # MODIFIED — remove WinSearchQuery wiring (lines 24, 98-122); wire FileIndex/ScanService seams
qml/
├── SettingsWindow.qml             # MODIFIED — "Scan locations" section (window height grows — see Open Questions)
└── MainWindow.qml                 # MODIFIED — empty-query no-roots prompt branch (line 708 region)
tests/
├── tst_search.cpp                 # DELETED (classifyCatalogStatus/isAllowedResult/buildWhereRestriction tested WinSearchQuery)
├── tst_scan.cpp                   # NEW — delta logic with fake listDir seam (memo compare, skip-list, prune, single-flight)
├── tst_index.cpp                  # NEW — serialization round-trip, corrupt-file recovery, queryCandidates prefilter
└── tst_model.cpp / tst_filesearch.cpp  # EXTENDED — D-03 dedupe; status/copy expectations
```

### Pattern 1: Per-Directory Enumeration Seam (firewall + testability)
**What:** The Win32 walker is a single pure function `winListDirectory(path) -> DirListing{lastWriteMs, entries[{name, isDir, hidden, system, reparse, lastWriteMs}], ok}` using `FindFirstFileExW` (FindExInfoBasic + FIND_FIRST_EX_LARGE_FETCH). All recursion, memo comparison, skip-list filtering, and delta application live in `src/core` against this seam — so `tst_scan` fakes the seam with a map of paths → listings and tests every branch (changed/new/deleted dirs, hidden/system/reparse skips, skip-list, first-scan-import) with zero filesystem dependence. No COM, no `CoInitializeEx` (D-08 names this explicitly: "COM-not-required for FindFirstFile-style walking").
**When to use:** Always — this is the direct answer to both "only changed directories re-walk" (D-08) and the project's firewall/testability conventions.
**Example:**
```cpp
// src/win/WinDirectoryWalk.h — the entire Win32 surface. Plain Qt types only.
struct WinDirEntry {
    QString name;
    bool isDir = false, hidden = false, system = false, reparse = false;
    qint64 lastWriteMs = 0; // FILETIME → ms, FormatDifferent: always via the same helper (memo comparability)
};
struct WinDirListing { QVector<WinDirEntry> entries; qint64 lastWriteMs = 0; bool ok = false; };
WinDirListing winListDirectory(const QString &path); // src/win/WinDirectoryWalk.cpp
// Source: pattern adapted from cppstories 2024 FindFirstFileEx benchmark + qtbase qfilesystemiterator_win.cpp
```

### Pattern 2: Snapshot + Single-Flight Scan Job (AppCatalog discipline)
**What:** Root list + interval are copied on the UI thread at dispatch time; the worker job mutates only `FileIndex` (single writer); results come back via `QFutureWatcher` on the UI thread (exact AppCatalog/FileSearch pattern). A pending flag means an interval tick during a scan is dropped, never queued. The scan job lowers its own priority: inside the runnable, `QThread::currentThread()->setPriority(QThread::LowPriority)` (the pool's own threads keep normal priority; the job body drops itself). Queries (FileSearch workers on the global pool) are never blocked: different pool, and `FileIndex` reads use an immutable snapshot swap (copy-on-write on delta apply).
**When to use:** Any scan trigger — interval, Scan now, first-root-added — funnels into one dispatch.
**Example:**
```cpp
// main.cpp wiring sketch (planner's call on exact class names):
scanService.setListFn(&WinDirectoryWalk::winListDirectory);  // seam (tests fake it)
scanService.setSettingsSource([&store] {                     // UI-thread snapshot — never the store on a worker
    return ScanSettings{ store.scanRoots(), store.scanIntervalMinutes() };
});
fileSearch.setStatusFn([&scanService] { return scanService.stateOrdinal(); }); // map {Idle→Ok, NoRoots→Disabled, Scanning→Building, Error→Unavailable}
fileSearch.setQueryFn([&index](const QString &q) {           // replaces the WinSearchQuery lambda (main.cpp:98-112)
    const QVector<AppEntry> rows = FileIndex::toEntries(index.queryCandidates(q), /*candidateCap=*/100);
    return FileSearch::QueryResult{ rows, /*failed=*/false };
});
```

### Pattern 3: Status Seam Remap (no blank dead-end, D-01/D-04)
**What:** Same 4 ordinals, new semantics: `{Idle, NoRoots, Scanning, Error}` (renaming the `FileSearchState` members is safe — QML only sees `statusText`/`indexerOk` strings; verified MainWindow.qml renders `fileSearch.statusText` verbatim, line 756). Locked copy stays single-homed in `FileSearch::statusText()` (verified FileSearch.cpp:200-215 — "the SINGLE home"). `Scanning` mirrors the old `Building` contract: "Building stays Building — the query still runs" (FileSearch.cpp:156) — queries keep working against the loaded index while a scan is in flight. Suggested copy (planner's call, 03/04 empty-state tone):
- NoRoots: `"No scan locations yet — add folders in Settings to search files"`
- Scanning: `"Scanning — files appear as they're found"`
- Error: `"Scan unavailable — check your scan locations in Settings"`
**When to use:** The status row visibility pairing must change — verified MainWindow.qml:752 shows the row only when `query !== "" && !indexerOk`; the D-04 prompt needs an **empty-query branch** (`resultsModel.query === "" && !fileSearch.indexerOk` → prompt row, mutually exclusive with line 708's empty-state).

### Pattern 4: Model-Side Dedupe (D-03, app row wins)
**What:** `ResultsModel::mergeFiles` already owns the merged order of app rows (`m_entries`) and file rows (`m_fileEntries`). Add: build `QSet<QString>` from every catalog row's `targetPath` case-folded (Lnk targets, tracked, added — UWP rows have empty targetPath and can never collide), then drop file-channel rows whose case-folded targetPath is in the set. Catalog row renders with its icon/display name; scanned row suppressed. Overlapping roots producing duplicate scan rows are handled in the delta apply (path `QSet` during insertion).
**When to use:** Always — this is where display truth lives; unit-test in tst_model.

### Anti-Patterns to Avoid
- **QDirIterator / QDirListing for the bulk walk:** Qt 6.8+ adds a per-entry `GetFileAttributesExW` — ~15× slower than raw Win32 (Qt Forum 161349; project on Qt 6.11.1). Qt's fast internal iterator is not public API.
- **Worker threads touching SettingsStore:** verified no mutex (CONTEXT code_context); snapshots on the UI thread, always.
- **Re-walking everything at startup:** violates D-09 "relaunch loads the persisted index instantly"; startup loads the index file synchronously.
- **Writing the index from the UI thread:** persist on the scan worker (QSaveFile is thread-agnostic).
- **New JSON dependency debates:** QDataStream + version header is the compact-format answer (D-07); JSON only if debuggability is prioritized (planner's call — research recommends QDataStream).
- **Hand-rolled atomic writes / INI for the index:** QSaveFile for atomicity; index is a separate file per D-07; QSettings stays the small-settings store.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Atomic index file commit | temp-file + rename by hand | `QSaveFile` (Qt Core) | Handles existing-file edge cases, permissions, and commit semantics correctly; documented Qt pattern |
| Native folder picker (D-05) | Custom QML directory browser | `QFileDialog::getExistingDirectory` | Existing precedent main.cpp:124-127; native Windows dialog; handles drive roots (`D:\`) |
| Binary serialization format | Byte-level manual packing | `QDataStream` with magic + version header | Field-count evolution safe; Qt's own benchmark shows binary ~2× JSON speed (Qt blog 2018) |
| Fuzzy matching/ranking | A second ranker | `FuzzyMatcher` (existing) | mergeFiles already ranks file rows (verified ResultsModel.h:118-121); the index only pre-filters |
| Recurring-scan scheduling | A custom scheduler thread | `QTimer` on the UI thread + worker dispatch | Single-shot restart pattern already proven (FileSearch kDebounceMs) |
| Directory recursion/skip logic on the Win32 side | Walker doing recursion + filtering | Core-side recursion over the per-directory seam | Unit-testable delta logic; the Win32 file stays a thin listing primitive (~60 lines) |

**Key insight:** Every "hard" piece of this phase (atomic persistence, native dialogs, ranking, threading) already exists in the codebase or Qt Core. The only genuinely new code is the per-directory Win32 listing (which cannot be delegated — both Qt wrappers and std::filesystem benchmark significantly slower) and the pure delta logic (which is exactly the kind of logic that should be hand-written and unit-tested).

## Common Pitfalls

### Pitfall 1: Qt 6.8+ QDirIterator/QDirListing performance regression
**What goes wrong:** Walk of 85k files: ~4,600ms via QDirListing vs ~298ms platform code (Qt Forum 161349). The project runs Qt 6.11.1 — this applies.
**Why it happens:** Modern QDirListing performs a `GetFileAttributesExW` per entry **in addition to** FindNextFileW.
**How to avoid:** Raw `FindFirstFileExW` (FindExInfoBasic + FIND_FIRST_EX_LARGE_FETCH) behind the src/win firewall. Validation: quick enumeration benchmark of a large tree during Wave 0 (A1 in Assumptions Log).
**Warning signs:** A scan that pegs CPU per-entry; profiler shows GetFileAttributesExW in hot path.

### Pitfall 2: Per-entry stat calls (std::filesystem / QFileInfo misuse)
**What goes wrong:** `std::filesystem::recursive_directory_iterator` + `is_directory()` costs ~130× FindFirstFileEx uncached (cppstories 2024); 603ms vs 17,893ms on 25k files (SO 66517871).
**How to avoid:** Use the find-data only — attrs (`FILE_ATTRIBUTE_DIRECTORY/HIDDEN/SYSTEM/REPARSE_POINT`) and `ftLastWriteTime` come free with `WIN32_FIND_DATA`. Never call GetFileAttributesEx/status() per entry (LLVM's libsupport made the same fix: 4 min → 2 s on 520k files).

### Pitfall 3: Settings window is a fixed 480×360 with zero remaining vertical budget
**What goes wrong:** The D-10 scan-locations section doesn't fit — verified qml/SettingsWindow.qml line 111-112 budget comment: 24+18+12+64+12+88+12+64+24 = 318 of 328px consumed by hotkey/accent/autostart rows.
**How to avoid:** Grow `Theme.settingsWindowHeight` (token change; ~480×560 accommodates a 120px root list + controls) — OR a ScrollView if the planner finds growth unacceptable against UI-SPEC. Roots list itself: Repeater inside a height-capped ScrollView (never a full-height ListView in a fixed window).
**Warning signs:** QML rows clipped by `clip: true` on the surface (line 109).

### Pitfall 4: UI-thread-only stores read from the worker
**What goes wrong:** Scanner reads roots from SettingsStore on a worker — data race (no mutex, verified CONTEXT code_context).
**How to avoid:** Snapshot pattern (Pattern 2): copy roots+interval on the UI thread at dispatch; workers never touch QSettings. Also note: `QSettings` docs declare it reentrant (per-instance per-thread) — the shared-instance hazard stands regardless.

### Pitfall 5: Path separators / case in keys
**What goes wrong:** `QFileDialog::getExistingDirectory` returns `/`-separated paths; INI groups split on `/` (LaunchHistory precedent: paths native-normalized before storage); dedupe key case drift.
**How to avoid:** Normalize with `QDir::toNativeSeparators` when storing roots; dedupe key = case-folded native path (LaunchHistory::makeSettings precedent, verified LaunchHistory.cpp). `QStringList` array values in QSettings keep `/` fine inside values — normalization is for key-equality and dedupe consistency, not storage.

### Pitfall 6: Reparse points / junction cycles
**What goes wrong:** A junction loop (e.g., C:\Users subtree links) makes a naive recursive walk loop forever; duplicates inflate the index.
**How to avoid:** In the core recursion and/or the Win32 listing, never descend into entries with `FILE_ATTRIBUTE_REPARSE_POINT` (find-data bit — free). Symlinked dirs are thus indexed once at their in-root location or skipped; D-06's hidden/system skip is the same single bit-test from the same struct.

### Pitfall 7: Long paths (>260 chars)
**What goes wrong:** FindFirstFileEx fails with ERROR_PATH_NOT_FOUND on deep trees; Qt's own iterator auto-prefixes `\\?\` on Windows ("MSVC2015+ case we prepend //?/ for longer file-name support" — qtbase qfilesystemiterator_win.cpp), proving the need at this level.
**How to avoid:** In `winListDirectory`, when `path + "\\*"` exceeds MAX_PATH (or always with absolute paths), prefix `\\?\` (and `\\?\UNC\` for UNC). Optionally add `longPathAware` to wisp.rc's manifest (verified: wisp.rc currently has no manifest) — prefixing in the walker is the robust fix; the manifest is belt-and-braces for other APIs (e.g., QFileDialog returns). Paths beyond `\\?\`'s 32,767 limit: skip with a debug log.

### Pitfall 8: Corrupt / missing / legacy index file
**What goes wrong:** Crash or manual delete leaves the file unreadable; a format change (version bump) must not brick the launcher.
**How to avoid:** Header check (magic + version) on load; on any mismatch or stream error: start empty, trigger a first scan, overwrite atomically via QSaveFile. Unit-test: round-trip, truncated file, wrong-version file.

### Pitfall 9: Scan competition with typing (never block the query path)
**What goes wrong:** A full walk holds the FileSearch worker pool or mutates the index mid-query; keystrokes stutter — kills the product's core promise.
**How to avoid:** Dedicated low-priority pool for scans (Pattern 2), copy-on-write index snapshot for readers, single-flight flag, generation counter on scan results (interval result discarded if a newer scan started — mirror D-15).

### Pitfall 10: UWP rows and the dedupe key
**What goes wrong:** Naive "dedupe by targetPath" code sees UWP rows with empty paths and accidentally suppresses scan rows.
**How to avoid:** Build the dedupe path-set from path-bearing rows only (Lnk/tracked/added); UWP rows (aumid only) never enter the set (D-03: "same resolved path").

## Code Examples

### Common Operation 1: Per-directory Win32 listing (the walker's entire surface)
```cpp
// src/win/WinDirectoryWalk.cpp — FindFirstFileExW, FindExInfoBasic + FIND_FIRST_EX_LARGE_FETCH.
// Adapted from the cppstories.com/2024/cpp-query-file-attribs-faster benchmark pattern;
// find-data facts (attrs, ftLastWriteTime) came free with the listing.
WinDirListing winListDirectory(const QString &path)
{
    WinDirListing out;
    const QString pattern = longPathPrefix(path) + path + "\\*"; // Pitfall 7
    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFileExW(reinterpret_cast<const wchar_t *>(pattern.utf16()),
                                FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE)
        return out; // out.ok = false
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        WinDirEntry e;
        e.name = QString::fromWCharArray(fd.cFileName);
        e.isDir = fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        e.hidden = fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN;
        e.system = fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM;
        e.reparse = fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT; // Pitfall 6
        e.lastWriteMs = fileTimeToMs(fd.ftLastWriteTime);               // one shared helper
        out.entries.append(e);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    out.ok = true;
    return out;
}
```

### Common Operation 2: Atomic index persistence (QDataStream + QSaveFile)
```cpp
// src/core/FileIndex.cpp — magic + version header; load-failure → empty + rescan (Pitfall 8).
bool FileIndex::save(const QString &path) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_6_0);
    out << quint32(0x57535031) << quint32(kFormatVersion); // magic "WSP1" + version
    out << m_roots;                 // QStringList (informational; authoritative copy is the INI)
    out << m_dirMtimes;             // memo: QHash<QString, qint64>
    out << m_entries;               // QVector<IndexEntry{path, displayName, isFolder}>
    return file.commit();           // atomic replace
}
```

### Common Operation 3: Query candidates — subsequence prefilter (superset guarantee)
```cpp
// src/core/FileIndex.cpp — correctness argument: FuzzyMatcher::score(q, name) > 0
// ⟺ q is a case-insensitive subsequence of name (verified src/core/FuzzyMatcher.cpp:
// linear scan, score 0 otherwise). displayName is the final path component, hence any
// name-subsequence is a path-subsequence — a subsequence check on the lowercased
// FULL PATH can only overshoot, never miss a row mergeFiles would accept; the model
// re-ranks and caps at kMaxFileRows=5 (ResultsModel.h:140-141).
QVector<IndexEntry> FileIndex::queryCandidates(const QString &query) const
{
    const QString sub = query.toCaseFolded();
    QVector<IndexEntry> out;
    for (const IndexEntry &e : m_entries) {                 // linear scan over RAM copy
        if (isSubsequence(sub, e.matchKey.toCaseFolded()))  // matchKey = native lowercased path
            out.append(e);
        if (out.size() >= kCandidateCap)                    // ~100 — planner's call
            break;
    }
    return out; // empty query → empty (D-14: the model renders the added-only default list)
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Windows Search COM/OLE DB backend (Phase 4) | Self-managed index of user-chosen roots | Phase 7 (this phase) | D-01: removed, not fallbacked; hits files Windows Search misses (user's motivating case) |
| QDirIterator / QDirListing for walking | Raw FindFirstFileExW (Basic + LARGE_FETCH) | Qt 6.8 (2024); this project picks raw since it runs 6.11.1 | ~15× enumeration speed; find-data attributes/time free |
| std::filesystem::directory_iterator | FindFirstFileExW | Always (benchmarked 2021-2024) | Up to ~130× uncached; per-entry status() eliminated (LLVM r315378: 4 min → 2 s, 520k files) |
| JSON index | QDataStream binary (magic + version) | This phase (planner's call; research recommends binary) | ~2× write speed, ~20-30% smaller (Qt blog 2018); QSaveFile atomicity |

**Deprecated/outdated:**
- `FileSearchState` semantics `{Ok, Disabled, Building, Unavailable}`: superseded by scan states (D-01); recommend renaming members `{Idle, NoRoots, Scanning, Error}` — QML never sees the enum (verified), only statusText/indexerOk strings.
- `WinSearchQuery::{queryFiles, checkIndexStatus, classifyCatalogStatus, isAllowedResult, buildWhereRestriction}` + `tst_search.cpp`: deleted wholesale (D-01); check `tst_search` coverage before removal (CONTEXT: 04 tests 13/13 included it) — the new pure-logic targets are incremental-walk + dedupe (CONTEXT code_context).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | The Qt 6.8+ QDirListing per-entry `GetFileAttributesExW` regression is present in 6.11.1 (Qt Forum measurement was against 6.8) | Pitfall 1 | Correct only in degree: raw Win32 remains the right call regardless (FindFirstFileEx is the documented-fast path in every source); mitigation: Wave-0 micro-benchmark of one big tree |
| A2 | NTFS directory `ftLastWriteTime` changes whenever a direct child is added/removed/renamed (basis of D-08 memo) | Pattern 1 | If some tooling preserves timestamps (backup restores), a changed subtree could be missed until a full rescan — acceptable rarity; "Scan now" + interval covers drift |
| A3 | `FuzzyMatcher::score > 0 ⟺ case-insensitive subsequence` (verified by reading FuzzyMatcher.cpp: linear subsequence scan, score 0 = no match) — the prefilter never misses a row the model accepts, because displayName ⊂ path | Code Example 3 | If scoring semantics ever change, prefilter must change with it (guarded by tst_index + tst_model cross-checks) |
| A4 | Loading a ~50-100k-entry QDataStream index takes well under the launch budget (~tens of ms) | D-07 | If it measures slow, move load to the startup worker and gate the status row on "index ready" — planner should include the measurement in the plan's verification |

## Open Questions

1. **Settings window geometry (verified blocker):** 480×360 fixed with 318/328px consumed — the scan-locations section cannot fit without change.
   - What we know: qml/SettingsWindow.qml:111-112 budget; rows are token-driven; UI-SPEC calls the window "fixed 480×360".
   - What's unclear: whether growing `Theme.settingsWindowHeight` (recommended, ~480×560) violates the UI-SPEC Geometry contract or whether a ScrollView is preferred.
   - Recommendation: grow the token (the UI-SPEC governs the launcher shell; the settings surface is a 06-era token) and verify in the UI-phase gate; fall back to ScrollView if the spec review objects.

2. **State enum rename vs ordinal reuse:**
   - What we know: QML consumes only strings; `stateFromOrdinal` maps defensively.
   - Recommendation: rename members `{Idle, NoRoots, Scanning, Error}` — clarity wins, zero QML impact; main.cpp maps new → `FileSearchState` explicitly (existing switch pattern, main.cpp:114-122).

3. **Skip-list exact contents (D-06):** attribute-based skips (hidden/system dirs, reparse points) are free and fixed; name-based list is planner's call. Suggested seed set (case-insensitive, leaf-name match): `Windows`, `ProgramData`, `AppData`, `WindowsApps`, `node_modules`, `.git`, `.svn`, `.hg`, `.gradle`, `.m2`, `.cargo`, `$RECYCLE.BIN`, `System Volume Information`, `msocache`, `config.msi`. Rationale: the .exe-only filter already removes most noise; the list mostly saves walk time — keep it small and system-aligned, don't chase dev-tool chains the user may actually want indexed.

4. **Interval bounds (D-09):** recommend min 1 min / max 24 h, default 10 min (integer minutes; a QML SpinBox or combo of presets — planner's UI call).

5. **Candidate cap for queryCandidates:** recommend ~100 (model caps display at 5; the cap bounds per-keystroke work; linear scan of full index with early exit is fine at this scale).

6. **"Scan now" + first-root-added trigger placement:** first scan after the FIRST root is added (D-09) — recommend `ScanService` watches a roots-changed signal from the controller (UI thread) and dispatches; interval timer starts/stops with root count (no roots → no timer).

7. **Per-root failure granularity:** a root that's gone (removed USB drive) should degrade to the Error state with the prompt copy (or a per-root badge in Settings — nice-to-have), not a global "unavailable". Recommend: global Error state + lastScan summary shows failed roots count.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Qt 6.11.1 (Core, Concurrent, Gui, Quick, Qml, Widgets, Test) | Everything | ✓ | 6.11.1 at C:\Qt\6.11.1\msvc2022_64\bin (build.ps1 default; CMakeLists.txt line 7) | — |
| MSVC 2022 (v143) + vcvars64 | Toolchain | ✓ | VS2022 Community (build.ps1 vcvars64 path) | — |
| CMake + Ninja | Build | ✓ | CMake 4.3.4 | — |
| Windows SDK (FindFirstFileExW, FILETIME) | WinDirectoryWalk | ✓ | Via vcvars64 (WindowsSdkDir consumed at CMakeLists.txt:51) | — |
| ctest baseline | Validation | ✓ | 17/17 green (STATE.md) | — |

**Missing dependencies with no fallback:** None. **Missing dependencies with fallback:** None — the phase adds no external tools, services, or databases; the only "new" dependency is the Windows SDK's kernel32 (present on every Windows 10/11 target).

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Qt Test (QTest) via CTest — 17 suites registered (CMakeLists.txt:93-183) |
| Config file | none — CMake `include(CTest)` block; binaries get Qt DLL PATH via `ENVIRONMENT_MODIFICATION` (CMakeLists.txt:175-183) |
| Quick run command | `ctest --test-dir build/dev -R tst_filesearch --output-on-failure` (build dir verified in CMakeLists.txt:174) |
| Full suite command | `ctest --test-dir build/dev --output-on-failure` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|--------------|
| LAUN-02 | Index query candidates prefilter (subsequence superset, cap) | unit | `ctest --test-dir build/dev -R tst_index --output-on-failure` | ❌ Wave 0 — `tests/tst_index.cpp` (new) |
| LAUN-02 | FileSearch coordinator with index-backed fakes (generation, states, copy) | integration | `ctest --test-dir build/dev -R tst_filesearch --output-on-failure` | ✅ exists — update state/copy expectations (04-era Disabled/Building copy is asserted there) |
| LAUN-02 | Dedupe D-03: scan row suppressed when path matches catalog/tracked/added row; UWP never suppresses | unit | `ctest --test-dir build/dev -R tst_model --output-on-failure` | ✅ exists — extend with D-03 cases |
| LAUN-02 | Entry assembly: `.exe` rows carry displayName/targetPath; folder rows isFolder | unit | `ctest --test-dir build/dev -R tst_scan --output-on-failure` | ❌ Wave 0 — `tests/tst_scan.cpp` (new) |
| LAUN-02 | Index persistence: round-trip, version/magic guard, corrupt+truncated recovery, atomic save | unit | `ctest --test-dir build/dev -R tst_index --output-on-failure` | ❌ Wave 0 |
| LAUN-02 | Incremental walk: changed/new/deleted dirs via fake listDir; skip-list; hidden/system/reparse skip; single-flight | unit | `ctest --test-dir build/dev -R tst_scan --output-on-failure` | ❌ Wave 0 |
| LAUN-03 | Folder Ctrl+Enter reveal (isFolder rows) | regression | `ctest --test-dir build/dev -R "tst_launch|tst_filesearch" --output-on-failure` | ✅ exists — no new tests needed (folder rows unchanged) |
| Both | Deleted backend leaves no references | build gate | full build + suite (WinSearchQuery.cpp removed from wisp_core, line 19) | CMake change |

### Sampling Rate
- **Per task commit:** single targeted suite, e.g. `ctest --test-dir build/dev -R tst_scan --output-on-failure`
- **Per wave merge:** full suite `ctest --test-dir build/dev --output-on-failure`
- **Phase gate:** full suite green before `/gsd-verify-work` (baseline 17/17 must hold after tst_search removal; suite count changes to 18 with tst_scan+tst_index and no tst_search)

### Wave 0 Gaps
- [ ] `tests/tst_search.cpp` — DELETE with `src/win/WinSearchQuery.{h,cpp}` and the CMake block (lines 139-141); its 3 slots tested WinSearchQuery helpers only
- [ ] `tests/tst_scan.cpp` — NEW: delta logic over a fake listDir seam (Pattern 1); covers LAUN-02 walk semantics
- [ ] `tests/tst_index.cpp` — NEW: serialization round-trip + corrupt/version guards + queryCandidates prefilter
- [ ] `tests/tst_filesearch.cpp` — UPDATE: status-copy expectations (locked strings change; single home FileSearch.cpp:200-215)
- [ ] `tests/tst_model.cpp` — EXTEND: D-03 dedupe cases
- (No conftest equivalent — Qt Test needs none; `QSignalSpy::wait` with generous timeouts is the established async pattern in tst_filesearch)

## Security Domain

### Applicable ASVS Categories
| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | — local launcher, no identity |
| V3 Session Management | no | — |
| V4 Access Control | partial | Index lives in user profile (%APPDATA%); no elevated reads — scans run at user integrity level, same as Phase 4 |
| V5 Input Validation | yes | Query string is fuzzy-matched only (no SQL, no shell — Windows Search removal actually REMOVES the only query-injection surface the app had); root paths come from the native folder picker, normalized before use (Pitfall 5) |
| V6 Cryptography | no | Nothing secret is stored |
| V8 Protect Data | yes | Index file = file paths only (no content, no secrets); QSaveFile atomic writes; corrupt file degrades to empty+rescan, never crashes (Pitfall 8) |
| V14 Config | yes | Roots + interval in the existing INI (small-settings store, D-07); index versioned + magic-guarded |

### Known Threat Patterns for {stack: Win32 + Qt}
| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Junction/symlink loop in scanned roots (infinite walk, duplicate rows) | DoS | Never descend into `FILE_ATTRIBUTE_REPARSE_POINT` entries (Pitfall 6); directory recursion driven by core logic, depth-capped only by reparse skip |
| Tampered index file (path swap) | Tampering | User-scoped file, user-initiated launch, and the app already trusts user-writable .lnk targets in Phase 3 — threat parity, not new exposure; magic+version guard detects corruption (Pitfall 8) |
| Long-path denial (ERROR_PATH_NOT_FOUND on deep trees) | DoS | `\\?\` prefixing in the walker (Pitfall 7); failure of one listing degrades to Error state, never a hang |
| Scan starving the UI/typing path | DoS | Dedicated low-priority pool + single-flight + copy-on-write snapshot (Pitfall 9); the WM_HOTKEY/typing path is on the global pool and model, never on the scan pool |
| UWP ACL-locked dirs (WindowsApps) enumerated → permission errors | Info disclosure | `WindowsApps` on the skip-list (D-06 name list, Open Question 3) — sanctioned UWP inventory already comes from PackageManager (Phase 3); failed listings → Error state, not crash |

## Sources

### Primary (HIGH confidence)
- Codebase (read 2026-08-14): `src/core/FileSearch.{h,cpp}` (seams, enum, generation, single-homed status copy), `src/core/ResultsModel.{h,cpp}` (roles, mergeFiles, kMaxFileRows=5, kPathMatchScore=100), `src/core/FuzzyMatcher.cpp` (subsequence semantics — the prefilter guarantee), `src/core/AppEntry.h` (File-row shape), `src/core/LaunchHistory.cpp` (makeSettings INI + path normalization precedent), `src/core/AppCatalog.cpp` (dedupeLnkOverUwp + worker discipline), `src/ui/SettingsWindow.{h,cpp}` + `qml/SettingsWindow.qml` (injected controller, 480×360 budget), `src/app/main.cpp` (seam wiring lines 24/98-128, QFileDialog precedent), `qml/MainWindow.qml:708/748-756` (status-row visibility pairing), `CMakeLists.txt` (line 19 WinSearchQuery.cpp, lines 139-141 tst_search, test PATH setup), `src/app/wisp.rc` (no longPathAware)
- [VERIFIED: GitHub qt/qtbase] `src/corelib/io/qfilesystemiterator_win.cpp` — Qt's QFileSystemIterator uses FindFirstFileExW + FindExInfoBasic + FIND_FIRST_EX_LARGE_FETCH, fills metadata from find data, auto-prefixes `//?/` for long paths
- [VERIFIED: Qt Forum 161349 (2025-03-13)] QDirListing on Qt 6.8+ makes per-entry GetFileAttributesExW; measured QDirListing 4,600ms vs ~298ms platform code on 85k files
- [VERIFIED: cppstories.com 2024-08-11] FindFirstFileEx vs GetFileAttributesEx vs std::filesystem benchmark: FindFirstFileEx up to ~130× (uncached), std::filesystem ≈ GetFileAttributesEx
- [CITED: learn.microsoft.com/windows/win32/fileio/maximum-file-path-limitation] MAX_PATH 260; `\\?\` → 32,767; `\\?\UNC\` for UNC
- [CITED: learn.microsoft.com] FindFirstFileEx docs (FindExInfoBasic, FIND_FIRST_EX_LARGE_FETCH semantics)
- [CITED: doc.qt.io/qt-6/qsavefile.html, qdatastream.html, qfiledialog.html#getExistingDirectory, qsettings.html] QSaveFile atomic write; QDataStream versioning; native folder dialog; QSettings reentrancy (per-instance per-thread)
- [VERIFIED: gsd-sdk init.phase-op 07] phase state: has_research=false → this document; commit_docs=true; nyquist_validation=true

### Secondary (MEDIUM confidence)
- [CITED: qt.io/blog/serialization-in-qt (2018)] QJsonDocument::toJson 263ms/10k msgs vs toBinaryData 125ms — binary ~2× faster; JSON ~346 vs 290 bytes/msg — ratios stable across Qt 6
- [VERIFIED: Stack Overflow 66517871] FindFirstFileW 603ms vs std::filesystem 17,893ms on 25k files (with the answerer's cache-order caveat — direction confirmed by cppstories)
- [VERIFIED: blog.s-schoener.com 2024-06-09] FIND_FIRST_EX_LARGE_FETCH ~2× walltime on HDD enumeration (single machine)
- [VERIFIED: lists.llvm.org r315378 (2017)] directory_iterator::status() from find data: 4 min → 2 s on 520k files — the "no per-entry status()" principle

### Tertiary (LOW confidence)
- None — A1 (Qt 6.11 exactness of the QDirListing regression) is flagged in the Assumptions Log instead of being presented as fact.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no new dependencies; every named API verified present on this machine (Qt 6.11.1, Windows SDK via vcvars64) or in the codebase
- Architecture: HIGH — every integration point (seams, status copy, mergeFiles, settings window budget, QML visibility pairing) verified by direct file reads this session
- Pitfalls: MEDIUM — A1 (Qt 6.8→6.11 regression presence) and A2 (NTFS mtime semantics) are sourced but not re-validated on this machine; both have safe fallbacks and Wave-0 validation steps

**Research date:** 2026-08-14
**Valid until:** 2026-09-13 (30 days — Qt/Win32 APIs stable; the QDirListing regression claim could shift if Qt changes internals, re-check at planning execution only if Qt version changes)