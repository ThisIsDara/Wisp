#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QVector>

#include <functional>

#include "core/AppEntry.h"
#include "core/FuzzyMatcher.h"

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
    bool favoritesOnly() const;
    Q_INVOKABLE void setFavoritesOnly(bool on); // "All | Favorites" tab toggle
    int favoriteCount() const; // 2026-08-15: m_favoriteIds.size() — persisted favorites
    // Persistence seam — production binds FavoritesStore (main.cpp);
    // tests inject spies. UI thread only (FavoritesStore precedent).
    using FavoriteStore = std::function<void(const QString &id, bool favorite)>;
    void setFavoriteStore(FavoriteStore fn);
    void setFavoriteIds(const QSet<QString> &ids); // seed persisted favorites at startup

    // ── Frecency (count + recency) — LaunchHistory-backed ──
    using FrecencyFn = std::function<int(const QString &id)>;
    void setFrecencyFn(FrecencyFn fn);
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

private:
    // Display row: resolved by data()/snapshotSelected() against m_entries
    // (fromFiles == fromAdded == false), m_fileEntries (fromFiles == true),
    // or m_addedEntries (fromAdded == true — the D-14 default-list channel).
    // Calculator rows are ephemeral synthetic entries (isCalculator == true).
    struct Row { int entryIndex; bool fromFiles; bool fromAdded = false; bool isCalculator = false; };
    const AppEntry &entryAt(const Row &row) const;

    // App-only filter+rank (the 03-05 loop verbatim) filling m_order/m_ranges;
    // setQuery's empty branch calls it for the full alphabetical list (D-14).
    void buildAppOrder();
    // Merges m_fileEntries into m_order/m_ranges by score desc then displayName
    // asc (D-01/D-05), caps file rows at kMaxFileRows (D-03), and applies the
    // kPathMatchScore base tier for path-only matches (D-07).
    void mergeFiles();
    // 2026-08-15: favorites mode — true if the row's id is in m_favoriteIds.
    bool isFavoriteRow(const Row &row) const;
    // 2026-08-15: when m_favoritesOnly, prunes m_order/m_ranges to favorite
    // rows (no-op otherwise). Called at the end of every order-building site
    // (setEntries, buildAppOrder, mergeFiles).
    void filterFavorites();

    QVector<AppEntry> m_entries;       // always sorted alphabetically (case-insensitive)
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
    FrecencyFn m_frecencyFn;     // frecency boost — LaunchHistory-backed
    AppEntry m_calcEntry;        // synthetic calculator row (when query is math)
    bool m_hasCalc = false;
    static constexpr int kVisibleRows = 7;   // 640×400 shell ≈ 7 rows of 44px (UI-SPEC geometry)
    static constexpr int kMaxFileRows = 1000; // 07-06: file rows ARE the list; the index
                                             // pipeline caps candidates at 1000 upstream,
                                             // so this cap is effectively off
    static constexpr int kPathMatchScore = 100; // D-07 base tier below every name match
};