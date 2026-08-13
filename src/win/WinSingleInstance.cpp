#include "win/WinSingleInstance.h"

#include <QDebug>

#include <windows.h>

namespace {

// Session-local namespace (RESEARCH A2): separate sessions (RDP) get separate
// namespaces — acceptable for a launcher. Distinctive "wisp-" prefix minimizes
// cross-app collision (T-06-05: worst case a duplicate exits silently — the
// benign failure mode).
const wchar_t kMutexName[] = L"Local\\wisp-single-instance";
const wchar_t kShowEventName[] = L"Local\\wisp-show-launcher";

} // namespace

WinSingleInstance::WinSingleInstance(QObject *parent)
    : QObject(parent)
{
}

WinSingleInstance::~WinSingleInstance()
{
    stopWatching();
    if (m_mutex)
        CloseHandle(static_cast<HANDLE>(m_mutex));
    if (m_event)
        CloseHandle(static_cast<HANDLE>(m_event));
}

bool WinSingleInstance::tryAcquire()
{
    if (m_mutex)
        return true; // this instance already acquired it — idempotent

    // bInitialOwner=FALSE: no ownership semantics needed — the handle IS the
    // gate; existence of the named object is the signal.
    HANDLE h = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!h) {
        qWarning("WinSingleInstance: CreateMutexW failed (err=%lu)",
                 ulong(GetLastError()));
        return false; // OS-level failure — caller decides (treated as not-first)
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another instance owns the name: we do NOT hold the gate. Drop the
        // handle (the existing instance's handle keeps the kernel object
        // alive) and report duplicate.
        CloseHandle(h);
        return false;
    }
    m_mutex = h;
    return true;
}

void WinSingleInstance::signalShow()
{
    // Create-or-open FIRST (RESEARCH Pitfall 1): if the first instance's
    // watcher does not exist yet, the event object is created here and stays
    // signaled; when the watcher later opens and waits, it fires immediately.
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, kShowEventName);
    if (!ev) {
        qWarning("WinSingleInstance: CreateEventW failed (err=%lu)",
                 ulong(GetLastError()));
        return;
    }
    SetEvent(ev);            // auto-reset (bManualReset=FALSE): one show request
    CloseHandle(ev);         // the watcher holds its own handle
}

void WinSingleInstance::startWatching()
{
    if (m_watching.exchange(true))
        return; // already watching

    // create-or-open: a signal delivered before this call (Pitfall 1) leaves
    // the event signaled — the first wait returns immediately. Do NOT reset:
    // that would eat a pending show request.
    m_event = CreateEventW(nullptr, FALSE, FALSE, kShowEventName);
    if (!m_event) {
        qWarning("WinSingleInstance: CreateEventW failed (err=%lu)",
                 ulong(GetLastError()));
        m_watching.store(false);
        return;
    }

    m_watcher = std::thread([this] {
        while (m_watching.load(std::memory_order_acquire)) {
            const DWORD r = WaitForSingleObject(static_cast<HANDLE>(m_event), INFINITE);
            if (r != WAIT_OBJECT_0) {
                qWarning("WinSingleInstance: WaitForSingleObject failed (r=%lu)",
                         ulong(r));
                break;
            }
            // Emitted from the watcher thread; Qt queues it to receivers on
            // the owner (GUI) thread (WinHotkey precedent).
            emit showRequested();
        }
    });
}

void WinSingleInstance::stopWatching()
{
    if (!m_watching.exchange(false))
        return;
    // Wake the waiter so it observes the quit flag; the join guarantees the
    // thread is gone before the destructor closes the handles (no dangling
    // HANDLE use).
    if (m_event)
        SetEvent(static_cast<HANDLE>(m_event));
    if (m_watcher.joinable())
        m_watcher.join();
}
