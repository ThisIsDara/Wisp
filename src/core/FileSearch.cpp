#include "core/FileSearch.h"

#include <QtConcurrent>

#include "core/FuzzyMatcher.h"

// FileSearch coordinator (D-12..D-18): the typing-feel heart of the phase.
// Threading (see the header contract): the debounce timer, m_generation and
// m_state are UI-thread-only; the QtConcurrent worker owns the COM apartment
// and runs the status probe + query + tracked-source match (AppCatalog
// discipline, PATTERNS §2). The seams are set before any dispatch and never
// mutated — the worker captures copies, never touches UI-thread state.

namespace {
// Map the worker's FileSearchState ordinal to the enum (StatusFn contract:
// returns the ordinal — main.cpp wires checkIndexStatus's IndexerState whose
// ordinal order mirrors FileSearchState; tests pass ordinals directly).
FileSearch::FileSearchState stateFromOrdinal(int ordinal)
{
    switch (ordinal) {
    case FileSearch::FileSearchState::Disabled:
        return FileSearch::FileSearchState::Disabled;
    case FileSearch::FileSearchState::Building:
        return FileSearch::FileSearchState::Building;
    case FileSearch::FileSearchState::Unavailable:
        return FileSearch::FileSearchState::Unavailable;
    case FileSearch::FileSearchState::Ok:
    default:
        return FileSearch::FileSearchState::Ok;
    }
}
} // namespace

FileSearch::FileSearch(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDebounceMs); // D-12 locked at 150ms
    connect(&m_debounce, &QTimer::timeout, this, [this] { dispatch(); });
    connect(&m_watcher, &QFutureWatcher<WorkerResult>::finished,
            this, &FileSearch::onFinished);
}

void FileSearch::setQueryFn(QueryFn fn)
{
    m_queryFn = std::move(fn);
}

void FileSearch::setStatusFn(StatusFn fn)
{
    m_statusFn = std::move(fn);
}

void FileSearch::setTrackedSource(TrackedSource fn)
{
    m_trackedSource = std::move(fn);
}

void FileSearch::setAddedSource(AddedSource fn)
{
    m_addedSource = std::move(fn);
}

void FileSearch::setAddExeDialog(AddExeDialog fn)
{
    m_addExeDialog = std::move(fn);
}

void FileSearch::setAddEntryStore(AddEntryStore fn)
{
    m_addEntryStore = std::move(fn);
}

void FileSearch::setPool(QThreadPool *pool)
{
    // WR-05 test seam — must be set before dispatch (workers are queued onto
    // whichever pool is current at dispatch time). Never owned by FileSearch.
    m_pool = pool;
}

void FileSearch::setQuery(const QString &query)
{
    m_query = query;
    if (query.isEmpty()) {
        // D-14 (default list): immediate added-only snapshot — the curated
        // catalog is rendered by the app pipeline; manual picks (CUR-04
        // escape hatch) flow here. NO index query and NO debounce: this is
        // the cheap QSettings read, and a manual pick must join the default
        // list the instant the dialog closes.
        m_debounce.stop();
        dispatch();
        return;
    }
    // Single-shot restart — the LAST text wins when typing pauses (D-12).
    m_debounce.start();
}

void FileSearch::addExecutable()
{
    if (!m_addExeDialog)
        return; // seam not wired — no-op
    const QString p = m_addExeDialog();
    if (p.isEmpty())
        return; // D-11: cancelled dialog → nothing

    if (m_addEntryStore)
        m_addEntryStore(p);
    // D-11: immediate re-dispatch (fresh generation) — the new exe appears
    // right away, no 150ms debounce wait. Empty query → the default-list
    // snapshot; live query → the merged results. Either way: instant.
    dispatch();
    emit addExecutableDone(p);
}

void FileSearch::dispatch()
{
    const quint64 gen = ++m_generation; // UI thread only
    const QString q = m_query;          // UI-thread snapshot (D-12 last text wins)

    // Copies for the worker — the lambda never touches UI-thread state.
    const StatusFn statusFn = m_statusFn;
    const QueryFn queryFn = m_queryFn;
    const TrackedSource tracked = m_trackedSource;
    const AddedSource added = m_addedSource;

    const auto worker = [gen, q, statusFn, queryFn, tracked, added]() -> WorkerResult {
        try {
            const FileSearchState st =
                statusFn ? stateFromOrdinal(statusFn()) : FileSearchState::Ok;

            if (q.isEmpty()) {
                // D-14 (default list): added-only snapshot. The curated
                // catalog renders via the app pipeline; manual picks (CUR-04
                // escape hatch) flow here. No index query, no fuzzy pass
                // (every added entry shows), and no Disabled/Unavailable
                // short-circuit — the default list must survive an index
                // outage. Status still rides along (st) so the UI's
                // stateChanged stays truthful when the query clears.
                QVector<AppEntry> out;
                if (added)
                    out = added();
                return WorkerResult{ gen, q, out, st };
            }

            // D-16/D-17: Disabled/Unavailable skip the query — status only.
            if (st == FileSearchState::Disabled || st == FileSearchState::Unavailable)
                return WorkerResult{ gen, q, {}, st };

            QueryResult r;
            if (queryFn)
                r = queryFn(q);
            // RESEARCH §2: query failed with Ok status → Unavailable.
            if (r.failed)
                return WorkerResult{ gen, q, {}, FileSearchState::Unavailable };

            // Building stays Building — the query still runs (D-17).
            // D-06/D-07 second source: tracked/added .exe matched on name AND path.
            QVector<AppEntry> out = r.entries;
            if (tracked) {
                const QVector<AppEntry> trackedEntries = tracked();
                for (const AppEntry &e : trackedEntries) {
                    if (FuzzyMatcher::score(q, e.displayName).score > 0
                        || FuzzyMatcher::score(q, e.targetPath).score > 0)
                        out.append(e);
                }
            }
            return WorkerResult{ gen, q, out, st };
        } catch (...) {
            // AppCatalog discipline: nothing may escape the worker — QtConcurrent
            // would rethrow at result() on the UI thread → crash. A throwing seam
            // degrades to Unavailable (RESEARCH §2 query-failure semantics).
            return WorkerResult{ gen, q, {}, FileSearchState::Unavailable };
        }
    };

    m_watcher.setFuture(QtConcurrent::run(
        m_pool ? m_pool : QThreadPool::globalInstance(), worker));
}

void FileSearch::onFinished()
{
    const WorkerResult r = m_watcher.result();
    if (r.generation != m_generation)
        return; // D-15 stale-drop — defense-in-depth layer ONE (04-04's
                // ResultsModel::setFileResults drops again on its side)
    // WR-03 layer ONE: the generation check proves recency, not relevance — a
    // result computed for OLD query text (its dispatch raced a newer setQuery
    // that is still inside the debounce window) must not reach the model
    // while the CURRENT query reads differently.
    if (r.query != m_query)
        return;

    if (r.state != m_state) {
        m_state = r.state;
        emit stateChanged(); // statusText / indexerOk follow
    }
    emit resultsReady(r.generation, r.query, r.files);
}

QString FileSearch::statusText() const
{
    // D-17: the SINGLE home of the three locked trouble-state copies (RESEARCH
    // §9). QML renders this string verbatim (04-05) — it never invents its own.
    switch (m_state) {
    case FileSearchState::Disabled:
        return QStringLiteral("Indexing is turned off — enable Windows Search to find files");
    case FileSearchState::Building:
        return QStringLiteral("Windows is still building its search index — files will appear soon");
    case FileSearchState::Unavailable:
        return QStringLiteral("File search is unavailable right now");
    case FileSearchState::Ok:
        break;
    }
    return QString();
}

bool FileSearch::indexerOk() const
{
    return m_state == FileSearchState::Ok;
}
