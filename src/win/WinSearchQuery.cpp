#include "win/WinSearchQuery.h"

#include <QDebug>

#include <windows.h>
#include <oledb.h>     // raw OLE DB interfaces: DBBINDING, IRowset, IAccessor, ...
#include <msdasc.h>    // OLE DB service component ProgIDs (header-only)
#include <searchapi.h> // ISearchManager / ISearchCatalogManager / ISearchQueryHelper

#include <cstring> // memcpy
#include <cwchar>  // _wcsicmp (column-name ordinal resolution)
#include <string>
#include <vector>

namespace {

// Minimal COM ownership guard. The OLE DB chain spans ~10 interfaces with a
// failure exit at every step; RAII is the only release discipline that
// guarantees "release everything on every path" (plan T-04-02 / RESEARCH §1
// step 7). Matches the WSOleDB sample's CComPtr role with zero ATL.
template <typename T>
class ComPtr
{
public:
    ComPtr() = default;
    ~ComPtr() { release(); }
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;

    T **put() noexcept { return &m_ptr; }
    T *get() const noexcept { return m_ptr; }
    T *operator->() const noexcept { return m_ptr; }
    explicit operator bool() const noexcept { return m_ptr != nullptr; }

private:
    void release() noexcept
    {
        if (m_ptr) {
            m_ptr->Release();
            m_ptr = nullptr;
        }
    }
    T *m_ptr = nullptr;
};

// BSTR allocated with CoTaskMemAlloc (DWORD length prefix + data + NUL).
// SysAllocString lives in oleaut32.dll; building the layout ourselves keeps
// the link line at "ole32 already linked" (plan: no new link libs). Freed
// with CoTaskMemFree(b) — the prefix is addressable via offsetof arithmetic
// below.
BSTR makeBstr(const std::wstring &s)
{
    const DWORD bytes = DWORD(s.size() * sizeof(wchar_t));
    BYTE *raw = static_cast<BYTE *>(CoTaskMemAlloc(sizeof(DWORD) + bytes + sizeof(wchar_t)));
    if (!raw)
        return nullptr;
    *reinterpret_cast<DWORD *>(raw) = bytes;
    if (bytes)
        memcpy(raw + sizeof(DWORD), s.data(), bytes);
    *reinterpret_cast<wchar_t *>(raw + sizeof(DWORD) + bytes) = L'\0';
    return reinterpret_cast<BSTR>(raw + sizeof(DWORD));
}

void freeBstr(BSTR b)
{
    if (b)
        CoTaskMemFree(reinterpret_cast<BYTE *>(b) - sizeof(DWORD));
}

// get_ConnectionString() includes "provider=Search.CollatorDSO.1;..." — the
// token names this same provider and must not be re-set as a property
// (RESEARCH §1 step 3). Token-wise strip, case-insensitive.
std::wstring stripProviderToken(const std::wstring &raw)
{
    std::wstring result;
    size_t pos = 0;
    while (pos <= raw.size()) {
        const size_t end = raw.find(L';', pos);
        const std::wstring token =
            raw.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
        const bool isProvider =
            token.size() >= 9 && _wcsnicmp(token.c_str(), L"provider=", 9) == 0;
        if (!isProvider) {
            if (!result.empty())
                result += L';';
            result += token;
        }
        if (end == std::wstring::npos)
            break;
        pos = end + 1;
    }
    return result;
}

// Row-buffer layout for the three selected columns. EMPIRICAL (verified live
// 2026-08-10 on Win11): the provider rejects DBBINDINGs whose value/length
// offsets are not 16-byte aligned and status offsets not 8-byte aligned
// (DBBINDSTATUS_BADBINDINFO on every binding otherwise) — the compact
// 4-aligned WSOleDB-sample layout fails. Layout below is the verified-good
// one: value | ULONG length | ULONG status per column, 16/8-aligned.
struct RowLayout {
    static constexpr ULONG kPathMax = 4096; // System.ItemPathDisplay — long paths
    static constexpr ULONG kNameMax = 1024; // System.ItemNameDisplay
    static constexpr ULONG kFolderMax = 128; // System.IsFolder (WSTR "True"/"False")

    ULONG pathValue = 0;
    ULONG pathLength = 0;
    ULONG pathStatus = 0;
    ULONG nameValue = 0;
    ULONG nameLength = 0;
    ULONG nameStatus = 0;
    ULONG folderValue = 0;
    ULONG folderLength = 0;
    ULONG folderStatus = 0;
    ULONG rowSize = 0;

    void init() noexcept
    {
        pathValue = 0;
        pathLength = pathValue + kPathMax; // 4096 — 16-aligned
        pathStatus = pathLength + sizeof(ULONG) + 4; // 4104 — 8-aligned
        nameValue = pathStatus + sizeof(ULONG) + 4; // 4112 — 16-aligned
        nameLength = nameValue + kNameMax; // 5136 — 16-aligned
        nameStatus = nameLength + sizeof(ULONG) + 4; // 5144 — 8-aligned
        folderValue = nameStatus + sizeof(ULONG) + 4; // 5152 — 16-aligned
        folderLength = folderValue + kFolderMax; // 5280 — 16-aligned
        folderStatus = folderLength + sizeof(ULONG) + 4; // 5288 — 8-aligned
        rowSize = folderStatus + sizeof(ULONG) + 4; // 5296 — 16-aligned
    }
};

// Resolve the selected-column ordinals from the rowset's column info. The
// provider column list can vary by dialect — names are the stable contract
// (RESEARCH §1: "System.ItemPathDisplay, System.ItemNameDisplay,
// System.IsFolder").
struct Ordinals {
    DBORDINAL path = 0;
    DBORDINAL name = 0;
    DBORDINAL folder = 0;
    bool complete() const noexcept
    {
        return path != 0 && name != 0 && folder != 0;
    }
};

Ordinals resolveOrdinals(IColumnsInfo *columns, DBORDINAL count, DBCOLUMNINFO *infos)
{
    Ordinals ord;
    for (DBORDINAL i = 0; i < count; ++i) {
        if (!infos[i].pwszName)
            continue;
        if (_wcsicmp(infos[i].pwszName, L"System.ItemPathDisplay") == 0)
            ord.path = infos[i].iOrdinal;
        else if (_wcsicmp(infos[i].pwszName, L"System.ItemNameDisplay") == 0)
            ord.name = infos[i].iOrdinal;
        else if (_wcsicmp(infos[i].pwszName, L"System.IsFolder") == 0)
            ord.folder = infos[i].iOrdinal;
    }
    return ord;
}

// The raw OLE DB walk (RESEARCH §1 steps 2-7). SQL comes exclusively from
// ISearchQueryHelper::GenerateSQLFromUserQuery (T-04-01: the helper owns AQS
// escaping — never hand-concatenated). On any HRESULT failure: qWarning (never
// a crash), release what exists, return the rows fetched so far.
bool runOleDbQuery(const std::wstring &sql, const std::wstring &connectionString,
                   QVector<WinSearchQuery::FileResult> &results)
{
    CLSID clsid = {};
    if (FAILED(CLSIDFromProgID(L"Search.CollatorDSO.1", &clsid))) {
        qWarning("WinSearchQuery: CLSIDFromProgID(Search.CollatorDSO.1) failed");
        return false;
    }

    ComPtr<IDBInitialize> init;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(init.put()));
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: CoCreateInstance(Search.CollatorDSO.1) failed (hr=0x%08lx)",
                 ulong(hr));
        return false;
    }

    // DBPROP_INIT_DATASOURCE = the helper's connection string (provider token
    // stripped — it names this same provider); DBPROP_INIT_PROMPT = NOPROMPT
    // (a search provider must never pop UI on a worker thread).
    ComPtr<IDBProperties> props;
    hr = init->QueryInterface(IID_PPV_ARGS(props.put()));
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: QI IDBProperties failed (hr=0x%08lx)", ulong(hr));
        return false;
    }

    DBPROP dbProps[2] = {};
    dbProps[0].dwPropertyID = DBPROP_INIT_DATASOURCE;
    dbProps[0].dwOptions = DBPROPOPTIONS_REQUIRED;
    dbProps[0].vValue.vt = VT_BSTR;
    BSTR dsBstr = makeBstr(connectionString);
    dbProps[0].vValue.bstrVal = dsBstr;
    dbProps[1].dwPropertyID = DBPROP_INIT_PROMPT;
    dbProps[1].dwOptions = DBPROPOPTIONS_REQUIRED;
    dbProps[1].vValue.vt = VT_I2;
    dbProps[1].vValue.iVal = DBPROMPT_NOPROMPT;

    DBPROPSET propSet = {};
    propSet.guidPropertySet = DBPROPSET_DBINIT;
    propSet.cProperties = 2;
    propSet.rgProperties = dbProps;

    hr = props->SetProperties(1, &propSet);
    freeBstr(dsBstr);
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: IDBProperties::SetProperties failed (hr=0x%08lx)", ulong(hr));
        return false;
    }

    hr = init->Initialize();
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: IDBInitialize::Initialize failed (hr=0x%08lx)", ulong(hr));
        return false;
    }

    ComPtr<IDBCreateSession> session;
    hr = init->QueryInterface(IID_PPV_ARGS(session.put()));
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: QI IDBCreateSession failed (hr=0x%08lx)", ulong(hr));
        return false;
    }

    ComPtr<IDBCreateCommand> createCmd;
    hr = session->CreateSession(nullptr, __uuidof(IDBCreateCommand),
                                reinterpret_cast<IUnknown **>(createCmd.put()));
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: CreateSession failed (hr=0x%08lx)", ulong(hr));
        return false;
    }

    ComPtr<ICommandText> text;
    hr = createCmd->CreateCommand(nullptr, __uuidof(ICommandText),
                                  reinterpret_cast<IUnknown **>(text.put()));
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: CreateCommand failed (hr=0x%08lx)", ulong(hr));
        return false;
    }

    hr = text->SetCommandText(DBGUID_DEFAULT, sql.c_str());
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: SetCommandText failed (hr=0x%08lx)", ulong(hr));
        return false;
    }

    ComPtr<ICommand> cmd;
    hr = text->QueryInterface(IID_PPV_ARGS(cmd.put()));
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: QI ICommand failed (hr=0x%08lx)", ulong(hr));
        return false;
    }

    ComPtr<IRowset> rowset;
    hr = cmd->Execute(nullptr, __uuidof(IRowset), nullptr, nullptr,
                      reinterpret_cast<IUnknown **>(rowset.put()));
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: ICommand::Execute failed (hr=0x%08lx)", ulong(hr));
        return false;
    }

    // Column ordinals → accessor (one-time per query, bind-on-demand).
    ComPtr<IColumnsInfo> columns;
    hr = rowset->QueryInterface(IID_PPV_ARGS(columns.put()));
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: QI IColumnsInfo failed (hr=0x%08lx)", ulong(hr));
        return false;
    }
    DBORDINAL colCount = 0;
    DBCOLUMNINFO *colInfos = nullptr;
    OLECHAR *stringsBuffer = nullptr;
    hr = columns->GetColumnInfo(&colCount, &colInfos, &stringsBuffer);
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: GetColumnInfo failed (hr=0x%08lx)", ulong(hr));
        return false;
    }
    const Ordinals ord = resolveOrdinals(columns.get(), colCount, colInfos);
    CoTaskMemFree(colInfos);
    CoTaskMemFree(stringsBuffer);
    if (!ord.complete()) {
        qWarning("WinSearchQuery: selected column not found in rowset (path=%lu name=%lu folder=%lu)",
                 ulong(ord.path), ulong(ord.name), ulong(ord.folder));
        return false;
    }

    RowLayout layout;
    layout.init();
    DBBINDING binds[3] = {};
    binds[0].iOrdinal = ord.path;
    binds[0].dwPart = DBPART_VALUE | DBPART_LENGTH | DBPART_STATUS;
    binds[0].dwMemOwner = DBMEMOWNER_CLIENTOWNED;
    binds[0].eParamIO = DBPARAMIO_NOTPARAM;
    binds[0].obValue = layout.pathValue;
    binds[0].obLength = layout.pathLength;
    binds[0].obStatus = layout.pathStatus;
    binds[0].cbMaxLen = RowLayout::kPathMax;
    binds[0].wType = DBTYPE_WSTR;
    binds[1].iOrdinal = ord.name;
    binds[1].dwPart = DBPART_VALUE | DBPART_LENGTH | DBPART_STATUS;
    binds[1].dwMemOwner = DBMEMOWNER_CLIENTOWNED;
    binds[1].eParamIO = DBPARAMIO_NOTPARAM;
    binds[1].obValue = layout.nameValue;
    binds[1].obLength = layout.nameLength;
    binds[1].obStatus = layout.nameStatus;
    binds[1].cbMaxLen = RowLayout::kNameMax;
    binds[1].wType = DBTYPE_WSTR;
    binds[2].iOrdinal = ord.folder;
    binds[2].dwPart = DBPART_VALUE | DBPART_LENGTH | DBPART_STATUS;
    binds[2].dwMemOwner = DBMEMOWNER_CLIENTOWNED;
    binds[2].eParamIO = DBPARAMIO_NOTPARAM;
    binds[2].obValue = layout.folderValue;
    binds[2].obLength = layout.folderLength;
    binds[2].obStatus = layout.folderStatus;
    binds[2].cbMaxLen = RowLayout::kFolderMax;
    // DBTYPE_WSTR, NOT DBTYPE_BOOL: the provider exposes System.IsFolder as a
    // string column ("True"/"False" — verified live) and rejects BOOL bindings
    // with DBBINDSTATUS_BADBINDINFO.
    binds[2].wType = DBTYPE_WSTR;

    ComPtr<IAccessor> accessor;
    hr = rowset->QueryInterface(IID_PPV_ARGS(accessor.put()));
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: QI IAccessor failed (hr=0x%08lx)", ulong(hr));
        return false;
    }
    HACCESSOR hAccessor = 0;
    hr = accessor->CreateAccessor(DBACCESSOR_ROWDATA, 3, binds, layout.rowSize,
                                  &hAccessor, nullptr);
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: CreateAccessor failed (hr=0x%08lx)", ulong(hr));
        return false;
    }

    // Row loop: GetNextRows → GetData → build FileResult. WR-02: the row
    // buffer is REUSED across rows, so a row is only read when every column
    // reports DBSTATUS_S_OK (any other status — ISNULL or E_* — means the
    // value slot was never written; reading it would surface stale bytes from
    // the previous row). Length slots bound every string read: the provider
    // writes the terminator only if it fits within cbMaxLen, so an unbounded
    // read on a full slot would run into the following fields and, in the
    // worst case, past the allocation. len >= cbMaxLen = truncation → skip.
    constexpr DBCOUNTITEM kRowsPerFetch = 32;
    std::vector<HROW> hrows(kRowsPerFetch);
    std::vector<BYTE> rowData(layout.rowSize);
    const auto boundedString = [](const wchar_t *chars, int charCount) {
        if (charCount > 0 && chars[charCount - 1] == L'\0')
            --charCount; // length semantics may include the terminator — trim it
        return QString::fromWCharArray(chars, charCount);
    };
    for (;;) {
        HROW *hrowsPtr = hrows.data();
        DBCOUNTITEM obtained = 0;
        hr = rowset->GetNextRows(0, 0, kRowsPerFetch, &obtained, &hrowsPtr);
        if (FAILED(hr)) {
            qWarning("WinSearchQuery: GetNextRows failed (hr=0x%08lx)", ulong(hr));
            break;
        }
        if (obtained == 0)
            break;

        for (DBCOUNTITEM i = 0; i < obtained; ++i) {
            hr = rowset->GetData(hrows[i], hAccessor, rowData.data());
            if (FAILED(hr)) {
                qWarning("WinSearchQuery: GetData failed (hr=0x%08lx)", ulong(hr));
                continue;
            }
            const BYTE *row = rowData.data();
            const DBSTATUS pathStatus =
                *reinterpret_cast<const DBSTATUS *>(row + layout.pathStatus);
            const DBSTATUS nameStatus =
                *reinterpret_cast<const DBSTATUS *>(row + layout.nameStatus);
            const DBSTATUS folderStatus =
                *reinterpret_cast<const DBSTATUS *>(row + layout.folderStatus);
            if (pathStatus != DBSTATUS_S_OK || nameStatus != DBSTATUS_S_OK
                || folderStatus != DBSTATUS_S_OK) {
                continue; // incomplete row — the value slots were never written (WR-02)
            }

            const ULONG pathLen = *reinterpret_cast<const ULONG *>(row + layout.pathLength);
            const ULONG nameLen = *reinterpret_cast<const ULONG *>(row + layout.nameLength);
            const ULONG folderLen = *reinterpret_cast<const ULONG *>(row + layout.folderLength);
            if (pathLen >= RowLayout::kPathMax || nameLen >= RowLayout::kNameMax
                || folderLen >= RowLayout::kFolderMax) {
                continue; // truncated — the slot is full with no terminator (WR-02)
            }

            const auto *pathChars =
                reinterpret_cast<const wchar_t *>(row + layout.pathValue);
            const auto *nameChars =
                reinterpret_cast<const wchar_t *>(row + layout.nameValue);
            const auto *folderChars =
                reinterpret_cast<const wchar_t *>(row + layout.folderValue);
            const QString path =
                boundedString(pathChars, int(pathLen / sizeof(wchar_t)));
            const QString name =
                boundedString(nameChars, int(nameLen / sizeof(wchar_t)));
            // Length-bounded folder compare (WR-02): exactly "true" (case-
            // insensitive), with the optional terminator — never scans past
            // the slot's written chars.
            const int folderCharsCount = int(folderLen / sizeof(wchar_t));
            const bool isFolder =
                (folderCharsCount == 4
                 || (folderCharsCount == 5 && folderChars[4] == L'\0'))
                && _wcsnicmp(folderChars, L"true", 4) == 0;

            // Post-filter gate (authoritative, D-09): folders + case-insensitive .exe.
            if (!WinSearchQuery::isAllowedResult(path, isFolder))
                continue;
            results.push_back(WinSearchQuery::FileResult{ path, name, isFolder });
        }

        rowset->ReleaseRows(obtained, hrows.data(), nullptr, nullptr, nullptr);
        if (obtained < kRowsPerFetch)
            break;
    }

    accessor->ReleaseAccessor(hAccessor, nullptr);
    return true;
}

// The COM-initialized body of queryFiles. Fresh ISearchQueryHelper per query
// (STACK locked — cached helpers go stale as the catalog evolves). Returns
// false when any HRESULT at/after catalog acquisition failed (RESEARCH §2
// Unavailable path — the caller maps it via the *ok out-param).
bool runQuery(const QString &query, QVector<WinSearchQuery::FileResult> &results)
{
    ComPtr<ISearchManager> manager;
    // Combined CLSCTX: some Win11 machines register CSearchManager as an
    // out-of-process server only (empty InprocServer32 stub → REGDB_E_CLASSNOTREG
    // under INPROC-only; verified 2026-08-10 on the dev machine). COM prefers
    // in-proc when registered, falls back to the local server otherwise.
    HRESULT hr = CoCreateInstance(__uuidof(CSearchManager), nullptr,
                                  CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                                  IID_PPV_ARGS(manager.put()));
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: CoCreateInstance(CSearchManager) failed (hr=0x%08lx)",
                 ulong(hr));
        return false;
    }

    ComPtr<ISearchCatalogManager> catalog;
    hr = manager->GetCatalog(L"SystemIndex", catalog.put());
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: GetCatalog(SystemIndex) failed (hr=0x%08lx)", ulong(hr));
        return false;
    }

    ComPtr<ISearchQueryHelper> helper;
    hr = catalog->GetQueryHelper(helper.put());
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: GetQueryHelper failed (hr=0x%08lx)", ulong(hr));
        return false;
    }

    // PITFALLS #5 discipline: SelectColumns + MaxResults(30) + AQS syntax +
    // WHERE restriction. Locale defaults are left alone (keyword-only queries).
    hr = helper->put_QuerySelectColumns(
        L"System.ItemPathDisplay, System.ItemNameDisplay, System.IsFolder");
    if (SUCCEEDED(hr))
        hr = helper->put_QueryMaxResults(30);
    if (SUCCEEDED(hr))
        hr = helper->put_QuerySyntax(SEARCH_ADVANCED_QUERY_SYNTAX);
    if (SUCCEEDED(hr)) {
        // The helper appends the restriction fragment verbatim right after the
        // CONTAINS(...) clause it generates, so the fragment must begin with
        // "AND " — without it the provider rejects the SQL with
        // 0x80040e14 DB_E_ERRORSINCOMMAND (verified live 2026-08-10). The
        // locked buildWhereRestriction() string stays AND-less (T-04-01 grep
        // gate); the prefix is a transport detail, added here only.
        const std::wstring whereW = L"AND " + WinSearchQuery::buildWhereRestriction().toStdWString();
        hr = helper->put_QueryWhereRestrictions(whereW.c_str());
    }
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: helper configuration failed (hr=0x%08lx)", ulong(hr));
        return false;
    }

    // The ONLY SQL source (T-04-01): the helper owns AQS escaping. The query
    // text goes exclusively into this call — never string-concatenated.
    PWSTR sql = nullptr;
    const std::wstring queryW = query.toStdWString();
    hr = helper->GenerateSQLFromUserQuery(queryW.c_str(), &sql);
    if (FAILED(hr)) {
        qWarning("WinSearchQuery: GenerateSQLFromUserQuery failed (hr=0x%08lx)", ulong(hr));
        return false;
    }

    PWSTR connectionString = nullptr;
    hr = helper->get_ConnectionString(&connectionString);
    std::wstring connectionW;
    if (SUCCEEDED(hr) && connectionString) {
        connectionW = stripProviderToken(connectionString);
        CoTaskMemFree(connectionString);
    }
    if (connectionW.empty()) {
        qWarning("WinSearchQuery: get_ConnectionString returned an empty datasource");
        CoTaskMemFree(sql);
        return false;
    }

    const std::wstring sqlW = sql ? sql : L"";
    CoTaskMemFree(sql);
    return runOleDbQuery(sqlW, connectionW, results);
}

} // namespace

namespace WinSearchQuery {

QVector<FileResult> queryFiles(const QString &query, bool *ok)
{
    if (ok)
        *ok = true; // only explicit failures flip it (see below)

    QVector<FileResult> results;

    // D-14: empty query = apps only — the file pipeline never engages.
    if (query.trimmed().isEmpty())
        return results;

    // MTA per batch, reuse an existing apartment (S_FALSE / RPC_E_CHANGED_MODE
    // tolerated — same discipline as AppCatalog/WinStartMenuEnumerator); the
    // thread that creates the COM objects also uses and releases them
    // (T-04-02 / PITFALLS #3).
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        qWarning("WinSearchQuery: CoInitializeEx failed (hr=0x%08lx)", ulong(initHr));
        if (ok)
            *ok = false;
        return results;
    }
    const bool weInitialized = (initHr == S_OK);

    const bool queried = runQuery(query, results);

    if (weInitialized)
        CoUninitialize();

    // RESEARCH §2 Unavailable path: catalog OK but the query walk failed
    // (Execute / row iteration / helper chain) → the coordinator maps it to
    // IndexerState::Unavailable via this flag.
    if (ok)
        *ok = queried;
    return results;
}

IndexerState checkIndexStatus()
{
    // D-16 on-query probe: GetCatalogStatus → classifyCatalogStatus. Any
    // failure before GetCatalogStatus (service not running, catalog missing)
    // → Disabled (RESEARCH §2 mapping).
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        qWarning("WinSearchQuery: CoInitializeEx failed (hr=0x%08lx)", ulong(initHr));
        return IndexerState::Disabled;
    }
    const bool weInitialized = (initHr == S_OK);

    IndexerState state = IndexerState::Disabled;
    ComPtr<ISearchManager> manager;
    if (SUCCEEDED(CoCreateInstance(__uuidof(CSearchManager), nullptr,
                                   CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                                   IID_PPV_ARGS(manager.put())))) {
        ComPtr<ISearchCatalogManager> catalog;
        if (SUCCEEDED(manager->GetCatalog(L"SystemIndex", catalog.put()))) {
            CatalogStatus status = CATALOG_STATUS_IDLE;
            CatalogPausedReason reason = CATALOG_PAUSED_REASON_NONE;
            if (SUCCEEDED(catalog->GetCatalogStatus(&status, &reason)))
                state = classifyCatalogStatus(long(status), true);
        }
    }

    if (weInitialized)
        CoUninitialize();
    return state;
}

IndexerState classifyCatalogStatus(long catalogStatus, bool catalogAvailable)
{
    // RESEARCH §2 locked mapping (D-17). Unknown states default to Ok —
    // queries still answer; a false alarm is worse than a missed notice.
    if (!catalogAvailable)
        return IndexerState::Disabled;
    switch (catalogStatus) {
    case CATALOG_STATUS_FULL_CRAWL:
    case CATALOG_STATUS_INCREMENTAL_CRAWL:
    case CATALOG_STATUS_PROCESSING_NOTIFICATIONS:
    case CATALOG_STATUS_RECOVERING:
        return IndexerState::Building;
    case CATALOG_STATUS_SHUTTING_DOWN:
        return IndexerState::Unavailable;
    case CATALOG_STATUS_IDLE:
    case CATALOG_STATUS_PAUSED:
    default:
        return IndexerState::Ok;
    }
}

bool isAllowedResult(const QString &path, bool isFolder)
{
    // D-09 post-filter gate: folders always kept (D-04); files only when the
    // path is a case-insensitive .exe (broader types deferred — CONTEXT user
    // decision: application launcher, not document finder).
    if (isFolder)
        return true;
    return path.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive);
}

QString buildWhereRestriction()
{
    // PITFALLS #5 scope restriction (file:% — the index covers the whole
    // volume set) + D-09 source-level filter, ANDed by the helper. Compile-
    // time constant — the only "SQL construction" this file contains
    // (T-04-01 grep gate).
    return QStringLiteral(
        "System.ItemUrl LIKE 'file:%' AND (System.FileExtension='.exe' OR System.IsFolder=TRUE)");
}

} // namespace WinSearchQuery
