#include "win/WinFullscreenGuard.h"

#include <windows.h>
#include <shellapi.h>

WinFullscreenGuard::State WinFullscreenGuard::currentState()
{
    QUERY_USER_NOTIFICATION_STATE quns = QUNS_NOT_PRESENT;
    // Pure Win32, no COM init required (RESEARCH.md §2). Failure → Other:
    // the caller must decide a safe default (defer).
    if (SHQueryUserNotificationState(&quns) != S_OK)
        return Other;
    return fromQunsWithForeground(static_cast<quint32>(quns),
                                  foregroundWindowIsFullscreen());
}

WinFullscreenGuard::State WinFullscreenGuard::fromQunsWithForeground(quint32 quns,
                                                                     bool foregroundIsFullscreen)
{
    switch (quns) {
    case 2: // QUNS_BUSY — full-screen app / presentation settings
    case 3: // QUNS_RUNNING_D3D_FULL_SCREEN — exclusive-mode D3D game
    case 4: // QUNS_PRESENTATION_MODE
        // Softened (D-02.3 amendment): only a foreground fullscreen window
        // actually dominates the screen; a minimized/alt-tabbed game must not
        // lock the launcher out.
        return foregroundIsFullscreen ? FullscreenActive : AcceptsNotifications;
    case 5: // QUNS_ACCEPTS_NOTIFICATIONS
    case 6: // QUNS_QUIET_TIME
    case 7: // QUNS_APP
        return AcceptsNotifications;
    default: // 1 = QUNS_NOT_PRESENT (locked screen) and anything unknown
        return Other;
    }
}

bool WinFullscreenGuard::foregroundWindowIsFullscreen()
{
    const HWND fg = ::GetForegroundWindow();
    // Absent foreground (session quirks) → "fullscreen" — conservative,
    // preserves the pre-softening defer behavior (D-02.3 "unknown → defer").
    if (fg == nullptr)
        return true;
    // A minimized or hidden game holds no screen real estate.
    if (::IsIconic(fg) || !::IsWindowVisible(fg))
        return false;

    RECT r{};
    if (!::GetWindowRect(fg, &r))
        return true;
    const HMONITOR mon = ::MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(MONITORINFO) };
    if (!::GetMonitorInfoW(mon, &mi))
        return true;

    // Cover the monitor's FULL rect (including the taskbar strip) — a merely
    // maximized window stops at the working area and must not count.
    constexpr int tol = 8; // px slack for DPI rounding / border slop
    return r.left <= mi.rcMonitor.left + tol
        && r.top <= mi.rcMonitor.top + tol
        && r.right >= mi.rcMonitor.right - tol
        && r.bottom >= mi.rcMonitor.bottom - tol;
}

WinFullscreenGuard::State WinFullscreenGuard::fromQuns(quint32 quns)
{
    switch (quns) {
    case 2: // QUNS_BUSY — full-screen app / presentation settings
    case 3: // QUNS_RUNNING_D3D_FULL_SCREEN — exclusive-mode D3D game
    case 4: // QUNS_PRESENTATION_MODE
        return FullscreenActive;
    case 5: // QUNS_ACCEPTS_NOTIFICATIONS
    case 6: // QUNS_QUIET_TIME
    case 7: // QUNS_APP
        return AcceptsNotifications;
    default: // 1 = QUNS_NOT_PRESENT (locked screen) and anything unknown
        return Other;
    }
}