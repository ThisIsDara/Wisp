#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QHash>
#include <QSet>
#include <QVector>

#include <functional>

#include "core/AppEntry.h"
#include "core/FuzzyMatcher.h"
#include "core/SearchProvider.h"

class QThreadPool;

// QML consumes this via a context property in 03-05. Rendered rows are the
// permutation m_order over the always-alphabetical m_entries.
//
// MatchRangesRole shape (03-05 Phase-5 highlight contract): a QVariantList
// of two-int lists, [{start, length}, ...] — one entry per contiguous matched
// run, positions into the ORIGINAL displayName. Read as ranges[i][0]/[1].
//
// QML contracts (03-05): roleNames expose displayName/subtitle/matchRanges/
// aumid to the delegate; selectionChanged NOTIFY keeps ListView's
// `currentIndex: resultsModel.selectedIndex` binding live (single source of
// selection truth — hover and keyboard render through the same highlight).
class ResultsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString query READ query NOTIFY queryChanged) // 03-05 empty-state copy
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectionChanged) // LAUN-05 nav binding
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged) // 05.1: show-hidden mode toggle
    Q_PROPERTY(int hiddenCount READ hiddenCount NOTIFY hiddenCountChanged) // 05.1: footer visibility (rule- AND user-hidden)
    Q_PROPERTY(bool favoritesOnly READ favoritesOnly WRITE setFavoritesOnly NOTIFY favoritesOnlyChanged) // 2026-08-15: "Favorites" tab mode
    Q_PROPERTY(int favoriteCount READ favoriteCount) // 2026-08-15: persisted favorite-id count — startup tab default (All if 0)
    Q_PROPERTY(QString calculatorResult READ calculatorResult NOTIFY calculatorResultChanged)
    // ── Duplicate detection (2026-10-08) ────────────────────────────────
    // Two rows are duplicates when they share a display title — the same app listed
    // more than once. Startup-folder autostart entries are excluded. Pure string
    // work: no file I/O, so it is safe to recompute on every order change. See
    // ResultsModel.cpp's duplicateKeyOf() for the measured rationale.
    Q_PROPERTY(int duplicateGroupCount READ duplicateGroupCount NOTIFY duplicatesChanged) // footer chip
    Q_PROPERTY(QVariantList duplicateGroups READ duplicateGroups NOTIFY duplicatesChanged) // panel model

public:
    enum Roles {
        DisplayNameRole = Qt::UserRole + 1,
        SubtitleRole,
        MatchRangesRole,
        AumidRole,
        IsFolderRole, // D-04: QML folder glyph — true for folder file rows only
        IconKeyRole,  // 05-04: image://wispicons/{id} — Lnk 'path;index', File 'path:path', Uwp 'uwp:PFN|appId'
        IsHiddenRole, // 05.1: QML dims hidden rows via model.isHidden
        IsHideableRole, // 2026-08-15: remove-button visibility (CUR-04 guard parity)
        IsFavoriteRole, // 2026-08-15: QML star — true if the row's id (targetPath/aumid) is favorited
        CanRevealRole, // 0.1.8: "Open file location" menu item may show for this row (File, or Lnk with a resolved target)
        IsDuplicateRole, // 2026-10-08: this row has a same-name/same-folder twin
    };

    explicit ResultsModel(QObject *parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;

    // Full catalog snapshot (03-03 signals → this). Resets query to "" and selection to 0.
    void setEntries(QVector<AppEntry> entries);
    // Query → filter+rank via FuzzyMatcher::score, sort score desc then displayName asc (D-05).
    // Empty query → all entries + manual picks (CUR-04/D-14) interleaved
    // alphabetically (D-01), selection index 0 (D-02).
    Q_INVOKABLE void setQuery(const QString &query);
    QString query() const; // for the 03-05 "No results for \"{query}\"" interpolation
    void setPool(QThreadPool *pool);
    void setProviders(const QVector<SearchProvider*> &providers);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &idx, int role) const override;

    // Selection (LAUN-05): clamped to [0, rowCount-1]; delta ±1 for ↑/↓,
    // ±kVisibleRows (7) for PageUp/PageDown; Home=0 / End=count-1.
    Q_INVOKABLE int selectedIndex() const;
    Q_INVOKABLE void moveSelection(int delta);
    Q_INVOKABLE void selectIndex(int index);

    // D-12 freeze seed: value copy of the entry at the CURRENT selection
    // (03-04's launchSelected() calls this at keypress).
    AppEntry snapshotSelected() const;

    // ── 05.1 curation surface (CUR-02/CUR-03/CUR-04) ──
    // Hide/unhide the SELECTED row: app and manual-pick rows (File → no-op,
    // CUR-04 escape-hatch guard — only TRANSIENT index file rows are
    // never-hideable; fromAdded rows are curated like apps, 2026-08-12).
    // hideSelected MARKS the entry hidden (hidden=true) — it stays in
    // m_entries/m_addedEntries so hiddenCount()/Show-hidden/Unhide work
    // in-session; unhideSelected flips it back. Live marking with the query
    // PRESERVED — NEVER a catalog rebuild / setEntries (D-08 query reset,
    // research Pitfall 3).
    Q_INVOKABLE void hideSelected();
    Q_INVOKABLE void unhideSelected();   // writes the shown override via the seam
    Q_INVOKABLE void setShowHidden(bool on); // rebuilds display order (buildAppOrder branch)
    int hiddenCount() const;                 // rule- AND user-hidden together
    bool showHidden() const;
    // Persistence seam — production binds CurationStore (main.cpp 05.1-04);
    // tests inject spies. UI thread only (SettingsStore precedent).
    using HideStore = std::function<void(const QString &id, bool hidden)>;
    void setHideStore(HideStore fn);
    // Curation RE-READ seam — the counterpart to setHideStore above. setHideStore
    // only writes; this reads the persisted hidden ids back so every incoming
    // result batch can be stamped (see stampCurationHidden).
    //
    // Without it, a removal only lived in the model's in-memory `hidden` flags,
    // which every result batch overwrites: a typed query rebuilt its rows from
    // the providers (hidden=false), and a directory rescan did the same — so the
    // row the user just removed came straight back, which is what "it just goes
    // to hidden, I still see the app" was. Persistence was already there; nothing
    // ever re-read it for index rows.
    //
    // Read once per batch (not per row) — the store is a small INI-backed set and
    // this runs on the query hot path.
    using HiddenSource = std::function<QSet<QString>()>;
    void setHiddenSource(HiddenSource fn);
    // ── Removed (2026-10-08) ───────────────────────────────────────────────
    // A removal is NOT a hide. hideSelected/hidePath keep the row reachable via
    // "Show hidden (N)" on purpose (CUR-03); the user asked for a removed
    // duplicate to leave the app entirely, so removal gets its own axis:
    //   - rows are filtered out UNCONDITIONALLY, including in show-hidden mode;
    //   - they never reach hiddenCount(), so the footer never offers to restore
    //     them and toggling Show hidden cannot bring one back;
    //   - nothing is deleted — the file stays on disk, and a rescan of its
    //     directory still honours the removal.
    using RemovedSource = std::function<QSet<QString>()>;
    void setRemovedSource(RemovedSource fn);
    void setRemoveStore(std::function<void(const QString &, bool)> fn);
    // Remove one entry by path — what the duplicates panel's Remove calls.
    // Returns false only for an empty path.
    Q_INVOKABLE bool removePath(const QString &path);
    // True when the entry is removed — filtered before the hidden check, so it
    // outranks show-hidden mode.
    bool isRemoved(const AppEntry &e) const;
    // Refresh the cached removed ids. Cheap (one store read) and only re-runs
    // when the order may have changed.
    void refreshRemoved() const;
    // Stamp `hidden` onto a freshly arrived batch from the curation store.
    // Never CLEARS a flag - manual picks arrive pre-stamped by
    // fileSearch.setAddedSource, and an incoming false must not undo that.
    void stampCurationHidden(QVector<AppEntry> &entries);
    void stampCurationHidden(QVector<ScoredEntry> &rows);

    // ── 2026-08-15 favorites surface ("Favorites" tab) ──
    // Favorites are tracked as a SET of ids (targetPath/aumid — same identity
    // contract as curation) held in m_favoriteIds, NOT an AppEntry flag: in
    // the 07-06-pivoted product rows are File/added entries rebuilt per query,
    // so membership-by-id survives rebuilds for ANY row type (file rows too —
    // favorites are a positive marker, unlike curation's escape-hatch guard,
    // so nothing is excluded). The star renders via IsFavoriteRole; the
    // favorites-only mode filters m_order to favorite rows. favoriteSelected/
    // unfavoriteSelected toggle the SELECTED row and persist via the seam,
    // preserving the active query (never a setEntries reset).
    Q_INVOKABLE void favoriteSelected();   // mark the selected row favorite (persist true)
    Q_INVOKABLE void unfavoriteSelected(); // unmark the selected row (persist false)
    // 2026-09-15: rebuild the view after a favorite/unfavorite mutation. Routes
    // through the provider fan-out when providers are wired (production), else
    // the legacy synchronous builders — see the .cpp for why calling those
    // directly emptied the user's query results.
    void rebuildAfterMutation();
    // 2026-09-15: re-emit IsFavoriteRole for every row sharing the selected
    // row's id, so the hover star repaints after a toggle without a full reset.
    void emitFavoriteChangedForCurrent();
    // 2026-10-08: hide one entry by path (duplicates panel "Remove").
    // Reversible — marks hidden + persists through the hide store, exactly
    // like hideSelected; nothing is deleted from disk.
    Q_INVOKABLE bool hidePath(const QString &path);
    // ── Duplicate detection (2026-10-08) ────────────────────────────────
    // "display name + containing folder name", both case-folded. Empty when the
    // entry can't be keyed (no path), which excludes synthetic rows.
    static QString duplicateKeyOf(const AppEntry &e);
    // True when the path sits under a Startup folder — an autostart item, which
    // is excluded from duplicate analysis (see duplicateKeyOf).
    static bool isAutostartPath(const QString &path);
    // Rebuild m_duplicateRowKeys / m_duplicateGroups from the CURRENT m_order.
    // Call after anything that changes which rows are visible.
    void recomputeDuplicates();
    // Authoritative invalidation hook, connected to modelReset / rowsInserted /
    // rowsRemoved / layoutChanged. See the constructor comment for why the
    // per-mutator trailing calls are not sufficient on their own.
    void onOrderChanged();
    // Lazy recompute, safe from const readers (data()). Returns true if the
    // duplicate set/count actually changed.
    bool refreshDuplicates() const;
    int duplicateGroupCount() const { refreshDuplicates(); return m_duplicateGroups.size(); }
    QVariantList duplicateGroups() const;
    // Row indices (into m_order) whose entry belongs to a duplicate group —
    // backs the IsDuplicateRole badge.
    bool isDuplicateRow(int row) const;
    bool favoritesOnly() const;
    Q_INVOKABLE void setFavoritesOnly(bool on); // "All | Favorites" tab toggle
    int favoriteCount() const; // 2026-08-15: m_favoriteIds.size() — persisted favorites
    // Persistence seam — production binds FavoritesStore (main.cpp);
    // tests inject spies. UI thread only (FavoritesStore precedent).
    using FavoriteStore = std::function<void(const QString &id, bool favorite)>;
    void setFavoriteStore(FavoriteStore fn);
    void setFavoriteIds(const QSet<QString> &ids); // seed persisted favorites at startup

    // ── Frecency (count + recency) — LaunchHistory-backed ──
    using FrecencyFn = std::function<int(const QString &id)>; // legacy per-id
    using FrecencyMapFn = std::function<QHash<QString, int>()>; // batched (hot path)
    void setFrecencyFn(FrecencyFn fn);
    void setFrecencyMapFn(FrecencyMapFn fn);
    QString calculatorResult() const;

    // ── Phase-4 file results (04-04): D-01..D-07, D-14, D-15 ──
    // setFileResults(generation, query, files): UI-thread delivery from the
    // file-search coordinator (04-05 wiring). Stores the latest generation,
    // DROPS older ones (D-15 defense in depth — FileSearch also drops).
    // WR-03: results computed for query text other than the CURRENT m_query
    // are dropped too — the generation proves recency, the text proves
    // relevance (a stale-text result can carry the current generation when it
    // lands inside the debounce window). Re-merges immediately when a query
    // is active; an EMPTY-query delivery is the 07-06 default-list snapshot
    // (index .exe rows + manual picks, deduped upstream in FileSearch) — it
    // refills the m_addedEntries channel and rebuilds the default list
    // alphabetically, so scanned executables appear the instant a scan lands.
    // The call must arrive on the UI thread (documented contract — the
    // watcher completion in FileSearch guarantees it).
    void setFileResults(quint64 generation, const QString &query, QVector<AppEntry> files);

signals:
    void queryChanged(const QString &query);
    // Emitted when the clamped selection actually changes — the QML ListView
    // binding (`currentIndex: resultsModel.selectedIndex`) depends on it.
    void selectionChanged();
    void showHiddenChanged();
    void hiddenCountChanged();  // 05.1: QML footer "Show hidden (N)" visibility
    void favoritesOnlyChanged(); // 2026-08-15: "Favorites" tab toggle
    void calculatorResultChanged();
    void duplicatesChanged(); // 2026-10-08: duplicate set/count changed (footer chip + badges)

private:
    // Display row: resolved by data()/snapshotSelected() against m_entries
    // (fromFiles == fromAdded == false), m_fileEntries (fromFiles == true),
    // m_addedEntries (fromAdded == true — the D-14 default-list channel), or
    // m_providerEntries (fromProvider == true — Phase-10 provider results,
    // each carrying its own AppEntry). Calculator rows are ephemeral synthetic
    // entries (isCalculator == true).
    struct Row { int entryIndex; bool fromFiles; bool fromAdded = false; bool fromProvider = false; bool isCalculator = false; };
    const AppEntry &entryAt(const Row &row) const;

    // App-only filter+rank (the 03-05 loop verbatim) filling m_order/m_ranges;
    // setQuery's empty branch calls it for the full alphabetical list (D-14).
    void buildAppOrder();
    // Merges m_fileEntries into m_order/m_ranges by score desc then displayName
    // asc (D-01/D-05), caps file rows at kMaxFileRows (D-03), and applies the
    // kPathMatchScore base tier for path-only matches (D-07).
    void mergeFiles();
    // 2026-09-15 perf: the cached case-folded displayName for a row (mirrors
    // entryAt's four origins). Falls back to folding on the fly if a channel's
    // cache is somehow missing, so ordering can never be wrong — and can never
    // index out of range — if a fill site is added later.
    QString foldedNameFor(const Row &row) const;
    // 2026-08-15: favorites mode — true if the row's id is in m_favoriteIds.
    bool isFavoriteRow(const Row &row) const;
    // 2026-08-15: when m_favoritesOnly, prunes m_order/m_ranges to favorite
    // rows (no-op otherwise). Called at the end of every order-building site
    // (setEntries, buildAppOrder, mergeFiles).
    void filterFavorites();
    // Phase-11 perf fix (2026-09-02): apply the target view (bRows/bRanges —
    // computed by the builders under the NEW favoritesOnly flag) as batched
    // contiguous-run row REMOVES + INSERTS against the CURRENT m_order, instead
    // of a full beginResetModel. Favorites vs All are both stable orderings of
    // the same catalog, so survivors keep their ListView delegates (no
    // rich-text/chip/elide re-run, no icon re-load). When the change is NOT
    // localized (the two tabs differ by a scattered majority of rows → the
    // delta would emit hundreds of tiny signal batches, each forcing a QML
    // layout pass), the WHOLESALE tab switch falls back to a single reset —
    // one signal pair / one layout pass beats ~N/2 signal pairs. Key
    // (packed entryIndex + row-origin flags + isCalculator) matches rows
    // across the two orderings; the packed-quint64 form avoids the ~4000
    // QString allocations a string signature cost over a ~1000-row catalog.
    void applyFavoritesDelta(const QVector<Row> &bRows,
                             const QVector<FuzzyMatcher::Result> &bRanges);

    QVector<AppEntry> m_entries;       // always sorted alphabetically (case-insensitive)
    QVector<QString> m_entriesLower;   // lowercased displayName cache for fast scoring
    QVector<QVector<char>> m_entriesBoundaries; // isBoundary per char (precomputed)
    // 2026-09-15 perf: case-folded displayName, parallel to each channel's
    // entries. The alphabetical tie-breaks (D-01/D-05) compared
    // `displayName.toCaseFolded()` INSIDE their comparators, which allocated a
    // fresh QString per comparison — O(N log N) allocations per keystroke, and
    // mergeFiles ran up to three such sorts per query. Caching the fold turns
    // those into plain QString compares. Populated wherever the channel is
    // (re)filled; always the exact `displayName.toCaseFolded()` of the row at
    // the same index, so ordering is byte-identical to the uncached version.
    QVector<QString> m_entriesFolded;
    QVector<QString> m_fileEntriesFolded;
    QVector<QString> m_addedEntriesFolded;
    QVector<AppEntry> m_fileEntries;   // latest accepted file set (D-15 generation-guarded)
    QVector<AppEntry> m_addedEntries;  // D-14 default-list channel: latest accepted
                                       // added-only snapshot (manual picks, CUR-04),
                                       // kept sorted for the buildAppOrder interleave
    // WR-03: MONOTONIC across catalog refreshes — setEntries() clears the file
    // entries but NEVER resets this counter (a reset would let any in-flight
    // generation pass the model-side guard; FileSearch's counter keeps
    // climbing independently).
    quint64 m_fileGeneration = 0;      // D-15 model-side stale guard
    QString m_query;
    QVector<Row> m_order;                 // merged display order (score desc, alpha asc)
    QVector<FuzzyMatcher::Result> m_ranges; // match results aligned with m_order (O(1) in data())
    int m_selected = 0;
    bool m_showHidden = false;   // 05.1: show-hidden mode — reveals dimmed rows for Unhide
    HideStore m_hideStore;       // 05.1: persistence seam (CurationStore in production, spies in tests)
    bool m_favoritesOnly = false;  // 2026-08-15: "Favorites" tab mode
    QSet<QString> m_favoriteIds;   // 2026-08-15: favorite ids (targetPath/aumid) — survives rebuilds
    FavoriteStore m_favoriteStore; // 2026-08-15: persistence seam (FavoritesStore in production, spies in tests)
    FrecencyFn m_frecencyFn;     // legacy per-id (fallback)
    FrecencyMapFn m_frecencyMapFn; // batched — one lock per query (hot path)
    AppEntry m_calcEntry;        // synthetic calculator row (when query is math)
    bool m_hasCalc = false;
    // 2026-10-08: duplicate detection cache, rebuilt by recomputeDuplicates().
    // m_duplicateRowKeys is a SET OF KEYS (not row indices) on purpose: m_order
    // is rebuilt constantly, and keying by identity survives reordering, so the
    // badge stays correct without re-running detection per repaint. Mutable +
    // dirty-flagged so CONST readers (data(), duplicateGroups()) can refresh
    // lazily — that way every order-building site only has to mark it dirty,
    // instead of ~11 of them having to remember to recompute (and the
    // progressive-insert path in applyProviderResult never resets the model at
    // all, so it would have been missed).
    mutable QSet<QString> m_duplicateRowKeys;
    mutable QVector<QVariantMap> m_duplicateGroups; // { name, count, paths: QStringList }
    mutable bool m_duplicatesDirty = true;
    // Armed by a const reader that refreshed and saw a change, since it cannot
    // emit. recomputeDuplicates() (end of each order mutator) fires it.
    mutable bool m_duplicatesNotifyPending = false;
    HiddenSource m_hiddenSource;   // curation re-read side (see setHiddenSource)
    RemovedSource m_removedSource;
    std::function<void(const QString &, bool)> m_removeStore;
    // Cached removed ids. NOT lazily dirty-flagged like the duplicate cache:
    // isRemoved() runs inside the display-order loops for every row, and a store
    // read per row would be absurd. Refreshed once per order build instead —
    // every path that can change the order calls refreshRemoved().
    mutable QSet<QString> m_removedIds;
    mutable bool m_removedLoaded = false;
    // Async scoring — null pool = synchronous (tests)
    struct AppResult {
        quint64 gen = 0;
        QString query;
        QVector<Row> order;
        QVector<FuzzyMatcher::Result> ranges;
        AppEntry calcEntry;
        bool hasCalc = false;
    };
    void applyAppResult(const AppResult &r);
    // Phase-10 provider fan-out: one future per provider, merged on the UI
    // thread. Each ScoredEntry carries its OWN AppEntry (no internal index).
    struct ProviderResult {
        quint64 gen = 0;
        QString query;
        QVector<ScoredEntry> rows;
    };
    void applyProviderResult(const ProviderResult &r);
    void dispatchProviderQuery();
    QFutureWatcher<ProviderResult> m_providerWatcher;
    QVector<ScoredEntry> m_providerRows; // latest accepted provider rows (gen-guarded)
    QVector<SearchProvider*> m_providers;
    QThreadPool *m_pool = nullptr;
    QFutureWatcher<AppResult> m_watcher;
    quint64 m_appGen = 0;
    static constexpr int kVisibleRows = 7;   // 640×400 shell ≈ 7 rows of 44px (UI-SPEC geometry)
    static constexpr int kMaxFileRows = 100; // cap file rows in the merged list
    static constexpr int kMaxDisplayRows = 80; // total rows shown — caps delegate work
    static constexpr int kPathMatchScore = 100; // D-07 base tier below every name match
    static constexpr int kDefaultListCap = 1000; // empty-query default list breadth (FileIndex::kCandidateCap)
};