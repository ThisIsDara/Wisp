#include "win/WinLaunch.h"

#include <QDir>
#include <QFileInfo>

#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h> // IApplicationActivationManager (+ class uuid)

namespace {

// CLSID_ApplicationActivationManager {45BA127D-10A8-46EA-8AB7-56EA9078943C}
// Declared EXTERN_C in ShObjIdl_core.h but its GUID value lives in uuid.lib;
// the local constant keeps this TU self-contained (value verified against
// the 10.0.26100.0 header's DECLSPEC_UUID).
const CLSID kClsidApplicationActivationManager = {
    0x45ba127d, 0x10a8, 0x46ea, {0x8a, 0xb7, 0x56, 0xea, 0x90, 0x78, 0x94, 0x3c}
};

} // namespace

namespace WinLaunch {

LaunchResult launchClassic(const AppEntry &entry, bool elevated)
{
    // D-11/D-13 contract: launch ONLY the resolved target; no target → Failed.
    if (entry.targetPath.isEmpty())
        return LaunchResult::Failed;

    // Keep the wide buffers alive until after ShellExecuteExW returns — the
    // SHELLEXECUTEINFOW pointers must not dangle (they are consumed during
    // the call only, so locals are fine).
    const std::wstring fileW = QDir::toNativeSeparators(entry.targetPath).toStdWString();
    const std::wstring dirW =
        QDir::toNativeSeparators(QFileInfo(entry.targetPath).absolutePath()).toStdWString();
    const std::wstring argsW = entry.arguments.toStdWString();

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    // NOCLOSEPROCESS: receive the process handle to close (no wait — D-13).
    // FLAG_NO_UI: ShellExecuteEx must not pop its own error dialog; the
    // controller classifies and stays silent (D-11).
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = elevated ? L"runas" : L"open";
    sei.lpFile = fileW.c_str();
    // RESEARCH §1: carry the .lnk arguments into elevation (empty → nullptr).
    sei.lpParameters = entry.arguments.isEmpty() ? nullptr : argsW.c_str();
    sei.lpDirectory = dirW.c_str(); // PITFALLS #13: target's parent, never the launcher's cwd
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        const DWORD err = GetLastError();
        if (err == ERROR_CANCELLED || err == SE_ERR_ACCESSDENIED) {
            // D-11: user cancelled the UAC prompt — quiet no-op, no signals,
            // no UI, launcher stays open for the next attempt.
            qInfo("WinLaunch: UAC cancelled by user for '%s' (err=%lu)",
                  qUtf8Printable(entry.displayName), ulong(err));
            return LaunchResult::CancelledByUser;
        }
        qWarning("WinLaunch: ShellExecuteEx '%s' failed (err=%lu)",
                 qUtf8Printable(entry.displayName), ulong(err));
        return LaunchResult::Failed;
    }

    if (sei.hProcess && sei.hProcess != INVALID_HANDLE_VALUE)
        CloseHandle(sei.hProcess); // no wait — launch returns immediately (D-13)
    return LaunchResult::Launched;
}

LaunchResult launchUwp(const AppEntry &entry)
{
    if (entry.aumid.isEmpty())
        return LaunchResult::Failed;

    IApplicationActivationManager *pActivator = nullptr;
    HRESULT hr = CoCreateInstance(kClsidApplicationActivationManager, nullptr,
                                  CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&pActivator));
    if (FAILED(hr) || !pActivator) {
        qWarning("WinLaunch: CoCreateInstance(ApplicationActivationManager) failed (hr=0x%08lx)",
                 ulong(hr));
        return LaunchResult::Failed;
    }

    const std::wstring aumidW = entry.aumid.toStdWString();
    DWORD pid = 0;
    hr = pActivator->ActivateApplication(aumidW.c_str(), nullptr, AO_NONE, &pid);
    pActivator->Release();

    if (FAILED(hr)) {
        // RESEARCH unknowns: suspended / not-installed-for-user → normal
        // failure, surfaced as launchFailed by the controller, never a crash.
        qWarning("WinLaunch: ActivateApplication('%s') failed (hr=0x%08lx)",
                 qUtf8Printable(entry.aumid), ulong(hr));
        return LaunchResult::Failed;
    }
    return LaunchResult::Launched;
}

LaunchResult revealInExplorer(const QString &path)
{
    // LAUN-03: no path → nothing to reveal. (T-04-07: the path never reaches
    // explorer.exe unnormalized — see below.)
    if (path.isEmpty())
        return LaunchResult::Failed;

    // T-04-07 (injection): the path is native-normalized (a leading '/' can
    // never parse as a switch) and quoted INSIDE the argument (spaces can
    // never split it). The whole thing is one SHELLEXECUTEINFOW string to
    // explorer.exe's fixed command line — no shell/CMD is ever involved.
    const std::wstring fileW = QDir::toNativeSeparators(path).toStdWString();
    const std::wstring argsW = L"/select,\"" + fileW + L"\"";

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    // FLAG_NO_UI: no error dialog — the controller classifies and stays
    // silent (same discipline as launchClassic). No NOCLOSEPROCESS: the
    // reveal is fire-and-forget (D-13 instant path, no handle to close).
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = L"explorer.exe"; // system binary — never the launcher's cwd (lpDirectory = nullptr)
    sei.lpParameters = argsW.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        const DWORD err = GetLastError();
        if (err == ERROR_CANCELLED || err == SE_ERR_ACCESSDENIED) {
            // Quiet — same classification as a cancelled UAC prompt.
            qInfo("WinLaunch: Explorer reveal cancelled by user for '%s' (err=%lu)",
                  qUtf8Printable(path), ulong(err));
            return LaunchResult::CancelledByUser;
        }
        qWarning("WinLaunch: ShellExecuteEx(explorer /select) failed for '%s' (err=%lu)",
                 qUtf8Printable(path), ulong(err));
        return LaunchResult::Failed;
    }
    return LaunchResult::Launched;
}

} // namespace WinLaunch