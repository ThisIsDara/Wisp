#pragma once

#include <QDateTime>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>

#include <functional>

#include "core/FileIndex.h"
#include "win/WinDirectoryWalk.h"

// Scan orchestration (07-03): interval timer + single-flight worker dispatch
// + UI-thread snapshot discipline + state publishing for the self-managed
// file scan. The worker walks via FileIndex::walkAndDelta (pure, read-locked);
// the UI-thread completion applies + persists. All seams are injectable —
// main.cpp (07-04) and SettingsWindow (07-05) consume exactly what is here,
// and tests inject fakes.
//
// Threading contract (Pitfall 4, T-07-04): SettingsStore is UI-thread-only
// (no mutex), so roots/interval are read through ONE SettingsSource lambda
// invoked on the UI thread at dispatch; the worker receives a by-value copy
// and can never reach into the store. The worker runs on a DEDICATED pool
// (production: a 1-thread pool owned by main.cpp — never the global pool) and
// drops its own priority to low.
//
// State machine (ordinals feed FileSearch via an EXPLICIT map in main.cpp —
// never a blind cast): Idle (roots exist, last scan ok / never scanned),
// NoRoots (no configured roots — Settings UI shows the picker hint), Scanning
// (walk in flight), Error (a listing failed — summary carries the count).
//
// No scan at boot (D-09): start() only arms the timer; the persisted index
// (loaded by main.cpp before start) is what makes relaunch instant. The first
// scan happens on root addition (SettingsWindow → requestScan) or the first
// interval tick.
class ScanService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastScanSummary READ lastScanSummary NOTIFY scanStateChanged)

public:
    enum ScanState { Idle = 0, NoRoots, Scanning, Error };

    struct ScanSettings {
        QStringList roots;
        int intervalMinutes = 10; // clamped to [1, 1440] at every consumption site
    };

    using ListFn = std::function<WinDirectoryWalk::WinDirListing(const QString &path)>;
    using SettingsSource = std::function<ScanSettings()>;

    explicit ScanService(QObject *parent = nullptr);

    void setListFn(ListFn fn);
    void setSettingsSource(SettingsSource fn);
    void setIndex(FileIndex *index); // external ownership — must outlive the service
    void setPool(QThreadPool *pool); // dedicated pool; nullptr → global (tests inject)

    void start();           // boot: read snapshot; arm timer iff roots exist; NO scan (D-09)
    void requestScan();     // "Scan now" / roots-changed funnel — single-flight + coalesce
    void refreshInterval(); // re-arm the timer from a fresh snapshot (Settings interval selector)

    int stateOrdinal() const;
    QString lastScanSummary() const; // "" when never scanned

signals:
    void scanStateChanged(); // emitted only when state or summary actually changed

private:
    void dispatch();
    void onScanFinished();

    ListFn m_listFn;
    SettingsSource m_settingsSource;
    FileIndex *m_index = nullptr;
    QThreadPool *m_pool = nullptr;
    QTimer m_timer;
    QFutureWatcher<FileIndex::WalkOutcome> m_watcher;
    bool m_scanInFlight = false;   // single-flight gate (AppCatalog discipline)
    bool m_rescanPending = false;  // coalesced follow-up — never a queue
    ScanState m_state = Idle;
    QDateTime m_lastScanTime; // invalid → never scanned → summary ""
    int m_entryCount = 0;
    int m_failedRoots = 0;
};