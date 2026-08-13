#pragma once
#include <QString>
#include <QVector>

// Windows Search (SystemIndex) query firewall — raw OLE DB COM (SDK headers
// only; ATL verified absent on this toolchain). All Win32/COM detail lives in
// the .cpp. Must be called on a COM-initialized worker thread (FileSearch
// coordinator — CoInitializeEx MTA per batch, PITFALLS #3 discipline).
namespace WinSearchQuery {

struct FileResult {
    QString path;          // System.ItemPathDisplay (local path — subtitle + launch + reveal)
    QString displayName;   // System.ItemNameDisplay (filename — title)
    bool isFolder = false; // System.IsFolder (D-04 folder rows)
};

enum class IndexerState { Ok, Disabled, Building, Unavailable }; // D-17 locked mapping

// Live OLE DB walk: query → rows. Fresh ISearchQueryHelper per query (STACK
// locked), put_QueryMaxResults(30), WHERE restriction (file:% + .exe/folder),
// post-filter gate. Returns up to 30 candidate rows. *ok (may be nullptr):
// false when an HRESULT at/after catalog acquisition failed (RESEARCH §2
// Unavailable path — the coordinator maps it to IndexerState::Unavailable).
QVector<FileResult> queryFiles(const QString &query, bool *ok = nullptr);

// Live indexer status probe (D-16 on-query): GetCatalogStatus → classifyCatalogStatus.
IndexerState checkIndexStatus();

// ── Pure helpers (unit-tested in tst_search; the live COM paths call these) ──
// Map CatalogStatus enum value + catalog-availability to IndexerState (RESEARCH §2 table):
//   catalog unavailable (GetCatalog/CoCreateInstance failed) → Disabled
//   FULL_CRAWL/INCREMENTAL_CRAWL/PROCESSING_NOTIFICATIONS/RECOVERING → Building
//   SHUTTING_DOWN → Unavailable ; IDLE/PAUSED → Ok
IndexerState classifyCatalogStatus(long catalogStatus, bool catalogAvailable);

// D-09 post-filter correctness gate: keep folders, keep case-insensitive .exe paths.
bool isAllowedResult(const QString &path, bool isFolder);

// PITFALLS #5 scope restriction + D-09 source-level filter — the SQL fragment
// passed to put_QueryWhereRestrictions (ANDed by the helper). Exact string locked:
//   System.ItemUrl LIKE 'file:%' AND (System.FileExtension='.exe' OR System.IsFolder=TRUE)
QString buildWhereRestriction();

} // namespace WinSearchQuery
