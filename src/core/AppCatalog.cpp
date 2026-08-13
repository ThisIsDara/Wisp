#include "core/AppCatalog.h"

#include "core/CurationRules.h"

#include <QtConcurrent>

#include <QDebug>
#include <QHash>

#include <algorithm>
#include <exception>

#include <objbase.h> // CoInitializeEx / CoUninitialize — worker apartment only (PITFALLS #3, RESEARCH §2)

namespace {

// D-10 / T-03-03-04: exact case-insensitive full-name equality only — a UWP
// entry is suppressed when a .lnk entry (processed first) already holds its
// toCaseFolded() name. .lnk-vs-.lnk and Uwp-vs-Uwp duplicates are NOT deduped
// (no rule covers them; the per-user/all-users Start Menu folders are
// distinct sources). No fuzzy matching, ever.
QVector<AppEntry> dedupeLnkOverUwp(QVector<AppEntry> raw)
{
    QVector<AppEntry> out;
    out.reserve(raw.size());
    QHash<QString, int> taken; // folded displayName → index in out

    for (const AppEntry &e : raw) {
        if (e.source == AppEntry::Source::Lnk) {
            taken.insert(e.displayName.toCaseFolded(), out.size());
            out.push_back(e);
        }
    }
    for (const AppEntry &e : raw) {
        if (e.source == AppEntry::Source::Uwp
            && !taken.contains(e.displayName.toCaseFolded())) {
            taken.insert(e.displayName.toCaseFolded(), out.size());
            out.push_back(e);
        }
    }
    // 05.1 CUR-04: File rows pass through untouched — D-10 collision logic is
    // Lnk-over-UWP only, and markCurated's File guard (below) must stay
    // reachable. Dropping them here would silently delete file entries from
    // the catalog.
    for (const AppEntry &e : raw) {
        if (e.source == AppEntry::Source::File)
            out.push_back(e);
    }
    return out;
}

// D-03: canonical display order — the exact comparator ResultsModel::setEntries
// uses, so the catalog → model pipeline sorts consistently at both layers.
void sortAlphabetical(QVector<AppEntry> &entries)
{
    std::sort(entries.begin(), entries.end(), [](const AppEntry &a, const AppEntry &b) {
        return a.displayName.toCaseFolded() < b.displayName.toCaseFolded();
    });
}

// 05.1 marking (CUR-01/CUR-02/CUR-04): NEVER removes entries — hidden
// stays in the snapshot so Show-hidden/Unhide work without a rebuild
// (pattern 1). Runs strictly AFTER dedupeLnkOverUwp: filtering before
// dedupe would let a hidden .lnk's suppressed UWP twin resurrect
// (research Pitfall 1). Identity: Lnk → targetPath, UWP → aumid.
// shownIds override wins over hiddenIds AND default rules.
// Curation is UX visibility, NOT access control (Pitfall 5): hidden rows
// remain launchable via other paths by design.
void markCurated(QVector<AppEntry> &entries, const AppCatalog::CurationData &data)
{
    for (AppEntry &e : entries) {
        if (e.source == AppEntry::Source::File)
            continue;   // CUR-04: added executables are never curated
        const QString id = e.targetPath.isEmpty() ? e.aumid : e.targetPath;
        if (data.shownIds.contains(id))
            continue;   // explicit user override beats rules
        e.hidden = data.hiddenIds.contains(id) || CurationRules::matches(e);
    }
}

} // namespace

AppCatalog::AppCatalog(QObject *parent)
    : QObject(parent)
    , m_interval(std::chrono::minutes(10)) // D-08 default refresh age
{
    // finished() is delivered to the UI thread (watcher lives here); the
    // swap therefore always runs on the UI thread, never on a pool thread.
    connect(&m_watcher, &QFutureWatcher<ScanResult>::finished,
            this, &AppCatalog::onBuildFinished);
}

void AppCatalog::setScanners(std::vector<Scanner> scanners)
{
    m_scanners = std::move(scanners);
}

void AppCatalog::setCurationSource(CurationSource source)
{
    m_curationSource = std::move(source);
}

void AppCatalog::setRefreshInterval(std::chrono::milliseconds interval)
{
    m_interval = interval;
}

void AppCatalog::start()
{
    buildAsync();
}

void AppCatalog::ensureFresh()
{
    if (m_buildInFlight)
        return; // single-flight: a build is already on its way (T-03-03-02)
    if (m_lastBuilt.time_since_epoch() == std::chrono::steady_clock::duration::zero()) {
        buildAsync(); // never built yet — the age check has nothing to compare
        return;
    }
    const auto age = std::chrono::steady_clock::now() - m_lastBuilt;
    if (age >= m_interval)
        buildAsync(); // D-08: rebuild only when older than the interval
}

QVector<AppEntry> AppCatalog::entries() const
{
    QMutexLocker locker(&m_snapshotMutex);
    return m_snapshot; // implicit-shared copy — O(1); one consistent version (D-08)
}

void AppCatalog::buildAsync()
{
    if (m_buildInFlight)
        return; // T-03-03-02: overlapping start()/ensureFresh() coalesce
    m_buildInFlight = true;

    const std::vector<Scanner> scanners = m_scanners; // worker owns a copy
    const auto worker = [scanners]() -> ScanResult {
        ScanResult result;

        // PITFALLS #3 / RESEARCH §2: this worker thread owns its COM
        // apartment for the batch. CoInitializeEx(COINIT_MULTITHREADED) is
        // the same underlying call winrt::init_apartment(multi_threaded)
        // makes — both WinRT scanners require an initialized apartment.
        // Reuse discipline copied from WinStartMenuEnumerator: S_FALSE
        // (already initialized) and RPC_E_CHANGED_MODE (different mode) are
        // both fine — only an apartment WE initialized gets CoUninitialize.
        const HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
            qWarning() << "AppCatalog: CoInitializeEx failed" << initHr;
            return result;
        }
        const bool weInitialized = (initHr == S_OK);

        try {
            for (const Scanner &scanner : scanners) {
                try {
                    result.entries += scanner();
                } catch (const std::exception &e) {
                    // T-03-03-03: one bad scanner never aborts the batch and
                    // never reaches the UI thread — it is counted and skipped.
                    ++result.errorCount;
                    qWarning() << "AppCatalog: scanner threw:" << e.what();
                } catch (...) {
                    ++result.errorCount;
                    qWarning() << "AppCatalog: scanner threw (non-std exception)";
                }
            }
        } catch (...) {
            // Belt-and-braces: nothing may escape the worker (QtConcurrent
            // would rethrow it on the UI thread at result() → crash).
            ++result.errorCount;
            qWarning() << "AppCatalog: unexpected worker failure";
        }

        if (weInitialized)
            CoUninitialize();
        return result;
    };

    m_watcher.setFuture(QtConcurrent::run(std::move(worker)));
}

void AppCatalog::onBuildFinished()
{
    m_buildInFlight = false;
    m_lastBuilt = std::chrono::steady_clock::now();

    const ScanResult result = m_watcher.result(); // worker never throws (all caught)

    // Policy pipeline: dedupe (D-10) → curation mark (05.1) → sort (D-03) → swap (D-08 silent).
    QVector<AppEntry> next = dedupeLnkOverUwp(result.entries);
    markCurated(next, m_curationSource ? m_curationSource() : AppCatalog::CurationData{});
    sortAlphabetical(next);
    {
        QMutexLocker locker(&m_snapshotMutex);
        m_snapshot = std::move(next); // atomic swap: readers see the new version whole
    }

    if (result.errorCount > 0)
        emit buildFailed(result.errorCount); // log-level padding; UI ignores
    emit refreshed(); // exactly one per swap (T-03-03-02)
}