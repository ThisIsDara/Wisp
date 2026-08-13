#pragma once

#include <QObject>

#include <atomic>
#include <thread>

// Win32 single-instance firewall (D-09, SYS-01): a session-local named mutex
// gates the process (tryAcquire), and a session-local named auto-reset event
// carries "show the launcher" requests from later-starting instances
// (signalShow -> watcher thread -> showRequested). Pure C++ interface — no
// Win32 headers here (src/win firewall rule): HANDLEs stay opaque (void*) and
// all Win32 detail lives in the .cpp, mirroring the WinHotkey/WinLaunch
// pattern.
//
// Usage (wired in 06-04 main.cpp): tryAcquire() at startup; on false the
// process is a duplicate — signalShow() to surface the existing instance,
// then exit. The surviving instance calls startWatching() once; every
// showRequested() means "another launch attempt happened — show the window".
//
// Threading: the watcher loops WaitForSingleObject on its own thread — never
// the GUI thread (PATTERNS anti-pattern). showRequested() is emitted from the
// watcher thread; Qt auto-queues it to receivers on the owner thread
// (WinHotkey precedent).
class WinSingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit WinSingleInstance(QObject *parent = nullptr);
    ~WinSingleInstance() override;

    // CreateMutexW(L"Local\\wisp-single-instance"): true if THIS process
    // acquired the mutex (first instance); false if another instance already
    // holds it (ERROR_ALREADY_EXISTS) — the caller should signalShow() and
    // exit. Idempotent per instance.
    bool tryAcquire();

    // "Show the launcher, I'm a duplicate." Create-or-open the named event
    // FIRST, then SetEvent (RESEARCH Pitfall 1): if the first instance has
    // not started waiting yet, the auto-reset event stays signaled and its
    // watcher fires immediately once it starts waiting.
    void signalShow();

    // Spawn the watcher thread (WaitForSingleObject loop emitting
    // showRequested() per signal). No-op if already watching. The event is
    // created create-or-open here, so signals delivered before this call are
    // still delivered (Pitfall 1).
    void startWatching();

    // Stop the watcher: clear the flag, wake the waiter, join the thread.
    // Safe to call when not watching (also invoked by the destructor).
    void stopWatching();

signals:
    void showRequested();

private:
    void *m_mutex = nullptr; // HANDLE — opaque, Win32 headers confined to the .cpp
    void *m_event = nullptr; // HANDLE — opaque, owned from startWatching
    std::atomic<bool> m_watching{false};
    std::thread m_watcher;
};
