#pragma once

#include <QtGlobal>

// Fullscreen-state firewall for the global hotkey (HOTK-04).
//
// Wraps SHQueryUserNotificationState (shellapi.h) — the supported way for a
// desktop app to learn "something fullscreen is active, don't pop up".
// Values verified against QUERY_USER_NOTIFICATION_STATE (RESEARCH.md §2):
// games report QUNS_RUNNING_D3D_FULL_SCREEN (3) or QUNS_BUSY (2);
// presentations report QUNS_PRESENTATION_MODE (4).
class WinFullscreenGuard
{
public:
    enum State {
        AcceptsNotifications = 0,   // QUNS_ACCEPTS_NOTIFICATIONS / QUIET_TIME / APP
        FullscreenActive = 1,       // QUNS_BUSY / RUNNING_D3D_FULL_SCREEN / PRESENTATION_MODE
        Other = 2,                  // QUNS_NOT_PRESENT or API failure
    };

    // Live OS query — "is fullscreen content on top right now". Refines the
    // QUNS verdict with a foreground-window check (D-02.3 amendment): a game
    // that reported exclusive fullscreen but is minimized/alt-tabbed no longer
    // counts as blocking — only a genuinely foreground, screen-covering window
    // defers the launcher.
    static State currentState();

    // Pure mapping of a QUNS_* value to State — unit-tested without the OS.
    static State fromQuns(quint32 quns);

    // Pure decision table for the softened guard: a blocking QUNS verdict
    // (BUSY/D3D_FULL_SCREEN/PRESENTATION_MODE) defers only when the observer
    // confirms a fullscreen window is actually foreground. Unit-tested.
    static State fromQunsWithForeground(quint32 quns, bool foregroundIsFullscreen);

    // Win32 check: does the foreground window cover its monitor's full rect
    // (top-left at (0,0) of the monitor, no taskbar inset)? Unknown/absent
    // foreground answers "true" — conservative, keeps old defer behavior.
    static bool foregroundWindowIsFullscreen();
};