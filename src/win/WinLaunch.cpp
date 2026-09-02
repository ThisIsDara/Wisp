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

// Single ShellExecuteExW attempt for a classic target. Returns the Win32
// error code on failure (0 = success); on success sets *hOut to the process
// handle (or INVALID_HANDLE_VALUE when none). The wide buffers are locals so
// the SHELLEXECUTEINFOW pointers stay valid for the duration of the call.
static DWORD classicLaunch(const AppEntry &entry, const wchar_t *verb, HANDLE *hOut)
{
    *hOut = INVALID_HANDLE_VALUE;
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
    sei.lpVerb = verb;
    sei.lpFile = fileW.c_str();
    // RESEARCH §1: carry the .lnk arguments into elevation (empty → nullptr).
    sei.lpParameters = entry.arguments.isEmpty() ? nullptr : argsW.c_str();
    sei.lpDirectory = dirW.c_str(); // PITFALLS #13: target's parent, never the launcher's cwd
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        const DWORD err = GetLastError();
        if (sei.hProcess && sei.hProcess != INVALID_HANDLE_VALUE)
            CloseHandle(sei.hProcess); // best-effort cleanup on a false return
        return err;
    }
    *hOut = sei.hProcess;
    return 0;
}

LaunchResult launchClassic(const AppEntry &entry, bool elevated)
{
    // D-11/D-13 contract: launch ONLY the resolved target; no target → Failed.
    if (entry.targetPath.isEmpty())
        return LaunchResult::Failed;

    HANDLE hProcess = INVALID_HANDLE_VALUE;
    DWORD err = classicLaunch(entry, elevated ? L"runas" : L"open", &hProcess);

    // Netch-class fix: a non-elevated `open` on an app whose manifest REQUIRES
    // elevation fails with SE_ERR_ACCESSDENIED (no UAC prompt — the shell
    // refuses to auto-elevate on a bare non-elevated open). Retry via `runas`
    // so the app actually launches (and a real UAC prompt appears) instead of
    // silently "not opening" — which is what previously left a UI-visible
    // failure path for the launcher to trip over. Explicit elevated callers
    // never reach this branch (they go straight to runas above).
    if (err != 0 && !elevated && err == SE_ERR_ACCESSDENIED) {
        qInfo("WinLaunch: '%s' needs elevation (err=%lu) — retrying with runas",
              qUtf8Printable(entry.displayName), ulong(err));
        if (hProcess && hProcess != INVALID_HANDLE_VALUE)
            CloseHandle(hProcess);
        err = classicLaunch(entry, L"runas", &hProcess);
    }

    if (err != 0) {
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

    if (hProcess && hProcess != INVALID_HANDLE_VALUE)
        CloseHandle(hProcess); // no wait — launch returns immediately (D-13)
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

LaunchResult launchCommand(const QString &command)
{
    // D-09: no empty commands — the instructional "cmd/ — type a command"
    // row is guarded here too (belt and suspenders with the controller).
    if (command.isEmpty())
        return LaunchResult::Failed;

    // D-09: /K (keep) rather than /C — the console must REMAIN OPEN after the
    // command finishes so the user can read the output (a /C console closes
    // the instant a fast command exits, which looks like the runner "did
    // nothing"). /D skips AutoRun.
    const std::wstring lineW =
        QStringLiteral("cmd.exe /D /K \"%1\"").arg(command).toStdWString();
    const std::wstring dirW =
        QDir::toNativeSeparators(QDir::homePath()).toStdWString();

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // PITFALL: lpApplicationName MUST be nullptr (or an absolute path) — a
    // bare "cmd.exe" name makes CreateProcess skip PATH search and fail with
    // ERROR_FILE_NOT_FOUND (error 2), so the runner silently did nothing.
    // With lpApplicationName = nullptr, CreateProcess resolves the first
    // token of lpCommandLine ("cmd.exe") through System32/PATH. lpCommandLine
    // is documented as writable-only. CREATE_NEW_CONSOLE: the child runs in
    // its own console window (visible command output — D-09). The user-profile
    // cwd keeps launched scripts from depending on the launcher's cwd.
    const BOOL ok = CreateProcessW(nullptr, const_cast<wchar_t *>(lineW.c_str()),
                                   nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE,
                                   nullptr, dirW.c_str(), &si, &pi);
    if (!ok) {
        qWarning("WinLaunch: CreateProcess(cmd.exe) for '%s' failed (err=%lu)",
                 qUtf8Printable(command), ulong(GetLastError()));
        return LaunchResult::Failed;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess); // no wait — the new console owns the child (D-13)
    return LaunchResult::Launched;
}

} // namespace WinLaunch