#include "core/HotkeyManager.h"

#include "win/WinHotkey.h"

#include <QKeyCombination>
#include <QSettings>

#include <windows.h>

namespace {

constexpr quint32 kHotkeyId = 1;

// Qt::Key -> VK_* for the keys a launcher hotkey realistically uses.
// Anything unmapped returns 0 = invalid combo (rejected by HotkeyManager).
quint32 qtKeyToVk(Qt::Key key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return static_cast<quint32>('A' + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return static_cast<quint32>('0' + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return static_cast<quint32>(VK_F1 + (key - Qt::Key_F1));

    switch (key) {
    case Qt::Key_Space:       return VK_SPACE;
    case Qt::Key_Tab:         return VK_TAB;
    case Qt::Key_Return:      return VK_RETURN;
    case Qt::Key_Enter:       return VK_RETURN;
    case Qt::Key_Escape:      return VK_ESCAPE;
    case Qt::Key_Backspace:   return VK_BACK;
    case Qt::Key_Delete:      return VK_DELETE;
    case Qt::Key_Insert:      return VK_INSERT;
    case Qt::Key_Home:        return VK_HOME;
    case Qt::Key_End:         return VK_END;
    case Qt::Key_PageUp:      return VK_PRIOR;
    case Qt::Key_PageDown:    return VK_NEXT;
    case Qt::Key_Up:          return VK_UP;
    case Qt::Key_Down:        return VK_DOWN;
    case Qt::Key_Left:        return VK_LEFT;
    case Qt::Key_Right:       return VK_RIGHT;
    case Qt::Key_Plus:        return VK_ADD;
    case Qt::Key_Minus:       return VK_SUBTRACT;
    case Qt::Key_Comma:       return VK_OEM_COMMA;
    case Qt::Key_Period:      return VK_OEM_PERIOD;
    case Qt::Key_Slash:       return VK_OEM_2;
    case Qt::Key_Backslash:   return VK_OEM_5;
    case Qt::Key_Semicolon:   return VK_OEM_1;
    case Qt::Key_Apostrophe:  return VK_OEM_7;
    case Qt::Key_BracketLeft: return VK_OEM_4;
    case Qt::Key_BracketRight:return VK_OEM_6;
    default:                  return 0;
    }
}

quint32 qtModsToWin(Qt::KeyboardModifiers mods)
{
    quint32 m = 0;
    if (mods & Qt::ControlModifier) m |= MOD_CONTROL;
    if (mods & Qt::AltModifier)     m |= MOD_ALT;
    if (mods & Qt::ShiftModifier)   m |= MOD_SHIFT;
    if (mods & Qt::MetaModifier)    m |= MOD_WIN;
    return m;
}

// Empty / modifier-only / kernel-reserved F12 / unmapped keys are invalid
// (RESEARCH.md §1 — F12 is owned by the kernel's debugger path).
bool sequenceInvalid(const QKeySequence &seq)
{
    if (seq.count() == 0)
        return true;
    const Qt::Key key = seq[0].key();
    if (key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Shift
        || key == Qt::Key_Meta || key == Qt::Key_F12)
        return true;
    return qtKeyToVk(key) == 0;
}

} // namespace

HotkeyManager::HotkeyManager(const QString &settingsPath, QObject *parent)
    : QObject(parent)
    , m_winHotkey(new WinHotkey)
    , m_settings(settingsPath.isEmpty()
                     ? new QSettings(QSettings::IniFormat, QSettings::UserScope,
                                     QStringLiteral("TID"), QStringLiteral("wisp"))
                     : new QSettings(settingsPath, QSettings::IniFormat))
{
    // D-02.5: key hotkey/sequence, default Alt+Space.
    m_hotkey = QKeySequence::fromString(
        m_settings->value(QStringLiteral("hotkey/sequence"),
                          QStringLiteral("Alt+Space")).toString());
}

// UnregisterOnQuit (RESEARCH.md §1, PITFALLS #1): teardown releases every
// global combo so no stale registration survives process exit.
HotkeyManager::~HotkeyManager()
{
    m_winHotkey->unregisterAll();
    delete m_winHotkey;
    m_winHotkey = nullptr;
    delete m_settings;
}

bool HotkeyManager::start()
{
    if (sequenceInvalid(m_hotkey)) {
        emit registrationFailed(m_hotkey.toString());
        return false;
    }

    const QKeyCombination combo = m_hotkey[0];
    const quint32 mods = qtModsToWin(combo.keyboardModifiers()) | MOD_NOREPEAT;
    if (!m_winHotkey->registerCombo(kHotkeyId, mods, qtKeyToVk(combo.key()))) {
        emit registrationFailed(m_hotkey.toString());
        return false;
    }

    connect(m_winHotkey, &WinHotkey::hotkeyTriggered, this,
            [this](quint32 id) { if (id == kHotkeyId) emit hotkeyPressed(); });
    return true;
}

QKeySequence HotkeyManager::hotkey() const
{
    return m_hotkey;
}

void HotkeyManager::setHotkey(const QKeySequence &seq)
{
    if (sequenceInvalid(seq)) {
        emit registrationFailed(seq.toString());
        return;
    }
    if (seq == m_hotkey)
        return;

    // While suspended (capture dialog open) the combo is already released;
    // swap the target only — resume() performs the actual registration so a
    // single registration point holds (no double-register/double-fallback).
    if (m_suspended) {
        m_hotkey = seq;
        m_settings->setValue(QStringLiteral("hotkey/sequence"), seq.toString());
        m_settings->sync();
        emit hotkeyChanged(seq);
        return;
    }

    const QKeyCombination combo = seq[0];
    const quint32 mods = qtModsToWin(combo.keyboardModifiers()) | MOD_NOREPEAT;
    const quint32 vk = qtKeyToVk(combo.key());

    // Snapshot the current combo so availability-left holds: if the NEW
    // registration is refused, the OLD combo is re-registered and the
    // failure is surfaced (HOTK-02 — never silent).
    const QKeyCombination oldCombo = m_hotkey[0];
    const quint32 oldMods = qtModsToWin(oldCombo.keyboardModifiers()) | MOD_NOREPEAT;
    const quint32 oldVk = qtKeyToVk(oldCombo.key());

    m_winHotkey->unregisterAll();
    if (!m_winHotkey->registerCombo(kHotkeyId, mods, vk)) {
        m_winHotkey->registerCombo(kHotkeyId, oldMods, oldVk);
        emit registrationFailed(seq.toString());
        return;
    }

    m_hotkey = seq;
    m_settings->setValue(QStringLiteral("hotkey/sequence"), seq.toString());
    m_settings->sync();
    emit hotkeyChanged(seq);
}

void HotkeyManager::suspend()
{
    if (m_suspended)
        return;
    m_preSuspendCombo = m_hotkey;
    m_winHotkey->unregisterAll();
    m_suspended = true;
}

void HotkeyManager::resume()
{
    if (!m_suspended)
        return;
    m_suspended = false;

    if (sequenceInvalid(m_hotkey)) {
        emit registrationFailed(m_hotkey.toString());
        return;
    }

    const QKeyCombination combo = m_hotkey[0];
    const quint32 mods = qtModsToWin(combo.keyboardModifiers()) | MOD_NOREPEAT;
    const quint32 vk = qtKeyToVk(combo.key());

    // Availability-left, same as setHotkey: if the current combo can't be
    // re-registered (another app grabbed it while we were suspended), fall
    // back to the pre-capture combo and surface the conflict.
    if (m_winHotkey->registerCombo(kHotkeyId, mods, vk))
        return;

    if (!m_preSuspendCombo.isEmpty() && m_preSuspendCombo != m_hotkey) {
        const QKeyCombination oldCombo = m_preSuspendCombo[0];
        m_winHotkey->registerCombo(
            kHotkeyId,
            qtModsToWin(oldCombo.keyboardModifiers()) | MOD_NOREPEAT,
            qtKeyToVk(oldCombo.key()));
    }
    emit registrationFailed(m_hotkey.toString());
}