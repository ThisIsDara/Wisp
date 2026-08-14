#include "core/ResultsModel.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>

ResultsModel::ResultsModel(QObject *parent)
    : QAbstractListModel(parent)
{
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

void ResultsModel::setQuery(const QString &query)
{
    if (query == m_query)
        return; // file arrival never depends on this — setFileResults recomputes

    const int oldSelected = m_selected;
    beginResetModel();
    m_query = query;

    buildAppOrder();
    if (!query.isEmpty())
        mergeFiles(); // D-01: file rows join the ranked list only on a live query

    m_selected = 0; // D-02: first row selected after every query change
    endResetModel();
    emit queryChanged(m_query);
    if (oldSelected != 0)
        emit selectionChanged(); // re-sync the QML ListView binding
}

void ResultsModel::buildAppOrder()
{
    m_order.clear();
    m_ranges.clear();

    if (m_query.isEmpty()) {
        // D-01: full list in canonical order — the curated catalog PLUS the
        // D-14 manual picks (CUR-04 escape hatch), interleaved alphabetically,
        // ONE merged list (no sectioning).
        QVector<int> appIdx; // visible app indices (canonical order)
        appIdx.reserve(m_entries.size());
        for (int i = 0; i < m_entries.size(); ++i) {
            if (m_entries.at(i).hidden && !m_showHidden)
                continue; // 05.1: hidden rows render only in show-hidden mode (CUR-03)
            appIdx.append(i);
        }
        QVector<int> addIdx; // m_addedEntries is kept sorted (setFileResults)
        addIdx.reserve(m_addedEntries.size());
        for (int i = 0; i < m_addedEntries.size(); ++i) {
            if (m_addedEntries.at(i).hidden && !m_showHidden)
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
        return;
    }

    // Filter + rank once here; data() never recomputes (D-06 perf).
    QVector<QPair<Row, FuzzyMatcher::Result>> scored;
    for (int i = 0; i < m_entries.size(); ++i) {
        const FuzzyMatcher::Result r = FuzzyMatcher::score(m_query, m_entries.at(i).displayName);
        if (m_entries.at(i).hidden && !m_showHidden)
            continue; // 05.1: same skip as the empty-query loop (CUR-03)
        if (r.score > 0)
            scored.append({ Row{ i, false }, r });
    }
    std::sort(scored.begin(), scored.end(), [this](const auto &a, const auto &b) {
        if (a.second.score != b.second.score)
            return a.second.score > b.second.score; // D-04 rank desc
        // D-05: alphabetical tie-break — stable and predictable
        return m_entries.at(a.first.entryIndex).displayName.toCaseFolded()
               < m_entries.at(b.first.entryIndex).displayName.toCaseFolded();
    });
    m_order.reserve(scored.size());
    m_ranges.reserve(scored.size());
    for (const auto &s : scored) {
        m_order.append(s.first);
        m_ranges.append(s.second);
    }
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
    for (int i = 0; i < m_fileEntries.size(); ++i) {
        const FuzzyMatcher::Result r = FuzzyMatcher::score(m_query, m_fileEntries.at(i).displayName);
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
        // D-14 (default list): the added-only snapshot (manual picks, CUR-04)
        // refills the m_addedEntries channel and rebuilds the default list —
        // a freshly added executable shows the instant the dialog closes.
        // Sorted here for the buildAppOrder interleave (D-01 canonical order).
        std::sort(files.begin(), files.end(), [](const AppEntry &a, const AppEntry &b) {
            return a.displayName.toCaseFolded() < b.displayName.toCaseFolded();
        });
        m_addedEntries = std::move(files);

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
