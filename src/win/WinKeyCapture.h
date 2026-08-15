#pragma once

#include <QObject>
#include <QString>

#include <windows.h>

// WH_KEYBOARD_LL capture for the hotkey dialog (HOTK-01 capture).
//
// Windows reserves Alt+Space for the window system menu: the keydown is
// consumed by the OS before the focused window — and therefore Qt/QML —
// ever sees it. Unregistering our own global hotkey (HotkeyManager::suspend)
// is necessary but not sufficient. A low-level keyboard hook fires in the
// raw input thread BEFORE any window message routing: it sees Alt+Space,
// swallows it (return 1) so the system menu never opens, and reports the
// combo to the capture dialog. Installed ONLY while the capture dialog is
// open, on the GUI thread (which pumps messages — required for LL-hook
// callbacks).
class WinKeyCapture : public QObject
{
    Q_OBJECT

public:
    explicit WinKeyCapture(QObject *parent = nullptr);
    ~WinKeyCapture() override;

    // Idempotent: installs the hook (returns false on failure, never
    // crashes the dialog — other combos still work via QML Keys).
    bool start();
    void stop();
    bool active() const { return m_hook != nullptr; }

    // Pure vkCode + modifier-state → portable sequence ("Alt+Space") —
    // mirrors HotkeyCaptureDialog.qml's keyToken/composePortable. Unit-tested.
    static QString portableFromVk(quint32 vk, bool ctrl, bool alt, bool shift,
                                  bool meta);

signals:
    // Emitted when Alt+Space (or Ctrl+Alt+Space…) was captured and swallowed.
    void altSpaceCaptured(const QString &portable);

private:
    static LRESULT CALLBACK hookProc(int code, WPARAM wParam, LPARAM lParam);

    HHOOK m_hook = nullptr;
    static WinKeyCapture *s_instance;
};