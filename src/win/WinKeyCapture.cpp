#include "win/WinKeyCapture.h"

#include <QDebug>

namespace {

bool keyIsDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

} // namespace

WinKeyCapture *WinKeyCapture::s_instance = nullptr;

WinKeyCapture::WinKeyCapture(QObject *parent)
    : QObject(parent)
{
}

WinKeyCapture::~WinKeyCapture()
{
    stop();
}

bool WinKeyCapture::start()
{
    if (m_hook)
        return true;
    s_instance = this;
    m_hook = SetWindowsHookExW(WH_KEYBOARD_LL, hookProc, GetModuleHandleW(nullptr), 0);
    if (!m_hook) {
        s_instance = nullptr;
        qWarning() << "WinKeyCapture: SetWindowsHookEx(WH_KEYBOARD_LL) failed:"
                   << GetLastError();
        return false;
    }
    return true;
}

void WinKeyCapture::stop()
{
    if (!m_hook)
        return;
    UnhookWindowsHookEx(m_hook);
    m_hook = nullptr;
    s_instance = nullptr;
}

LRESULT CALLBACK WinKeyCapture::hookProc(int code, WPARAM wParam, LPARAM lParam)
{
    // Alt+Space arrives as WM_SYSKEYDOWN (not WM_KEYDOWN) — accepting both
    // is the classic LL-hook requirement for Alt-modified combos. The
    // reliable Alt signal is the LLKHF_ALTDOWN flag in the hook struct.
    if (code == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        const auto *info = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
        const bool altDown = (info->flags & LLKHF_ALTDOWN) != 0;
        if (info->vkCode == VK_SPACE && altDown) {
            // Alt+Space: swallow it so the OS system menu never opens and
            // no hotkey (ours is suspended, but foreign ones exist) sees it;
            // report the combo to the capture dialog instead.
            if (s_instance) {
                emit s_instance->altSpaceCaptured(WinKeyCapture::portableFromVk(
                    info->vkCode, keyIsDown(VK_CONTROL), true, keyIsDown(VK_SHIFT),
                    keyIsDown(VK_LWIN) || keyIsDown(VK_RWIN)));
            }
            return 1; // consumed
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

QString WinKeyCapture::portableFromVk(quint32 vk, bool ctrl, bool alt, bool shift,
                                      bool meta)
{
    QStringList tokens;
    if (ctrl)
        tokens << QStringLiteral("Ctrl");
    if (alt)
        tokens << QStringLiteral("Alt");
    if (shift)
        tokens << QStringLiteral("Shift");
    if (meta)
        tokens << QStringLiteral("Meta");

    QString key;
    if (vk >= 'A' && vk <= 'Z')
        key = QChar(static_cast<char16_t>(vk));
    else if (vk >= '0' && vk <= '9')
        key = QChar(static_cast<char16_t>(vk));
    else if (vk >= VK_F1 && vk <= VK_F24)
        key = QStringLiteral("F%1").arg(vk - VK_F1 + 1);
    else {
        switch (vk) {
        case VK_SPACE:   key = QStringLiteral("Space"); break;
        case VK_TAB:     key = QStringLiteral("Tab"); break;
        case VK_RETURN:  key = QStringLiteral("Return"); break;
        case VK_ESCAPE:  key = QStringLiteral("Esc"); break;
        case VK_BACK:    key = QStringLiteral("Backspace"); break;
        case VK_DELETE:  key = QStringLiteral("Del"); break;
        case VK_INSERT:  key = QStringLiteral("Ins"); break;
        case VK_HOME:    key = QStringLiteral("Home"); break;
        case VK_END:     key = QStringLiteral("End"); break;
        case VK_PRIOR:   key = QStringLiteral("PgUp"); break;
        case VK_NEXT:    key = QStringLiteral("PgDown"); break;
        case VK_UP:      key = QStringLiteral("Up"); break;
        case VK_DOWN:    key = QStringLiteral("Down"); break;
        case VK_LEFT:    key = QStringLiteral("Left"); break;
        case VK_RIGHT:   key = QStringLiteral("Right"); break;
        default: break; // modifier keys themselves carry no key token
        }
    }
    if (!key.isEmpty())
        tokens << key;
    return tokens.join(QLatin1Char('+'));
}