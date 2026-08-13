#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QSet>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

// Win32 global-hotkey firewall (ARCHITECTURE.md src/win/ pattern).
//
// Registers hotkeys with RegisterHotKey(NULL, ...) and ingests WM_HOTKEY on a
// dedicated pump thread. Why not the Qt-documented
// QAbstractNativeEventFilter route? Verified DEFECT on Qt 6.11.1 (2026-08-10):
// the dispatcher delivers WM_TIMER etc. to filters but swallows WM_HOTKEY
// posted to the registering thread's queue — the filter never sees it (live
// probe: standalone Win32 pump received 2/2 injected hotkeys; a Qt-native
// filter received 0). A raw PeekMessage pump on the registering thread is the
// proven-working delivery mechanism (OS posts the NULL-hwnd WM_HOTKEY to that
// thread's queue regardless of foreground).
//
// All OS work happens on the pump thread; registers/unregisters are marshaled
// to it with a synchronous handshake so registerCombo's error result (1409 =
// "already in use", HOTK-02) stays truthful. m_registeredIds is guarded by
// m_mutex (owner thread mutates, pump thread reads). The native event filter
// remains installed as a best-effort secondary path (harmless; some Qt builds
// deliver).
class WinHotkey : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    WinHotkey();
    ~WinHotkey() override;

    // Registers one global combo. Returns false (and sets lastError) when the
    // combo is already owned by another app — ERROR_HOTKEY_ALREADY_REGISTERED
    // (1409) — or the combo is otherwise invalid. mods is a MOD_* mask that
    // should include MOD_NOREPEAT (0x4000) to avoid toggle spam while held.
    bool registerCombo(uint id, quint32 mods, quint32 vk);

    // Releases every combo this instance registered. Called from
    // HotkeyManager's destructor so no stale global hotkey survives
    // process exit (RESEARCH.md §1 UnregisterOnQuit).
    void unregisterAll();

    // Human-readable reason for a GetLastError() value. 1409 becomes
    // "in use by another application" (HOTK-02): conflicts must never be
    // silent. Anything else falls back to the numeric code.
    static QString errorString(quint32 lastError);

signals:
    // Emitted when the OS delivers WM_HOTKEY for a registered id. In practice
    // this fires on the pump thread; Qt auto-queues it to receivers on the
    // owner thread.
    void hotkeyTriggered(quint32 id);

protected:
    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           qintptr *result) override;

private:
    enum class Request { None, Register, UnregisterAll, Quit };

    struct PumpRequest {
        Request kind = Request::None;
        quint32 id = 0;
        quint32 mods = 0;
        quint32 vk = 0;
        bool *result = nullptr;              // Register: outcome back to caller
        quint32 *errorOut = nullptr;         // Register: GetLastError on failure
    };

    void pumpLoop();
    bool pumpRegister(PumpRequest &req) const;
    void dispatchHotkey(quint32 id);

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    PumpRequest m_pending;                   // guarded by m_mutex (one at a time)
    std::atomic<bool> m_pendingConsumed{true};
    std::atomic<bool> m_quit{false};
    std::atomic<quint32> m_lastError{0};

    QSet<quint32> m_registeredIds;           // guarded by m_mutex
};