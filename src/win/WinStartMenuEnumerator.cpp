#include "win/WinStartMenuEnumerator.h"

#include <QDebug>
#include <QDirIterator>
#include <QFileInfo>

#include <iterator> // std::size
#include <optional>

#include <objbase.h>  // CoInitializeEx / CoCreateInstance
#include <shlobj.h>   // SHGetKnownFolderPath, CLSID_ShellLink, IShellLinkW

namespace {

// One-link parse. Returns the AppEntry, or nullopt when the link cannot be
// loaded/resolved (deleted target, garbage file) — those are skipped, never
// fatal (RESEARCH §1 broken-links rule).
std::optional<AppEntry> parseLnk(const QString &lnkPath)
{
    AppEntry entry;
    entry.source = AppEntry::Source::Lnk;

    // Fallback display name: .lnk base name when no description is set.
    const QString baseName = QFileInfo(lnkPath).completeBaseName();

    IShellLinkW *link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link))))
        return std::nullopt;

    IPersistFile *persist = nullptr;
    HRESULT hr = link->QueryInterface(IID_IPersistFile,
                                      reinterpret_cast<void **>(&persist));
    if (FAILED(hr)) {
        link->Release();
        return std::nullopt;
    }

    hr = persist->Load(reinterpret_cast<LPCWSTR>(lnkPath.utf16()), STGM_READ);
    if (FAILED(hr)) {
        persist->Release();
        link->Release();
        return std::nullopt; // broken link (target deleted / garbage file)
    }

    WCHAR target[MAX_PATH] = {};
    hr = link->GetPath(target, MAX_PATH, nullptr, SLGP_UNCPRIORITY);
    if (FAILED(hr) || target[0] == L'\0') {
        persist->Release();
        link->Release();
        return std::nullopt;
    }
    entry.targetPath = QString::fromWCharArray(target);

    // Arguments carried for 03-04's elevated launch (lpParameters) — a
    // runas on a shortcut with args must not lose them (RESEARCH §1).
    WCHAR args[1024] = {};
    if (SUCCEEDED(link->GetArguments(args, int(std::size(args)))))
        entry.arguments = QString::fromWCharArray(args);

    WCHAR desc[1024] = {};
    entry.displayName = baseName;
    if (SUCCEEDED(link->GetDescription(desc, int(std::size(desc))))
        && desc[0] != L'\0')
        entry.displayName = QString::fromWCharArray(desc);

    // iconRef format: "iconPath;index" — Phase 5 splits on the last ';'.
    WCHAR iconPath[MAX_PATH] = {};
    int iconIndex = 0;
    if (SUCCEEDED(link->GetIconLocation(iconPath, MAX_PATH, &iconIndex))
        && iconPath[0] != L'\0')
        entry.iconRef = QString::fromWCharArray(iconPath) + u';'
                        + QString::number(iconIndex);

    persist->Release();
    link->Release();
    return entry;
}

} // namespace

namespace WinStartMenuEnumerator {

QVector<AppEntry> scanStartMenu()
{
    // Both folders are mandatory (RESEARCH §1): skipping CommonPrograms
    // misses machine-wide installs.
    const KNOWNFOLDERID *folderIds[] = { &FOLDERID_Programs,
                                         &FOLDERID_CommonPrograms };
    QStringList roots;
    for (const KNOWNFOLDERID *id : folderIds) {
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(*id, KF_FLAG_DEFAULT, nullptr, &path))
            && path != nullptr) {
            roots << QString::fromWCharArray(path);
            CoTaskMemFree(path);
        }
    }
    return scanRoots(roots);
}

QVector<AppEntry> scanRoots(const QStringList &rootDirs)
{
    QVector<AppEntry> entries;

    // COM apartment on this thread: reuse an existing apartment
    // (S_FALSE = already initialized; RPC_E_CHANGED_MODE = different mode —
    // both are fine to continue with), take ownership only when WE
    // initialize it here (PITFALLS #3: same thread creates + uses COM).
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
        qWarning() << "WinStartMenuEnumerator: CoInitializeEx failed" << initHr;
        return entries;
    }
    const bool weInitialized = (initHr == S_OK);

    for (const QString &root : rootDirs) {
        if (root.isEmpty())
            continue;
        // NOTE: no QDir::NoSymLinks here — Qt treats Windows .lnk files as
        // "links" for that filter and silently hides every shortcut (caught
        // by tst_enum::brokenLinkPolicy, see 03-02-SUMMARY deviations).
        QDirIterator it(root, { QStringLiteral("*.lnk") }, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString path = it.filePath();
            try {
                if (const auto entry = parseLnk(path))
                    entries.push_back(*entry);
                else
                    qWarning() << "skipping broken shortcut" << path;
            } catch (...) {
                // One malformed link must never abort the batch.
                qWarning() << "skipping unparseable shortcut" << path;
            }
        }
    }

    if (weInitialized)
        CoUninitialize();
    return entries;
}

} // namespace WinStartMenuEnumerator