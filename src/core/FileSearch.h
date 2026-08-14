#pragma once
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>
#include <functional>
#include "core/AppEntry.h"

class QThreadPool;

// File-search coordinator (D-12..D-18): debounced, generation-countered,
// worker-thread file queries feeding ResultsModel::setFileResults (04-04).
// Firewall-clean: no Win32/COM headers — all index/scan detail arrives via
// the injectable std::function seams (PATTERNS §2; AppCatalog analog §2).
// Threading: worker (QtConcurrent pool) owns the query + status probe
// (AppCatalog discipline); the UI-thread watcher-completion drops stale
// generations and emits. The debounce timer is UI-thread-only (D-12).
class FileSearch : public QObject
{
    Q_OBJECT
    // D-18 QML contract: statusText renders the status row (verbatim locked
    // copy — RESEARCH §9), indexerOk gates its visibility (paired with
    // resultsModel.query !== "" in QML).
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(bool indexerOk READ indexerOk NOTIFY stateChanged)
    // 07-06: last-scan summary (ScanService::lastScanSummary proxy) — read
    // live on the UI thread; NOTIFY rides stateChanged (a scan completing
    // transitions Scanning→Idle and refreshes the summary).
    Q_PROPERTY(QString lastScanSummary READ lastScanSummary NOTIFY stateChanged)

public:
    // D-17 locked states; ordinal order mirrors ScanService::ScanState
    // (07-03) — main.cpp maps explicitly, never casts blindly.
    // Idle = index ready; NoRoots = no scan locations configured; Scanning =
    // first/ongoing walk; Error = scan failed (some locations unreadable).
    enum FileSearchState { Idle = 0, NoRoots, Scanning, Error };

    explicit FileSearch(QObject *parent = nullptr);

    // ── Injectable seams (defaults = no-op; main.cpp wires real, tests fake) ──
    struct QueryResult { QVector<AppEntry> entries; bool failed = false; }; // failed → Error (RESEARCH §2)
    using QueryFn = std::function<QueryResult(const QString &query)>;       // index rows (D-01) — wired to FileIndex::queryCandidates
    using StatusFn = std::function<int()>;                                  // FileSearchState ordinal — wired to ScanService::stateOrdinal
    using TrackedSource = std::function<QVector<AppEntry>()>;               // D-06/D-10 — wired to LaunchHistory::trackedExecutables
    using AddedSource = std::function<QVector<AppEntry>()>;                 // D-11 — wired to LaunchHistory::addedExecutables (default-list escape hatch)
    using AddExeDialog = std::function<QString()>;                          // D-11 native dialog; "" = cancelled
    using AddEntryStore = std::function<void(const QString &path)>;         // D-11 persist — wired to LaunchHistory::addExecutable
    using SummaryFn = std::function<QString()>;                             // 07-06 — wired to ScanService::lastScanSummary

    void setQueryFn(QueryFn fn);
    void setStatusFn(StatusFn fn);
    void setTrackedSource(TrackedSource fn);
    void setAddedSource(AddedSource fn);
    void setAddExeDialog(AddExeDialog fn);
    void setAddEntryStore(AddEntryStore fn);
    void setSummaryFn(SummaryFn fn);
    // WR-05 test seam: run workers on a DEDICATED pool (controlled thread
    // count, no contention from other suites) instead of the shared global
    // QtConcurrent pool. nullptr (default) → global pool (production).
    void setPool(QThreadPool *pool);

    // D-14: empty → immediate added-only snapshot (no debounce, no index
    // query — the default list is catalog apps + manual picks, and a manual
    // pick must join it instantly). Non-empty → restart the 150ms debounce;
    // the LAST text wins when typing pauses.
    Q_INVOKABLE void setQuery(const QString &query);
    // D-11: pinned "Add executable…" row → dialog → store → immediate
    // re-dispatch (fresh generation) so the new exe appears right away.
    Q_INVOKABLE void addExecutable();

    QString statusText() const; // D-17 locked copy (RESEARCH §9), "" when Idle
    bool indexerOk() const;     // state() == Idle (D-18 row gate)
    QString lastScanSummary() const; // 07-06 live UI-thread read; "" = never scanned

signals:
    // D-15: results carry the generation; ResultsModel::setFileResults (04-04)
    // ALSO drops stale generations — defense in depth, both sides check.
    // WR-03: the query TEXT the result was computed for travels too — the
    // generation proves recency, the text proves relevance: a result for old
    // text (delivered inside the debounce window, same generation) must never
    // merge under the current query.
    void resultsReady(quint64 generation, const QString &query,
                      const QVector<AppEntry> &files);
    void stateChanged();            // statusText/indexerOk follow
    void addExecutableDone(const QString &path);

private:
    void dispatch();   // generation++ → worker (status + query + tracked match)
    void onFinished(); // UI thread: stale-drop, state update, emit

    struct WorkerResult {
        quint64 generation = 0;
        QString query;               // the query text this result was computed for (WR-03)
        QVector<AppEntry> files;
        FileSearchState state = Idle;
    };

    QueryFn m_queryFn;
    StatusFn m_statusFn;
    TrackedSource m_trackedSource;
    AddedSource m_addedSource;
    AddExeDialog m_addExeDialog;
    AddEntryStore m_addEntryStore;
    SummaryFn m_summaryFn;
    QString m_query;                 // UI thread only — re-dispatch target for addExecutable
    QTimer m_debounce;               // single-shot, 150ms, UI thread only
    QFutureWatcher<WorkerResult> m_watcher;
    QThreadPool *m_pool = nullptr;   // WR-05: nullptr → global pool (set in tests)
    quint64 m_generation = 0;        // UI thread only — ++ per dispatch
    FileSearchState m_state = Idle;  // UI thread only
    static constexpr int kDebounceMs = 150; // D-12 roadmap range 120-150ms (locked at 150)
};
