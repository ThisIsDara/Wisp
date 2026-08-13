#include "win/WinHotkey.h"

#include <QCoreApplication>
#include <QDebug>

#include <chrono>
#include <windows.h>

namespace {
constexpr quint32 kWmHotkey = 0x0312;
}

WinHotkey::WinHotkey()
{
    // Best-effort secondary path (kept for Qt builds that DO deliver
    // dispatcher messages to filters — this one is proven dormant on 6.11.1).
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installNativeEventFilter(this);

    m_thread = std::thread(&WinHotkey::pumpLoop, this);
}

WinHotkey::~WinHotkey()
{
    unregisterAll();   // frees every combo on the pump thread (UnregisterOnQuit)
    m_quit.store(true);
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

bool WinHotkey::registerCombo(uint id, quint32 mods, quint32 vk)
{
    bool ok = false;
    quint32 error = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending = { Request::Register, static_cast<quint32>(id), mods, vk,
                      &ok, &error };
        m_pendingConsumed.store(false);
    }
    m_cv.notify_all();

    // Block until the pump thread has performed RegisterHotKey. The OS call is
    // on the pump thread so the NULL-hwnd WM_HOTKEY lands in ITS queue — the
    // queue our pump drains — not the GUI thread's.
    while (!m_pendingConsumed.load(std::memory_order_acquire))
        std::this_thread::yield();

    m_lastError.store(error, std::memory_order_release);
    if (ok) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_registeredIds.insert(static_cast<quint32>(id));
    } else {
        // The failure happened on the pump thread; restore the caller's
        // thread-local last-error so GetLastError() inspection contract holds
        // (HOTK-02 conflict detection reads it).
        SetLastError(error);
        qWarning() << "WinHotkey: RegisterHotKey failed (id" << id << "mods"
                   << Qt::hex << mods << Qt::dec << "vk" << vk << "):"
                   << errorString(error);
    }
    return ok;
}

void WinHotkey::unregisterAll()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_registeredIds.isEmpty())
            return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending = { Request::UnregisterAll, 0, 0, 0, nullptr, nullptr };
        m_pendingConsumed.store(false);
    }
    m_cv.notify_all();
    while (!m_pendingConsumed.load(std::memory_order_acquire))
        std::this_thread::yield();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_registeredIds.clear();
    }
}

QString WinHotkey::errorString(quint32 lastError)
{
    switch (lastError) {
    case ERROR_HOTKEY_ALREADY_REGISTERED:   // 1409
        return QStringLiteral("The hotkey is already in use by another application.");
    case ERROR_HOTKEY_NOT_REGISTERED:        // 1410
        return QStringLiteral("The hotkey is not registered.");
    default:
        return QString::number(lastError);
    }
}

bool WinHotkey::nativeEventFilter(const QByteArray &eventType, void *message,
                                  qintptr *result)
{
    Q_UNUSED(result)
    if (eventType != QByteArrayLiteral("windows_dispatcher_MSG"))
        return false;

    const auto *msg = static_cast<const MSG *>(message);
    bool known = false;
    if (static_cast<quint32>(msg->message) == kWmHotkey) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            known = m_registeredIds.contains(static_cast<quint32>(msg->wParam));
        }
        if (known)
            emit hotkeyTriggered(static_cast<quint32>(msg->wParam));
    }
    // Never consume: even a missed delivery must not break Qt/Win32 plumbing.
    return false;
}

void WinHotkey::pumpLoop()
{
    MSG msg;
    while (!m_quit.load(std::memory_order_acquire)) {
        // 1. Drain the thread's message queue — WM_HOTKEY lives here when the
        //    NULL-hwnd registration form is used (RegisterHotKey(...) posts to
        //    the REGISTERING thread's queue).
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (static_cast<quint32>(msg.message) == kWmHotkey)
                dispatchHotkey(static_cast<quint32>(msg.wParam));
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // 2. Serve marshaled requests from the owner thread.
        PumpRequest req;
        bool hasRequest = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_pending.kind != Request::None) {
                req = m_pending;
                m_pending.kind = Request::None;
                hasRequest = true;
            }
        }
        if (hasRequest) {
            switch (req.kind) {
            case Request::Register:
                if (req.result)
                    *req.result = pumpRegister(req);
                m_pendingConsumed.store(true, std::memory_order_release);
                break;
            case Request::UnregisterAll: {
                for (quint32 id : std::as_const(m_registeredIds))
                    UnregisterHotKey(nullptr, static_cast<int>(id));
                m_registeredIds.clear();
                m_pendingConsumed.store(true, std::memory_order_release);
                break;
            }
            case Request::Quit:
            case Request::None:
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void WinHotkey::dispatchHotkey(quint32 id)
{
    bool known;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        known = m_registeredIds.contains(id);
    }
    if (known)
        emit hotkeyTriggered(id);
}

bool WinHotkey::pumpRegister(PumpRequest &req) const
{
    if (RegisterHotKey(nullptr, static_cast<int>(req.id), req.mods, req.vk) == 0) {
        if (req.errorOut)
            *req.errorOut = GetLastError();
        return false;
    }
    return true;
}