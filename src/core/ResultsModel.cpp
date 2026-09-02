#include "core/ResultsModel.h"
#include "core/Calculator.h"

#include <QClipboard>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QSet>
#include <QtConcurrent>

namespace {

// 2026-08-15: favorites identity — the same contract as curation (targetPath
// wins; UWP falls back to aumid). Empty when neither is present.
QString idOf(const AppEntry &e)
{
    return e.targetPath.isEmpty() ? e.aumid : e.targetPath;
}

// Fast scoring using precomputed lower + boundaries (hot path — avoids
// per-entry toLower/toCaseFolded and isBoundary recompute).
inline FuzzyMatcher::Result scoreFast(const QString &queryLower,
                                      const QString &targetLower,
                                      const QVector<char> &boundaries)
{
    FuzzyMatcher::Result none;
    const int qLen = queryLower.size();
    const int nLen = targetLower.size();
    if (qLen == 0 || qLen > nLen)
        return none;
    // Prefix check (exact/prefix tier)
    bool starts = true;
    for (int i = 0; i < qLen; ++i) {
        if (queryLower.at(i) != targetLower.at(i)) { starts = false; break; }
    }
    if (starts) {
        int b = 0;
        for (int i = 0; i < qLen; ++i) if (boundaries.at(i)) ++b;
        FuzzyMatcher::Result r;
        const int tier = (qLen == nLen ? 1000 : 800);
        int bonus = qLen * 1 + b * 2 + 1;
        if (bonus > 199) bonus = 199;
        r.score = tier + bonus;
        r.ranges = {{0, qLen}};
        return r;
    }
    // General subsequence — first char prefers boundary
    int first = -1;
    for (int j = 0; j < nLen; ++j) {
        if (queryLower.at(0) == targetLower.at(j) && boundaries.at(j)) { first = j; break; }
    }
    if (first < 0) {
        for (int j = 0; j < nLen; ++j) if (queryLower.at(0) == targetLower.at(j)) { first = j; break; }
    }
    if (first < 0) return none;
    QVector<int> pos; pos.reserve(qLen); pos.append(first);
    int prev = first;
    for (int i = 1; i < qLen; ++i) {
        int j = prev + 1;
        while (j < nLen && queryLower.at(i) != targetLower.at(j)) ++j;
        if (j >= nLen) return none;
        pos.append(j); prev = j;
    }
    int boundariesHit = 0;
    for (int p : pos) if (boundaries.at(p)) ++boundariesHit;
    // Merge runs
    FuzzyMatcher::Result r;
    int runStart = pos.first(), runEnd = runStart, runs = 1;
    for (int i = 1; i < pos.size(); ++i) {
        if (pos.at(i) == runEnd + 1) runEnd = pos.at(i);
        else { r.ranges.append({runStart, runEnd - runStart + 1}); runStart = pos.at(i); runEnd = runStart; ++runs; }
    }
    r.ranges.append({runStart, runEnd - runStart + 1});
    const int tier = boundaries.at(pos.first()) ? 600 : 400;
    int bonus = int(pos.size()) * 1 + boundariesHit * 2 + (runs == 1 ? 1 : 0);
    if (bonus > 199) bonus = 199;
    r.score = tier + bonus;
    return r;
}

} // namespace

ResultsModel::ResultsModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_watcher, &QFutureWatcher<AppResult>::finished, this, [this] {
        const AppResult r = m_watcher.result();
        if (r.gen != m_appGen) return;
        if (r.query != m_query) return;
        applyAppResult(r);
    });
    connect(&m_providerWatcher, &QFutureWatcher<ProviderResult>::finished, this, [this] {
        const ProviderResult r = m_providerWatcher.result();
        if (r.gen != m_appGen) return;
        if (r.query != m_query) return;
        applyProviderResult(r);
    });
}

void ResultsModel::applyAppResult(const AppResult &r)
{
    const bool oldHasCalc = m_hasCalc;
    const QString oldCalc = m_hasCalc ? m_calcEntry.targetPath : QString();
    const int oldSelected = m_selected;
    beginResetModel();
    m_order = r.order;
    m_ranges = r.ranges;
    m_calcEntry = r.calcEntry;
    m_hasCalc = r.hasCalc;
    // Merge file rows that arrived while worker ran (in m_fileEntries)
    if (!m_query.isEmpty() && !m_fileEntries.isEmpty())
        mergeFiles();
    else if (m_hasCalc != oldHasCalc || (m_hasCalc && m_calcEntry.targetPath != oldCalc))
        emit calculatorResultChanged();
    m_selected = 0;
    endResetModel();
    if (oldSelected != 0) emit selectionChanged();
    if (m_hasCalc != oldHasCalc || (m_hasCalc && m_calcEntry.targetPath != oldCalc))
        emit calculatorResultChanged();
}

void ResultsModel::setPool(QThreadPool *pool) { m_pool = pool; }

void ResultsModel::applyProviderResult(const ProviderResult &r)
{
    const bool oldHasCalc = m_hasCalc;
    const QString oldCalc = m_hasCalc ? m_calcEntry.targetPath : QString();
    const int oldSelected = m_selected;
    // Phase-11 cmd/ preview + calculator text: the displayed NAME can change
    // while the row identity is unchanged (provider slot refills as the query
    // grows). Snapshot the CURRENT on-screen names BEFORE m_providerRows is
    // overwritten below, so the no-op guard can tell "same name" from a stale
    // title (entryAt resolves against the (soon-to-be-new) member rows).
    QVector<QString> oldNames;
    oldNames.reserve(m_order.size());
    for (int i = 0; i < m_order.size(); ++i)
        oldNames.append(entryAt(m_order.at(i)).displayName);
    m_providerRows = r.rows;
    // Build the display order first, then decide whether a reset is needed.
    // Settled queries re-deliver IDENTICAL rows (debounce re-fire, backspace
    // restoring a previous list) — an unconditional beginResetModel would
    // rebuild every delegate for no visible change (the "steam" jank).
    QVector<Row> newOrder;
    QVector<FuzzyMatcher::Result> newRanges;
    AppEntry newCalc;
    bool newHasCalc = false;
    newOrder.reserve(r.rows.size());
    newRanges.reserve(r.rows.size());
    for (int i = 0; i < r.rows.size(); ++i) {
        const ScoredEntry &se = r.rows.at(i);
        const bool isCalc = se.entry.source == AppEntry::Source::Calculator;
        // CUR-03: hidden rows render only in show-hidden mode. The favorites
        // override (favorited-but-hidden row stays, 2026-08-17) applies too.
        if (se.entry.hidden && !m_showHidden
            && !(m_favoritesOnly && m_favoriteIds.contains(idOf(se.entry))))
            continue;
        // 2026-08-15: Favorites tab prunes to favorite rows; the calculator
        // row is always shown (it was always treated as favorite). Phase-11:
        // an explicit Command row answers too — a typed cmd/ query must not
        // vanish inside the Favorites tab.
        if (m_favoritesOnly && !isCalc
            && se.entry.source != AppEntry::Source::Command
            && !m_favoriteIds.contains(idOf(se.entry)))
            continue;
        newOrder.append(Row{i, false, false, true, isCalc});
        newRanges.append(se.match);
        if (isCalc) {
            newCalc = se.entry;
            newHasCalc = true;
        }
    }
    // No-op guard: identical display rows → nothing to repaint. Also emit the
    // calculator signal even when rows are unchanged (a "1+1" re-query may
    // recompute the same value — the footer text needs the poke).
    bool sameRows = (newOrder.size() == m_order.size());
    if (sameRows) {
        for (int i = 0; i < newOrder.size(); ++i) {
            const Row &a = newOrder.at(i);
            const Row &b = m_order.at(i);
            if (a.entryIndex != b.entryIndex || a.fromFiles != b.fromFiles
                || a.fromAdded != b.fromAdded || a.fromProvider != b.fromProvider
                || a.isCalculator != b.isCalculator) {
                sameRows = false;
                break;
            }
            // Text may change while the row IDENTITY is unchanged — the phase-11
            // cmd/ preview (Command: "<typed>") and the calculator line both
            // re-render the SAME provider slot as the query grows. Without this
            // check the no-op guard skips the repaint and the title goes stale.
            // NOTE: a calculator row's proposed text comes from newCalc (the
            // pending result) — entryAt(a) would read the STALE m_calcEntry,
            // defeating the guard.
            const QString proposed =
                a.isCalculator ? newCalc.displayName : entryAt(a).displayName;
            if (proposed != oldNames.at(i)) {
                sameRows = false;
                break;
            }
        }
    }
    if (sameRows) {
        m_calcEntry = std::move(newCalc);
        m_hasCalc = newHasCalc;
    } else {
        // Progressive display (10-03): apply the first block with a single
        // small reset (instant visual feedback), then append the rest with
        // incremental inserts. A full beginResetModel over the whole list tears
        // down every visible delegate just to reveal rows below the fold — the
        // inserts keep the front block's delegates alive and untouched.
        constexpr int kInstantRows = 8;
        const int front = qMin<int>(newOrder.size(), kInstantRows);
        beginResetModel();
        m_order.clear();
        m_ranges.clear();
        m_order.reserve(newOrder.size());
        m_ranges.reserve(newOrder.size());
        for (int i = 0; i < front; ++i) {
            m_order.append(newOrder.at(i));
            m_ranges.append(newRanges.at(i));
        }
        m_calcEntry = std::move(newCalc);
        m_hasCalc = newHasCalc;
        m_selected = 0;
        endResetModel();
        int idx = front;
        while (idx < newOrder.size()) {
            const int chunkEnd = qMin(idx + 20, newOrder.size());
            beginInsertRows(QModelIndex(), m_order.size(),
                            m_order.size() + (chunkEnd - idx) - 1);
            for (; idx < chunkEnd; ++idx) {
                m_order.append(newOrder.at(idx));
                m_ranges.append(newRanges.at(idx));
            }
            endInsertRows();
        }
    }
    if (oldSelected != 0 && !sameRows) emit selectionChanged();
    if (m_hasCalc != oldHasCalc || (m_hasCalc && m_calcEntry.targetPath != oldCalc))
        emit calculatorResultChanged();
}

void ResultsModel::setProviders(const QVector<SearchProvider*> &providers)
{
    m_providers = providers;
}

QHash<int, QByteArray> ResultsModel::roleNames() const
{
    // The QML delegate contract (03-05): model.displayName / model.subtitle /
    // model.matchRanges / model.aumid — ResultsRow.qml consumes these names.
    // Phase-4 addition (04-04): model.isFolder — the D-04 monogram glyph for
    // folder rows.
    return {
        { DisplayNameRole, "displayName" },
        { SubtitleRole, "subtitle" },
        { MatchRangesRole, "matchRanges" },
        { AumidRole, "aumid" },
        { IsFolderRole, "isFolder" },
        // 05-04: iconKey for image://wispicons/{id} — Lnk 'path;index',
        // File 'path:path', Uwp 'uwp:PFN|appId'.
        { IconKeyRole, "iconKey" },
        // 05.1: isHidden — QML dims hidden rows in show-hidden mode (CUR-03).
        { IsHiddenRole, "isHidden" },
        // 2026-08-15: isHideable — ResultsRow's remove button renders only on
        // rows hideSelected() will actually hide (CUR-04 parity).
        { IsHideableRole, "isHideable" },
        // 2026-08-15: isFavorite — the star button's filled/orange state,
        // driven by m_favoriteIds membership (id = targetPath/aumid).
        { IsFavoriteRole, "isFavorite" },
    };
}

void ResultsModel::setEntries(QVector<AppEntry> entries)
{
    const int oldSelected = m_selected;
    beginResetModel();

    // Canonical display order: alphabetical, case-insensitive (D-01/D-03).
    std::sort(entries.begin(), entries.end(), [](const AppEntry &a, const AppEntry &b) {
        return a.displayName.toCaseFolded() < b.displayName.toCaseFolded();
    });
    m_entries = std::move(entries);
    // Precompute lowercased names + boundary flags for fast scoring (hot path)
    m_entriesLower.resize(m_entries.size());
    m_entriesBoundaries.resize(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        const QString &name = m_entries.at(i).displayName;
        const QString lower = name.toLower();
        m_entriesLower[i] = lower;
        QVector<char> b(lower.size());
        for (int j = 0; j < lower.size(); ++j) {
            bool isB = false;
            if (j == 0) isB = true;
            else {
                const QChar prev = name.at(j - 1);
                if (prev.isSpace() || prev == QLatin1Char('-') || prev == QLatin1Char('_')
                    || prev == QLatin1Char('/') || prev == QLatin1Char('.'))
                    isB = true;
                else if (prev.isLower() && name.at(j).isUpper())
                    isB = true;
            }
            b[j] = isB ? 1 : 0;
        }
        m_entriesBoundaries[i] = std::move(b);
    }

    m_order.clear();
    m_order.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        // 05.1: hidden rows render only in show-hidden mode (CUR-03). This
        // third order-building site (the default state — setQuery("") early-
        // returns) must apply the same visibility branch as buildAppOrder.
        if (m_entries.at(i).hidden && !m_showHidden)
            continue;
        m_order.append(Row{ i, false });
    }
    m_ranges = QVector<FuzzyMatcher::Result>(m_order.size()); // no-match results (aligned with m_order)
    filterFavorites(); // 2026-08-15: favorites tab — prune to favorite rows when active

    // Fresh catalog slate — a rebuilt catalog never resurrects stale file
    // rows (D-08 coherence). WR-03: only the ENTRIES are cleared — the
    // generation guard stays monotonic (a reset would admit any in-flight
    // generation from FileSearch, whose counter keeps climbing).
    // m_addedEntries is retained on purpose: manual picks (D-14) are
    // store-backed and survive catalog rebuilds (re-delivered on the next
    // empty-query dispatch anyway — clearing would only flash them out).
    m_fileEntries.clear();
    // m_fileGeneration is intentionally NOT reset here (WR-03).

    m_selected = 0; // D-02: first row selected by default

    if (!m_query.isEmpty()) { // reset the query view per the header contract
        m_query.clear();
        emit queryChanged(m_query);
    }
    endResetModel();
    // Reset moved the selection → re-sync the QML ListView binding
    // (currentIndex: resultsModel.selectedIndex).
    if (oldSelected != 0)
        emit selectionChanged();
    emit hiddenCountChanged(); // 05.1: fresh catalog → hidden flags re-marked
}

void ResultsModel::dispatchProviderQuery()
{
    const quint64 gen = ++m_appGen;
    const QString q = m_query;
    const QVector<SearchProvider*> providers = m_providers;
    QThreadPool *pool = m_pool ? m_pool : QThreadPool::globalInstance();
    auto worker = [gen, q, providers]() {
        ProviderResult out;
        out.gen = gen;
        out.query = q;
        // Empty query = the full default inventory (breadth matches the old
        // sync buildAppOrder, capped at kCandidateCap). Typed queries stay
        // capped so the worker merges fast.
        const bool emptyQuery = q.trimmed().isEmpty();
        const int perProvider = emptyQuery ? kDefaultListCap : qMin(qMax(kMaxDisplayRows, 40), 80);
        for (SearchProvider *p : providers) {
            auto rows = p->query(q, perProvider, false);
            for (auto &r : rows)
                out.rows.append(std::move(r));
        }
        std::sort(out.rows.begin(), out.rows.end(), [](const ScoredEntry &a, const ScoredEntry &b){
            if (a.totalScore != b.totalScore) return a.totalScore > b.totalScore;
            return a.entry.displayName.toCaseFolded() < b.entry.displayName.toCaseFolded();
        });
        if (!emptyQuery && out.rows.size() > kMaxDisplayRows) out.rows.resize(kMaxDisplayRows);
        return out;
    };
    m_providerWatcher.setFuture(QtConcurrent::run(pool, worker));
}

void ResultsModel::setQuery(const QString &query)
{
    if (query == m_query)
        return;
    m_query = query;
    emit queryChanged(m_query);

    // Synchronous path only for tests (no pool). The empty default list is
    // NOT cheap when providers are wired: the full inventory (up to
    // kCandidateCap) rebuilds on every open / backspace-to-empty / scan —
    // building + sorting + reseting on the UI thread pinned the frame. With
    // providers, the same work runs off-thread (dispatchProviderQuery) and the
    // progressive display keeps the first block instant.
    if (!m_pool) {
        QElapsedTimer t; t.start();
        const int oldSelected = m_selected;
        beginResetModel();
        buildAppOrder();
        if (!query.isEmpty())
            mergeFiles();
        m_selected = 0;
        endResetModel();
        if (t.elapsed() > 8) qDebug() << "setQuery sync" << query << t.elapsed() << "ms ->" << m_order.size() << "rows";
        if (oldSelected != 0)
            emit selectionChanged();
        return;
    }

    // ── Phase-10 provider path: parallel fan-out, merged on the UI thread ──
    if (!m_providers.isEmpty()) {
        dispatchProviderQuery();
        return;
    }

    // Async scoring — off the UI thread so typing never blocks the frame.
    // Captures are copies (no UI-thread access inside worker).
    const quint64 gen = ++m_appGen;
    const QString q = query;
    const QVector<AppEntry> entries = m_entries;
    const QVector<QString> lowers = m_entriesLower;
    const QVector<QVector<char>> bounds = m_entriesBoundaries;
    const QVector<AppEntry> added = m_addedEntries;
    const QSet<QString> favIds = m_favoriteIds;
    const bool showHidden = m_showHidden;
    const bool favOnly = m_favoritesOnly;
    const QHash<QString, int> frecencyMap = m_frecencyMapFn ? m_frecencyMapFn() : QHash<QString, int>();

    auto worker = [gen, q, entries, lowers, bounds, added, favIds, showHidden, favOnly, frecencyMap]() -> AppResult {
        AppResult out;
        out.gen = gen;
        out.query = q;
        // Score apps using fast precomputed path
        QVector<QPair<Row, FuzzyMatcher::Result>> scored;
        scored.reserve(entries.size());
        const QString qLower = q.trimmed().toLower();
        const int minTier = (qLower.size() == 1) ? 800 : (qLower.size() == 2 ? 600 : 0);
        for (int i = 0; i < entries.size(); ++i) {
            if (entries.at(i).hidden && !showHidden) {
                const QString id = entries.at(i).targetPath.isEmpty() ? entries.at(i).aumid : entries.at(i).targetPath;
                if (!(favOnly && favIds.contains(id))) continue;
            }
            FuzzyMatcher::Result r;
            if (i < lowers.size() && i < bounds.size())
                r = scoreFast(qLower, lowers.at(i), bounds.at(i));
            else
                r = FuzzyMatcher::score(q, entries.at(i).displayName);
            if (r.score > 0 && r.score < minTier) continue;
            if (r.score > 0) {
                const QString id = entries.at(i).targetPath.isEmpty() ? entries.at(i).aumid : entries.at(i).targetPath;
                int boost = frecencyMap.value(id, 0);
                r.score += boost;
                scored.append({Row{i, false, false, false}, r});
            }
        }
        auto cmp2 = [&](const auto &a, const auto &b){
            if (a.second.score != b.second.score) return a.second.score > b.second.score;
            return entries.at(a.first.entryIndex).displayName.toCaseFolded() < entries.at(b.first.entryIndex).displayName.toCaseFolded();
        };
        int cap2 = ResultsModel::kMaxDisplayRows;
        if (qLower.size() == 1) cap2 = 30;
        else if (qLower.size() == 2) cap2 = 40;
        if (scored.size() > cap2) {
            std::nth_element(scored.begin(), scored.begin() + cap2, scored.end(), cmp2);
            scored.resize(cap2);
            std::sort(scored.begin(), scored.end(), cmp2);
        } else {
            std::sort(scored.begin(), scored.end(), cmp2);
        }
        if (auto calc = Calculator::evaluate(q)) {
            out.hasCalc = true;
            out.calcEntry.source = AppEntry::Source::Calculator;
            out.calcEntry.displayName = q.trimmed() + QStringLiteral(" = ") + *calc;
            out.calcEntry.targetPath = *calc;
            out.order.append(Row{0, false, false, true});
            out.ranges.append(FuzzyMatcher::Result{2000, {}});
        }
        for (auto &s : scored) { out.order.append(s.first); out.ranges.append(s.second); }
        if (favOnly) {
            QVector<Row> keep; QVector<FuzzyMatcher::Result> keepR;
            keep.reserve(out.order.size()); keepR.reserve(out.order.size());
            for (int i=0;i<out.order.size();++i) {
                const Row &rw = out.order.at(i);
                bool isFav = false;
                if (rw.isCalculator) isFav = true;
                else {
                    const AppEntry &e = rw.fromAdded ? added.at(rw.entryIndex) : entries.at(rw.entryIndex);
                    const QString id = e.targetPath.isEmpty() ? e.aumid : e.targetPath;
                    isFav = favIds.contains(id);
                }
                if (isFav) { keep.append(rw); keepR.append(out.ranges.at(i)); }
            }
            out.order = keep; out.ranges = keepR;
        }
        return out;
    };

    m_watcher.setFuture(QtConcurrent::run(m_pool ? m_pool : QThreadPool::globalInstance(), worker));
}

void ResultsModel::buildAppOrder()
{
    const bool oldHasCalc = m_hasCalc;
    const QString oldCalc = m_hasCalc ? m_calcEntry.targetPath : QString();
    m_order.clear();
    m_ranges.clear();
    m_hasCalc = false;

    if (m_query.isEmpty()) {
        if (oldHasCalc)
            emit calculatorResultChanged();
        // D-01: full list in canonical order — the curated catalog PLUS the
        // D-14 manual picks (CUR-04 escape hatch), interleaved alphabetically,
        // ONE merged list (no sectioning).
        QVector<int> appIdx; // visible app indices (canonical order)
        appIdx.reserve(m_entries.size());
        for (int i = 0; i < m_entries.size(); ++i) {
            // 2026-08-17: in Favorites mode, hidden rows that ARE favorited
            // stay in the pool — favorites are a positive user marker and the
            // Favorites tab is the user's explicit list (a favorited-but-hidden
            // row vanished from it while still showing in search; observed
            // with Wow.exe, hidden via curation + starred). Non-favorite
            // hidden rows keep the CUR-03 skip.
            if (m_entries.at(i).hidden && !m_showHidden
                && !(m_favoritesOnly && m_favoriteIds.contains(idOf(m_entries.at(i)))))
                continue; // 05.1: hidden rows render only in show-hidden mode (CUR-03)
            appIdx.append(i);
        }
        QVector<int> addIdx; // m_addedEntries is kept sorted (setFileResults)
        addIdx.reserve(m_addedEntries.size());
        for (int i = 0; i < m_addedEntries.size(); ++i) {
            if (m_addedEntries.at(i).hidden && !m_showHidden
                && !(m_favoritesOnly && m_favoriteIds.contains(idOf(m_addedEntries.at(i)))))
                continue; // 2026-08-12: manual picks hide like apps (same CUR-03 skip)
            addIdx.append(i);
        }

        m_order.reserve(appIdx.size() + addIdx.size());
        size_t a = 0, b = 0;
        while (a < appIdx.size() || b < addIdx.size()) {
            const bool takeApp = b >= addIdx.size()
                                 || (a < appIdx.size()
                                     && m_entries.at(appIdx.at(a)).displayName.toCaseFolded()
                                            <= m_addedEntries.at(addIdx.at(b)).displayName.toCaseFolded());
            if (takeApp)
                m_order.append(Row{ appIdx.at(a++), false });
            else
                m_order.append(Row{ addIdx.at(b++), false, true });
        }
        // 05.1 review (L-02): align to m_order.size() like setEntries — an
        // empty-query order that skips hidden rows must not carry ranges
        // sized to m_entries.size() (only coincidentally equal today).
        m_ranges = QVector<FuzzyMatcher::Result>(m_order.size());
        filterFavorites(); // 2026-08-15: favorites tab — prune to favorite rows when active
        return;
    }

    // Filter + rank once here; data() never recomputes (D-06 perf).
    // Fast path: precomputed lower + boundaries avoids per-entry toLower/toCaseFolded.
    const QString queryLower = m_query.trimmed().toLower();
    const int minTier = (queryLower.size() == 1) ? 800 : (queryLower.size() == 2 ? 600 : 0);
    QVector<QPair<Row, FuzzyMatcher::Result>> scored;
    for (int i = 0; i < m_entries.size(); ++i) {
        const FuzzyMatcher::Result r = (i < m_entriesLower.size() && i < m_entriesBoundaries.size())
                                           ? scoreFast(queryLower, m_entriesLower.at(i), m_entriesBoundaries.at(i))
                                           : FuzzyMatcher::score(m_query, m_entries.at(i).displayName);
        if (r.score > 0 && r.score < minTier)
            continue;
        // 2026-08-17: same Favorites-overrides-hidden rule as the empty-query
        // branch above — a favorited-but-hidden row stays searchable in
        // Favorites mode (search results are all-visible anyway, D-01).
        if (m_entries.at(i).hidden && !m_showHidden
            && !(m_favoritesOnly && m_favoriteIds.contains(idOf(m_entries.at(i)))))
            continue; // 05.1: same skip as the empty-query loop (CUR-03)
        if (r.score > 0)
            scored.append({ Row{ i, false }, r });
    }
    // Frecency boost: batched map = one lock per query (hot path) vs
    // per-result QSettings reads. Keeps < tier gap so tier order preserved.
    QHash<QString, int> frecencyMap;
    if (m_frecencyMapFn)
        frecencyMap = m_frecencyMapFn();
    if (!frecencyMap.isEmpty() || m_frecencyFn) {
        for (auto &s : scored) {
            const QString id = idOf(m_entries.at(s.first.entryIndex));
            const int boost = !frecencyMap.isEmpty() ? frecencyMap.value(id, 0)
                            : (m_frecencyFn ? m_frecencyFn(id) : 0);
            s.second.score += boost;
        }
    }
    auto cmp = [this](const auto &a, const auto &b) {
        if (a.second.score != b.second.score)
            return a.second.score > b.second.score;
        return m_entries.at(a.first.entryIndex).displayName.toCaseFolded()
               < m_entries.at(b.first.entryIndex).displayName.toCaseFolded();
    };
    int cap = kMaxDisplayRows;
    if (queryLower.size() == 1) cap = 30;
    else if (queryLower.size() == 2) cap = 40;
    if (scored.size() > cap) {
        std::nth_element(scored.begin(), scored.begin() + cap, scored.end(), cmp);
        scored.resize(cap);
        std::sort(scored.begin(), scored.end(), cmp);
    } else {
        std::sort(scored.begin(), scored.end(), cmp);
    }
    m_order.reserve(scored.size() + 1);
    m_ranges.reserve(scored.size() + 1);
    // Calculator synthetic row: top of list when query is math
    if (auto calc = Calculator::evaluate(m_query)) {
        m_calcEntry.source = AppEntry::Source::Calculator;
        m_calcEntry.displayName = m_query.trimmed() + QStringLiteral(" = ") + *calc;
        m_calcEntry.targetPath = *calc;
        m_hasCalc = true;
        m_order.append(Row{ 0, false, false, true });
        m_ranges.append(FuzzyMatcher::Result{ 2000, {} }); // always top
    }
    for (const auto &s : scored) {
        m_order.append(s.first);
        m_ranges.append(s.second);
    }
    filterFavorites(); // 2026-08-15: favorites tab — prune to favorite rows when active
    if (m_hasCalc != oldHasCalc || (m_hasCalc && m_calcEntry.targetPath != oldCalc))
        emit calculatorResultChanged();
}

void ResultsModel::mergeFiles()
{
    // Candidates: the app rows just built by buildAppOrder() plus every file
    // row, scored on displayName (D-07: a name match keeps its real tier; a
    // path-only match falls back to the base tier so it ranks below EVERY
    // name match — apps and files alike).
    struct Candidate { Row row; FuzzyMatcher::Result result; };
    QVector<Candidate> merged;
    merged.reserve(m_order.size() + m_fileEntries.size());
    // D-03: path-set dedupe — scanned rows duplicating an app-catalog row
    // (Lnk/tracked/added) are suppressed; the catalog row (icon, display
    // name) wins. Built from path-bearing rows ONLY — UWP rows have an
    // empty targetPath and can never collide (Pitfall 10). Keys are
    // case-folded native paths (T-07-03: one consistent fold, no raw vs
    // normalized mixing — paths arrive normalized from every source).
    QSet<QString> appPaths;
    for (int i = 0; i < m_order.size(); ++i) {
        if (m_order.at(i).fromFiles)
            continue;
        const QString p = entryAt(m_order.at(i)).targetPath;
        if (!p.isEmpty())
            appPaths.insert(p.toCaseFolded());
    }
    for (int i = 0; i < m_order.size(); ++i) {
        if (m_order.at(i).fromFiles)
            continue; // file rows from an earlier merge — the loop below rebuilds them fresh
        merged.append({ m_order.at(i), m_ranges.at(i) });
    }
    const QString trimmedQuery = m_query.trimmed();
    const int fileMinTier = (trimmedQuery.size() == 1) ? 800 : (trimmedQuery.size() == 2 ? 600 : 0);
    for (int i = 0; i < m_fileEntries.size(); ++i) {
        const FuzzyMatcher::Result r = FuzzyMatcher::score(trimmedQuery, m_fileEntries.at(i).displayName);
        if (r.score > 0 && r.score < fileMinTier)
            continue;
        // For short queries, don't show path-only file hits (score 100) — too broad
        if (r.score == 0 && fileMinTier > 0)
            continue;
        const int fileScore = r.score > 0 ? r.score : kPathMatchScore;
        merged.append({ Row{ i, true }, FuzzyMatcher::Result{ fileScore, r.ranges } });
    }

    // D-01: ONE list — score desc, then displayName asc case-folded (D-05),
    // across BOTH sources. No sectioning, no app-priority.
    std::sort(merged.begin(), merged.end(), [this](const Candidate &a, const Candidate &b) {
        if (a.result.score != b.result.score)
            return a.result.score > b.result.score;
        return entryAt(a.row).displayName.toCaseFolded()
               < entryAt(b.row).displayName.toCaseFolded();
    });

    // Dynamic cap for broad queries — "S" shouldn't build 80 delegates
    int mergeCap = kMaxDisplayRows;
    if (trimmedQuery.size() == 1) mergeCap = 30;
    else if (trimmedQuery.size() == 2) mergeCap = 40;
    // Cap the merged list — maniac typing on "Steam" with 1000 file rows
    // would otherwise rebuild 1000 delegates per keystroke.
    if (merged.size() > mergeCap) {
        // Keep all app rows (they're at most ~500) and trim file tail
        std::nth_element(merged.begin(), merged.begin() + mergeCap, merged.end(),
                         [this](const Candidate &a, const Candidate &b) {
                             if (a.result.score != b.result.score)
                                 return a.result.score > b.result.score;
                             return entryAt(a.row).displayName.toCaseFolded()
                                    < entryAt(b.row).displayName.toCaseFolded();
                         });
        merged.resize(mergeCap);
        std::sort(merged.begin(), merged.end(), [this](const Candidate &a, const Candidate &b) {
            if (a.result.score != b.result.score)
                return a.result.score > b.result.score;
            return entryAt(a.row).displayName.toCaseFolded()
                   < entryAt(b.row).displayName.toCaseFolded();
        });
    }
    // D-03: keep the highest-scored kMaxFileRows file rows (they are the file
    // rows encountered first in the sorted union). Apps are never dropped.
    m_order.clear();
    m_ranges.clear();
    m_order.reserve(merged.size());
    m_ranges.reserve(merged.size());
    int fileRows = 0;
    for (const Candidate &c : merged) {
        if (c.row.fromFiles && fileRows >= kMaxFileRows)
            continue;
        if (c.row.fromFiles
            && appPaths.contains(entryAt(c.row).targetPath.toCaseFolded()))
            continue; // D-03: catalog row already renders this path — scan row suppressed
        if (c.row.fromFiles)
            ++fileRows;
        m_order.append(c.row);
        m_ranges.append(c.result);
    }
    filterFavorites(); // 2026-08-15: favorites tab — prune to favorite rows when active
}

void ResultsModel::setFileResults(quint64 generation, const QString &query,
                                  QVector<AppEntry> files)
{
    // WR-03: relevance check FIRST — a result computed for old query text
    // (same generation, delivered inside the debounce window or across a
    // catalog refresh that reset m_query) never merges under the current
    // query, and never advances the generation guard.
    if (query != m_query)
        return;
    if (generation < m_fileGeneration)
        return; // D-15: stale generation dropped model-side too (defense in depth)
    m_fileGeneration = generation;

    if (m_query.isEmpty()) {
        // 07-06 (default list): the executable launcher's default list — the
        // snapshot FileSearch dispatched (index .exe rows + manual picks,
        // deduped upstream) refills the m_addedEntries channel and rebuilds
        // the default list. Sorted here for the buildAppOrder interleave.
        std::sort(files.begin(), files.end(), [](const AppEntry &a, const AppEntry &b) {
            return a.displayName.toCaseFolded() < b.displayName.toCaseFolded();
        });
        m_addedEntries = std::move(files);

        // Phase-10: with providers wired, the DEFAULT list is built off the
        // UI thread too (dispatchProviderQuery) — the FileSearch snapshot that
        // lands here is the same index the provider fan-out already queried,
        // so this branch must NOT beginResetModel a second time (it would
        // rebuild every delegate for no visible change — the "loads a long
        // list, lags" jank on open/backspace/scan). Re-dispatch async instead;
        // the m_pool guard keeps the sync path for tests.
        if (!m_providers.isEmpty()) {
            if (m_pool)
                dispatchProviderQuery();
            return;
        }

        beginResetModel();
        buildAppOrder();
        // D-02 applies to query changes only: file arrival NEVER resets the
        // cursor — only clamp when the merged list shrank past it.
        const int last = m_order.size() - 1;
        if (m_selected > last) {
            const int clamped = last < 0 ? 0 : qBound(0, m_selected, last);
            if (clamped != m_selected) {
                m_selected = clamped;
                emit selectionChanged(); // re-sync the QML ListView binding
            }
        }
        endResetModel();
        return;
    }

    // Phase-10 (providers own typed queries): with providers wired, the
    // typed-query merge is the provider fan-out's job (applyProviderResult).
    // FileSearch re-dispatch on a scan completion still needs the index, so
    // only the EMPTY-query default-list branch above runs; a typed result
    // delivery here would double-render file rows from both channels.
    if (!m_providers.isEmpty())
        return;

    m_fileEntries = std::move(files);

    beginResetModel();
    mergeFiles();
    // D-02 applies to query changes only: file arrival NEVER resets the
    // cursor — only clamp when the merged list shrank past it.
    const int last = m_order.size() - 1;
    if (m_selected > last) {
        const int clamped = last < 0 ? 0 : qBound(0, m_selected, last);
        if (clamped != m_selected) {
            m_selected = clamped;
            emit selectionChanged(); // re-sync the QML ListView binding
        }
    }
    endResetModel();
}

QString ResultsModel::query() const
{
    return m_query;
}

int ResultsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_order.size();
}

QVariant ResultsModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_order.size())
        return {};
    // Phase-4: rows resolve against m_entries (apps) or m_fileEntries (files).
    // D-14: m_addedEntries (manual picks) resolve through the third branch.
    const Row &row = m_order.at(idx.row());
    const AppEntry &entry = entryAt(row);

    switch (role) {
    case DisplayNameRole:
        return entry.displayName;
    case SubtitleRole: {
        if (entry.source == AppEntry::Source::Calculator)
            return QStringLiteral("Copy result");
        // Phase-11 (D-09): a Command row's title IS the command; the subtitle
        // tells the user what Enter will do.
        if (entry.source == AppEntry::Source::Command)
            return QStringLiteral("Run in terminal");
        // D-02: File rows subtitle = the FULL path (elided in QML), not the
        // file name — BEFORE the existing Lnk/Uwp handling, which stays.
        if (entry.source == AppEntry::Source::File)
            return entry.targetPath;
        // Lnk → target file name; UWP / missing path → empty subtitle.
        if (entry.source == AppEntry::Source::Uwp || entry.targetPath.isEmpty())
            return QString();
        return QFileInfo(entry.targetPath).fileName();
    }
    case IsFolderRole:
        // D-04: folder file rows only — apps and plain files are false.
        return entry.isFolder;
    case IconKeyRole: {
        if (entry.source == AppEntry::Source::Calculator)
            return QStringLiteral("calc");
        // Phase-11: "cmd" → the ResultsRow monogram's ">" prompt marker.
        if (entry.source == AppEntry::Source::Command)
            return QStringLiteral("cmd");
        // 05-04: image://wispicons/{id} source in the parseKey grammar
        // (WinIconExtractor.h:49-72). Lnk → the enumerator's iconRef
        // ('path;index', GetIconLocation output) verbatim, else "path:" +
        // path; File → "path:" + path; Uwp → the 'uwp:PFN|appId' iconRef
        // ("" when the enumerator couldn't emit one — the QML monogram
        // fallback covers it, D-04/D-16).
        if (entry.source == AppEntry::Source::Uwp)
            return entry.iconRef;
        if (entry.source == AppEntry::Source::File || entry.iconRef.isEmpty())
            return QStringLiteral("path:") + entry.targetPath;
        return entry.iconRef;
    }
    case MatchRangesRole: {
        // [[{start, length}, ...]] — nested QVariantList-of-QVariantList per
        // the header contract (03-05/Phase-5 highlight shape). Build the inner
        // list BEFORE appending: a bare braced-init-list appended inline would
        // be interpreted as a QList<QVariant> spread (flattening every run to
        // the parent list — the 03-05 RED gate caught this exact shape bug).
        // File rows' ranges were computed at merge time (empty for path-only).
        QVariantList ranges;
        for (const FuzzyMatcher::MatchRange &r : m_ranges.at(idx.row()).ranges) {
            QVariantList run;
            run.append(r.start);
            run.append(r.length);
            ranges.append(QVariant(run));
        }
        return ranges;
    }
    case AumidRole:
        return entry.aumid;
    case IsHiddenRole:
        return entry.hidden;
    case IsHideableRole:
        // Phase-11: Command rows are ephemeral (rebuilt per query) — never
        // a dead remove button, mirroring the Calculator exclusion.
        if (entry.source == AppEntry::Source::Calculator
            || entry.source == AppEntry::Source::Command)
            return false;
        // CUR-04 parity with hideSelected(): TRANSIENT index file rows are
        // never hideable (the search escape hatch stays open); app rows,
        // UWP rows, and manual picks (fromAdded) all are.
        return !(entry.source == AppEntry::Source::File && !row.fromAdded);
    case IsFavoriteRole:
        if (entry.source == AppEntry::Source::Calculator)
            return false;
        // 2026-08-15: favorites are per-ID (targetPath/aumid), so membership
        // survives rebuilds for ANY row type (file rows too).
        return m_favoriteIds.contains(idOf(entry));
    default:
        return {};
    }
}

int ResultsModel::selectedIndex() const
{
    return m_selected;
}

void ResultsModel::moveSelection(int delta)
{
    selectIndex(m_selected + delta);
}

void ResultsModel::selectIndex(int index)
{
    const int last = m_order.size() - 1;
    const int clamped = last < 0 ? 0 : qBound(0, index, last);
    if (clamped == m_selected)
        return; // no spurious NOTIFY — the QML binding stays quiet
    m_selected = clamped;
    emit selectionChanged();
}

AppEntry ResultsModel::snapshotSelected() const
{
    if (m_order.isEmpty())
        return {};
    // Phase-4: file rows return the real value copy (D-12 freeze holds too).
    return entryAt(m_order.at(m_selected));
}

const AppEntry &ResultsModel::entryAt(const Row &row) const
{
    if (row.isCalculator)
        return m_calcEntry;
    if (row.fromProvider)
        return m_providerRows.at(row.entryIndex).entry;
    if (row.fromAdded)
        return m_addedEntries.at(row.entryIndex);
    if (row.fromFiles)
        return m_fileEntries.at(row.entryIndex);
    return m_entries.at(row.entryIndex);
}

// ── 05.1 curation surface (CUR-02/CUR-03/CUR-04) ──

bool ResultsModel::showHidden() const
{
    return m_showHidden;
}

void ResultsModel::setShowHidden(bool on)
{
    if (m_showHidden == on)
        return;
    const int oldSelected = m_selected;
    m_showHidden = on;
    beginResetModel();
    buildAppOrder();
    if (!m_query.isEmpty())
        mergeFiles();
    const int last = m_order.size() - 1;
    if (m_selected > last) {
        m_selected = last < 0 ? 0 : last;
        if (oldSelected != m_selected)
            emit selectionChanged(); // P7: emit only when the clamp moved it
    }
    endResetModel();
    emit showHiddenChanged();
}

int ResultsModel::hiddenCount() const
{
    int n = 0;
    for (const AppEntry &e : m_entries)
        if (e.hidden)
            ++n;
    // 2026-08-12: manual picks count too (hideable, D-14 channel)
    for (const AppEntry &e : m_addedEntries)
        if (e.hidden)
            ++n;
    return n;
}

void ResultsModel::setHideStore(HideStore fn)
{
    m_hideStore = std::move(fn);
}

void ResultsModel::hideSelected()
{
    if (m_order.isEmpty())
        return;
    // CUR-04 escape-hatch guard: index File rows are never hideable — the
    // search escape hatch must stay open. The carve-out: manual picks
    // (fromAdded — D-14) ARE curated entries the user chose; hiding one
    // declutters the default list, and show-hidden mode can unhide it.
    // Guard by SOURCE, not the fromFiles flag: a File entry that arrived
    // via setEntries builds a fromFiles=false row (catalog-modeled rows
    // must stay non-hideable too).
    const Row &row = m_order.at(m_selected);
    const AppEntry e = entryAt(row);
    if (e.source == AppEntry::Source::File && !row.fromAdded)
        return;
    // Phase-11: Command rows are ephemeral — their id IS the command text,
    // which must never be persisted as a hide-only-via-store identity.
    if (e.source == AppEntry::Source::Command)
        return;
    const QString id = e.targetPath.isEmpty() ? e.aumid : e.targetPath;
    if (id.isEmpty())
        return;   // no identity (empty selection) — nothing to persist
    if (m_hideStore)
        m_hideStore(id, true);
    const int oldSelected = m_selected;
    beginResetModel();
    // 05.1 checkpoint fix: MARK the row hidden instead of removing it from
    // m_entries. Removal made hiddenCount() read 0 in-session (the "Show
    // hidden (N)" footer never rendered) and left Unhide with no row to find
    // until a catalog rebuild re-marked rule-hidden entries. The row's
    // disappearance from the visible order is already handled by
    // buildAppOrder()'s `hidden && !m_showHidden` skip — no rebuild needed.
    // 2026-08-12: mark the ADDED channel too (manual picks are hideable) —
    // same id-matching, same M-01 no-break (all same-id rows).
    for (AppEntry &x : m_entries) {
        const QString xid = x.targetPath.isEmpty() ? x.aumid : x.targetPath;
        // 05.1 review (M-01): NO break — the persisted hide targets the id,
        // and a catalog rebuild hides EVERY entry with that id (two Start
        // Menu .lnk rows sharing one targetPath, e.g. per-user + all-users
        // Steam). Marking only the first row left its twin visible and made
        // hiddenCount disagree with the store. Mark all, like markCurated.
        if (xid == id) x.hidden = true;
    }
    for (AppEntry &x : m_addedEntries) {
        const QString xid = x.targetPath.isEmpty() ? x.aumid : x.targetPath;
        if (xid == id) x.hidden = true;
    }
    buildAppOrder();
    if (!m_query.isEmpty())
        mergeFiles(); // file rows re-merge against the smaller app set
    const int last = m_order.size() - 1;
    if (m_selected > last) {
        m_selected = last < 0 ? 0 : last;
        if (oldSelected != m_selected)
            emit selectionChanged();
    }
    endResetModel();
    emit hiddenCountChanged(); // 05.1: +1 hidden → footer count re-renders
}

void ResultsModel::unhideSelected()
{
    if (m_order.isEmpty())
        return;
    const Row &row = m_order.at(m_selected);
    const AppEntry e = entryAt(row);
    if (e.source == AppEntry::Source::File && !row.fromAdded)
        return;   // CUR-04 escape-hatch guard (see hideSelected)
    // 05.1 review (L-01): a row that is NOT hidden must not persist a
    // spurious shown-override — Ctrl+H in show-hidden mode on a visible row
    // would otherwise pin it visible forever against future rule changes.
    if (!e.hidden)
        return;
    const QString id = e.targetPath.isEmpty() ? e.aumid : e.targetPath;
    if (id.isEmpty())
        return;
    if (m_hideStore)
        m_hideStore(id, false);
    beginResetModel();
    // Shown override beats rules (markCurated precedence, plan 05.1-02):
    // mirror the re-mark result locally — no rebuild needed. Added channel
    // too — manual picks are hideable (2026-08-12).
    for (AppEntry &x : m_entries) {
        const QString xid = x.targetPath.isEmpty() ? x.aumid : x.targetPath;
        // 05.1 review (M-01): mark ALL same-id rows (markCurated parity) —
        // see hideSelected for the twin-row rationale.
        if (xid == id) x.hidden = false;
    }
    for (AppEntry &x : m_addedEntries) {
        const QString xid = x.targetPath.isEmpty() ? x.aumid : x.targetPath;
        if (xid == id) x.hidden = false;
    }
    buildAppOrder();
    if (!m_query.isEmpty())
        mergeFiles();
    endResetModel();
    emit hiddenCountChanged(); // 05.1: -1 hidden → footer count re-renders
}

// ── 2026-08-15 favorites surface ──

bool ResultsModel::isFavoriteRow(const Row &row) const
{
    return m_favoriteIds.contains(idOf(entryAt(row)));
}

void ResultsModel::filterFavorites()
{
    if (!m_favoritesOnly)
        return;
    QVector<Row> keep;
    QVector<FuzzyMatcher::Result> keepRanges;
    keep.reserve(m_order.size());
    keepRanges.reserve(m_order.size());
    for (int i = 0; i < m_order.size(); ++i) {
        if (m_order.at(i).isCalculator || isFavoriteRow(m_order.at(i))) {
            keep.append(m_order.at(i));
            keepRanges.append(m_ranges.at(i));
        }
    }
    m_order = std::move(keep);
    m_ranges = std::move(keepRanges);
}

// Phase-11 perf fix (2026-09-02): morph m_order/m_ranges from the current
// (old-tab) view into bRows/bRanges (the new-tab view) via batched row deltas
// instead of beginResetModel. Qt reconnect protocol: the row indices in the
// remove/insert signals are against the model state AS IT EVOLVES, so we MUST
// start from the old view and apply removals/inserts in lockstep. Survivors
// (rows present in both the old and new view) keep their ListView delegates —
// no rich-text/chip/elide re-run, no icon re-load.
void ResultsModel::applyFavoritesDelta(const QVector<Row> &bRows,
                                       const QVector<FuzzyMatcher::Result> &bRanges)
{
    // Packed integer key uniquely identifies a Row within
    // buildAppOrder/mergeFiles output: entryIndex (unique per origin array) in
    // the low 32 bits + the 4 origin/calc flag bits above. No string allocs —
    // a tab switch over ~1000 rows must not build ~4000 QString signatures in
    // a debug build (measured ~32ms); this keeps it O(1)-allocation per row.
    auto rowKey = [](const Row &r) -> quint64 {
        return quint64(quint64(r.isCalculator) << 33 | quint64(r.fromProvider) << 32
                       | quint64(r.fromAdded) << 31 | quint64(r.fromFiles) << 30)
            | quint64(r.entryIndex & 0x3FFFFFFFu);
    };
    auto rowKeyOf = [&](int i) -> quint64 { return rowKey(m_order.at(i)); };

    // ── Pass 1: compute BOTH delta directions without emitting, and count the
    // signal batches each would produce (a "run" = one contiguous
    // begin/end{Remove,Insert}Rows pair; the target index of an insertion moves
    // while insertions are applied, so runs here are counted per raw bRows
    // position difference — the same scatter the emitter below sees).
    // Removals: old-view rows absent from the new view.
    QSet<quint64> keep;
    keep.reserve(bRows.size());
    for (const Row &r : bRows)
        keep.insert(rowKey(r));
    QVector<int> rem;
    for (int i = 0; i < m_order.size(); ++i)
        if (!keep.contains(rowKeyOf(i)))
            rem.append(i);
    // Insertions: new-view rows absent from the (post-removal) old view.
    QSet<quint64> have;
    for (const Row &r : m_order)
        have.insert(rowKey(r));
    QVector<QPair<int, int>> ins; // (modelIndex, bRows index) per missing row
    int survSeen = 0, insDone = 0;
    for (int bix = 0; bix < bRows.size(); ++bix) {
        if (have.contains(rowKey(bRows.at(bix))))
            ++survSeen;
        else {
            ins.append({ survSeen + insDone, bix });
            ++insDone;
        }
    }
    auto countRuns = [](const QVector<int> &v) {
        int runs = 0;
        int n = 0;
        while (n < v.size()) {
            ++runs;
            int m = n;
            while (m + 1 < v.size() && v.at(m + 1) == v.at(m) + 1)
                ++m;
            n = m + 1;
        }
        return runs;
    };
    const int remRuns = countRuns(rem);
    // Insertion runs: an insertion's target index = survSeen + insDone; two
    // adjacent bRows entries form one run when their PLACED positions are
    // consecutive, which here means the intervening bRows survivors are exactly
    // contiguous too. Counting by (ins[k].first + k - ins[i].first - i)
    // consecutive-ness mirrors the emitter's while loop below.
    int insRuns = 0;
    for (int k = 0; k < ins.size(); ++k) {
        if (k == 0 || ins.at(k).first != ins.at(k - 1).first + 1)
            ++insRuns;
    }

    // ── Policy: LOCALIZED change → row deltas (survivors keep delegates).
    // Scattered / wholesale change → ONE reset (single signal pair, one QML
    // layout pass) — hundreds of tiny row-delta pairs each force a QML
    // highlight re-eval + buffer-window re-layout, the "Favorites→All stutter".
    constexpr int kMaxDeltaRuns = 24;
    if (remRuns + insRuns > kMaxDeltaRuns) {
        beginResetModel();
        m_order = bRows;
        m_ranges = bRanges;
        endResetModel();
        return;
    }

    // ── Pass 2: apply removals (DESCENDING run order so earlier precomputed
    // indices stay valid as the model shrinks). Within a run, removing at the
    // run's first index `count` times collapses exactly that span.
    int r = rem.size() - 1;
    while (r >= 0) {
        int j = r;
        while (j - 1 >= 0 && rem.at(j - 1) == rem.at(j) - 1)
            --j;
        const int first = rem.at(j), last = rem.at(r);
        beginRemoveRows(QModelIndex(), first, last);
        for (int k = first; k <= last; ++k) {
            m_order.removeAt(first);
            m_ranges.removeAt(first);
        }
        endRemoveRows();
        r = j - 1;
    }

    // ── Pass 3: apply insertions (batched contiguous runs, ascending — Qt's
    // documented safe order). Survivors preserve their relative order; a
    // missing row slots after the survivors already placed before it.
    int i = 0;
    while (i < ins.size()) {
        int j = i;
        while (j + 1 < ins.size() && ins.at(j + 1).first == ins.at(j).first + 1)
            ++j;
        const int first = ins.at(i).first, n = j - i + 1;
        beginInsertRows(QModelIndex(), first, first + n - 1);
        for (int k = i; k <= j; ++k) {
            const int pos = ins.at(k).first;
            m_order.insert(pos, bRows.at(ins.at(k).second));
            m_ranges.insert(pos, bRanges.at(ins.at(k).second));
        }
        endInsertRows();
        i = j + 1;
    }
}

bool ResultsModel::favoritesOnly() const
{
    return m_favoritesOnly;
}

int ResultsModel::favoriteCount() const
{
    return m_favoriteIds.size();
}

void ResultsModel::setFavoritesOnly(bool on)
{
    if (m_favoritesOnly == on)
        return;
    const int oldSelected = m_selected;

    // Phase-11 perf fix (2026-09-02): the old path did a full beginResetModel,
    // tearing down every ListView delegate (a large set while browsing the
    // empty-query list, whose cacheBuffer/displayMargin keep ~70 rows alive) →
    // the felt few-ms tab stutter. Now we compute the NEW-tab view with the
    // real builders (honoring the flag's hidden-override + favorites filter),
    // then apply it as batched row deltas so surviving delegates are reused.
    const QVector<Row> oldRows = m_order;
    const QVector<FuzzyMatcher::Result> oldRanges = m_ranges;
    m_favoritesOnly = on;

    // Build the target view into m_order/m_ranges under the new flag; also
    // refreshes m_hasCalc/m_calcEntry and emits calculatorResultChanged.
    // Snapshot the target, restore the old view, then morph old → target with
    // row-level signals (Qt protocol: signal indices are against the model
    // state as it evolves, i.e. starting from oldRows).
    buildAppOrder();
    if (!m_query.isEmpty())
        mergeFiles();
    QVector<Row> targetRows = m_order;
    QVector<FuzzyMatcher::Result> targetRanges = m_ranges;
    m_order = oldRows;
    m_ranges = oldRanges;

    applyFavoritesDelta(targetRows, targetRanges);

    const int last = m_order.size() - 1;
    if (m_selected > last) {
        m_selected = last < 0 ? 0 : last;
        if (oldSelected != m_selected)
            emit selectionChanged(); // P7: emit only when the clamp moved it
    }
    emit favoritesOnlyChanged();
}

void ResultsModel::setFavoriteStore(FavoriteStore fn)
{
    m_favoriteStore = std::move(fn);
}

void ResultsModel::setFavoriteIds(const QSet<QString> &ids)
{
    m_favoriteIds = ids;
}

void ResultsModel::setFrecencyFn(FrecencyFn fn)
{
    m_frecencyFn = std::move(fn);
}

void ResultsModel::setFrecencyMapFn(FrecencyMapFn fn)
{
    m_frecencyMapFn = std::move(fn);
}

QString ResultsModel::calculatorResult() const
{
    if (!m_hasCalc)
        return QString();
    return m_calcEntry.targetPath;
}

void ResultsModel::favoriteSelected()
{
    if (m_order.isEmpty())
        return;
    const Row &row = m_order.at(m_selected);
    const AppEntry e = entryAt(row);
    const QString id = idOf(e);
    if (id.isEmpty())
        return; // no identity — nothing to persist
    if (m_favoriteStore)
        m_favoriteStore(id, true);
    m_favoriteIds.insert(id);
    const int oldSelected = m_selected;
    beginResetModel();
    buildAppOrder();
    if (!m_query.isEmpty())
        mergeFiles();
    const int last = m_order.size() - 1;
    if (m_selected > last) {
        m_selected = last < 0 ? 0 : last;
        if (oldSelected != m_selected)
            emit selectionChanged(); // P7: emit only when the clamp moved it
    }
    endResetModel();
}

void ResultsModel::unfavoriteSelected()
{
    if (m_order.isEmpty())
        return;
    const Row &row = m_order.at(m_selected);
    const AppEntry e = entryAt(row);
    const QString id = idOf(e);
    if (id.isEmpty())
        return;
    // L-01-style guard: not a favorite → no spurious store write.
    if (!m_favoriteIds.contains(id))
        return;
    if (m_favoriteStore)
        m_favoriteStore(id, false);
    m_favoriteIds.remove(id);
    const int oldSelected = m_selected;
    beginResetModel();
    buildAppOrder();
    if (!m_query.isEmpty())
        mergeFiles();
    const int last = m_order.size() - 1;
    if (m_selected > last) {
        m_selected = last < 0 ? 0 : last;
        if (oldSelected != m_selected)
            emit selectionChanged(); // P7: emit only when the clamp moved it
    }
    endResetModel();
}
