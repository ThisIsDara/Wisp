#pragma once

#include <QFutureWatcher>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QVector>

#include <chrono>
#include <functional>
#include <vector>

#include "core/AppEntry.h"

// In-memory app catalog (D-08/D-09/D-10) — the deduped, alphabetically
// sorted entry list every search and launch flows through. Built on a worker
// thread at startup so the WM_HOTKEY path only pays an age check + an
// implicit-shared copy (PITFALLS #14: first show stays <100ms while the scan
// runs). In-memory only by design (D-09) — no persistence of any kind.
//
// Threading contract (all D-08):
//  - Scanners run on QtConcurrent pool threads. The worker lambda OWNS the
//    thread's COM apartment for the batch: CoInitializeEx(COINIT_MULTITHREADED)
//    at batch start, CoUninitialize at batch end (both WinRT scanners assume
//    an initialized apartment — PITFALLS #3 / RESEARCH §2). This file
//    intentionally contains no WinRT headers; scanners are std::function
//    calls and the cppwinrt include stays confined to WinUwpEnumerator.cpp.
//  - The swap happens on the UI thread (QFutureWatcher queued completion):
//    dedupe (D-10: exact case-insensitive full-name collision → Lnk wins,
//    UWP suppressed) → alphabetical sort (D-03) → ONE snapshot swap.
//  - m_snapshot is QMutex-guarded: readers copy under lock (implicitly-shared
//    QVector = ~µs), the writer swaps under lock (T-03-03-01) — readers
//    always see one consistent version, never a partial list.
//  - Single-flight (T-03-03-02): overlapping start()/ensureFresh() calls
//    coalesce; exactly one refreshed() per completed swap.
//  - m_buildInFlight / m_lastBuilt / m_interval are UI-thread-only
//    (start/ensureFresh/onBuildFinished all run there).
class AppCatalog : public QObject
{
    Q_OBJECT

public:
    explicit AppCatalog(QObject *parent = nullptr);

    // Scanner injection — the SAME std::function mechanism as
    // LauncherController::setFullscreenGuard (PATTERNS §2). Default is empty;
    // production wires WinStartMenuEnumerator::scanStartMenu +
    // WinUwpEnumerator::scanUwpApps (03-05 main.cpp), tests inject fakes.
    using Scanner = std::function<QVector<AppEntry>()>;
    void setScanners(std::vector<Scanner> scanners);
    void setRefreshInterval(std::chrono::milliseconds interval); // test hook; default 10 min

    // Curation source injection (05.1) — SAME std::function mechanism as
    // Scanner above. Production wires CurationStore reads (main.cpp, 05.1-04);
    // tests inject fakes. Read once per build ON THE UI THREAD (watcher
    // completion) as a value-copy of two QSets — SettingsStore precedent, no
    // mutex; the worker NEVER touches the store (WR-01 discipline).
    struct CurationData {
        QSet<QString> hiddenIds; // user hides (id → CUR-02) — identity: Lnk targetPath, UWP aumid
        QSet<QString> shownIds;  // user overrides — wins over hiddenIds AND default rules
    };
    using CurationSource = std::function<CurationData()>;
    void setCurationSource(CurationSource source);

    // Kick the first build on the worker thread (called once at startup in
    // main.cpp — NEVER from the hotkey path; D-08).
    void start();

    // Checks build age; rebuilds on the worker ONLY if older than the
    // interval. Cheapest possible call — safe on the hotkey/show path (D-08).
    void ensureFresh();

    // Immutable snapshot of the current catalog. Returning by value keeps the
    // swap atomic: callers see ONE consistent version (silent swap, D-08).
    // UI thread-safe (cheap copy of implicitly-shared QVector under lock).
    QVector<AppEntry> entries() const;

signals:
    void refreshed();                 // emitted after a swap completes (03-05 wires → ResultsModel::setEntries)
    void buildFailed(int errorCount); // scan error padding (log-level; UI ignores)

private:
    // Worker payload: scanner batch result handed from the pool thread to the
    // UI thread via the future (dedupe/sort/swap happen on the UI thread).
    struct ScanResult {
        QVector<AppEntry> entries;
        int errorCount = 0;
    };

    void buildAsync();      // single-flight gate + worker launch (UI thread)
    void onBuildFinished(); // watcher completion → dedupe/sort/swap (UI thread)

    std::vector<Scanner> m_scanners;   // UI thread only (set before start)
    CurationSource m_curationSource;   // UI thread only (read in onBuildFinished)
    QFutureWatcher<ScanResult> m_watcher;
    mutable QMutex m_snapshotMutex;    // mutable: entries() is const but takes a read lock
    QVector<AppEntry> m_snapshot;                 // guarded by m_snapshotMutex (swap = atomic step)
    std::chrono::steady_clock::time_point m_lastBuilt{}; // UI thread only
    std::chrono::milliseconds m_interval;               // UI thread only; D-08 default 10 min
    bool m_buildInFlight = false;                       // UI thread only
};