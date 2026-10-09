#include "core/ScanService.h"

#include <QDateTime>
#include <QLocale>
#include <QThread>
#include <QtConcurrent>

#include <algorithm>

namespace {

// OQ4 clamp: the effective interval is 1..1440 minutes. Applied at EVERY
// consumption site (start, onScanFinished re-arm, refreshInterval) so a
// corrupt/foreign settings value can never arm a broken timer.
int clampedInterval(int intervalMinutes)
{
    return qBound(1, intervalMinutes, 1440);
}

} // namespace

ScanService::ScanService(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(clampedInterval(ScanSettings{}.intervalMinutes) * 60000);
    connect(&m_timer, &QTimer::timeout, this, [this] { requestScan(); });
    connect(&m_watcher, &QFutureWatcher<FileIndex::WalkOutcome>::finished,
            this, &ScanService::onScanFinished);
}

void ScanService::setListFn(ListFn fn)
{
    m_listFn = std::move(fn);
}

void ScanService::setSettingsSource(SettingsSource fn)
{
    m_settingsSource = std::move(fn);
}

void ScanService::setIndex(FileIndex *index)
{
    m_index = index;
}

void ScanService::setPool(QThreadPool *pool)
{
    m_pool = pool;
}

void ScanService::start(bool indexLoadOk)
{
    // Boot: snapshot + arm only. NO scan (D-09) - relaunch is instant from
    // the persisted index main.cpp loaded before calling start().
    const ScanSettings s = m_settingsSource ? m_settingsSource() : ScanSettings{};
    if (s.roots.isEmpty()) {
        m_timer.stop();
        if (m_state != NoRoots) {
            m_state = NoRoots;
            emit scanStateChanged();
        }
        return;
    }
    m_timer.setInterval(clampedInterval(s.intervalMinutes) * 60000);
    m_timer.start();
    if (m_state != Idle) {
        m_state = Idle;
        emit scanStateChanged();
    }

    // 2026-10-09 (upgrade showed an empty list): D-09's "relaunch is instant
    // from the persisted index" stops holding when that index was REJECTED —
    // an index-format bump on update makes the old file unreadable, so the
    // first launch after an update had an empty index AND no scan, and the
    // list stayed blank until the interval tick (up to scanIntervalMinutes) or
    // a manual "Scan now". Recover with exactly one scan, here, right now.
    //
    // "Exactly once" needs no bookkeeping and no persisted flag: this scan
    // writes a CURRENT-format index, so the very next launch loads it (ok=1)
    // and takes the no-scan path above. It is also gated on roots, so a genuine
    // first run (no index file yet, no folders picked) still doesn't scan —
    // that case belongs to the D-09 first-root flow. And because it goes
    // through requestScan(), the single-flight gate means a timer tick or a
    // manual "Scan now" landing in the same window coalesces into THIS scan
    // instead of doubling the work.
    if (!indexLoadOk)
        requestScan();
}

void ScanService::requestScan()
{
    if (m_scanInFlight) {
        m_rescanPending = true; // coalesce into exactly ONE follow-up (never a queue)
        return;
    }
    if (m_index == nullptr)
        return; // seam not wired (main.cpp wires it before start())
    dispatch();
}

void ScanService::dispatch()
{
    m_scanInFlight = true;
    m_rescanPending = false;

    // UI-thread snapshot — the worker NEVER touches the store (Pitfall 4).
    const ScanSettings s = m_settingsSource ? m_settingsSource() : ScanSettings{};

    if (m_state != Scanning) {
        m_state = Scanning;
        emit scanStateChanged();
    }

    const ListFn listFn = m_listFn;
    FileIndex *const index = m_index;

    const auto worker = [s, listFn, index]() -> FileIndex::WalkOutcome {
        QThread::currentThread()->setPriority(QThread::LowPriority); // D-08: never starve typing
        try {
            return index->walkAndDelta(s.roots, listFn); // const, read-locked
        } catch (...) {
            // AppCatalog discipline: nothing may escape the worker — QtConcurrent
            // would rethrow at result() on the UI thread. Degrade to Error.
            FileIndex::WalkOutcome o;
            o.failedListings = 1;
            return o;
        }
    };

    m_watcher.setFuture(QtConcurrent::run(
        m_pool ? m_pool : QThreadPool::globalInstance(), worker));
}

void ScanService::onScanFinished()
{
    m_scanInFlight = false;

    FileIndex::WalkOutcome o;
    try {
        o = m_watcher.result(); // the worker caught everything — belt-and-braces anyway
    } catch (...) {
        o.failedListings = 1; // degrade to Error, never a crash
    }

    if (m_index) {
        m_index->apply(o);
        m_index->save(); // QSaveFile atomic commit; ~10ms — UI thread is fine
    }

    // State: roots-empty → NoRoots (empty-roots walk empties the index);
    // any failed listing → Error (count rides in the summary); else Idle.
    const ScanSettings s = m_settingsSource ? m_settingsSource() : ScanSettings{};
    const ScanState next = s.roots.isEmpty() ? NoRoots
                           : (o.failedListings > 0 ? Error : Idle);
    const bool stateChanged = (next != m_state);
    const QString oldSummary = lastScanSummary();
    m_lastScanTime = QDateTime::currentDateTime();
    m_entryCount = m_index ? m_index->entryCount() : 0;
    m_failedRoots = o.failedListings;
    m_state = next;
    if (stateChanged || lastScanSummary() != oldSummary)
        emit scanStateChanged(); // no spurious NOTIFY (FileSearch.cpp:193-196 precedent)

    // Re-arm from a FRESH snapshot (roots may have changed while scanning);
    // the timer runs only when roots exist.
    if (s.roots.isEmpty()) {
        m_timer.stop();
    } else {
        m_timer.setInterval(clampedInterval(s.intervalMinutes) * 60000);
        m_timer.start();
    }

    if (m_rescanPending)
        dispatch(); // the coalesced follow-up — keeps single-flight
}

void ScanService::refreshInterval()
{
    // Re-arm only — roots are handled by the scan path. Called by the
    // Settings interval selector on every change.
    const ScanSettings s = m_settingsSource ? m_settingsSource() : ScanSettings{};
    if (s.roots.isEmpty()) {
        m_timer.stop();
        return;
    }
    m_timer.setInterval(clampedInterval(s.intervalMinutes) * 60000);
    m_timer.start();
}

int ScanService::stateOrdinal() const
{
    return int(m_state); // main.cpp maps EXPLICITLY to FileSearchState — never a blind cast
}

QString ScanService::lastScanSummary() const
{
    if (!m_lastScanTime.isValid())
        return QString(); // never scanned — Settings QML shows "Not scanned yet"
    QString s = QStringLiteral("Last scan %1 — %2 entries")
                    .arg(m_lastScanTime.toString(QStringLiteral("HH:mm")),
                         QLocale().toString(m_entryCount));
    if (m_failedRoots > 0)
        s += QStringLiteral(" · %1 location failed").arg(m_failedRoots);
    return s;
}
