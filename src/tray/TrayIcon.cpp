#include "tray/TrayIcon.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>

#include <windows.h>

namespace {

// Simple generated icon — no asset dependency (D-02.2): accent-filled disc
// with a white "w". 16px at 96dpi; the tray scales as needed. The disc fill
// follows the caller-supplied accent (UI-SPEC tray icon contract); the "w"
// stays white at all accents (white holds >= 4.5:1 on every pickable accent).
QIcon makeTrayIcon(const QColor &accent)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawEllipse(0, 0, 15, 15);
    p.setPen(QPen(Qt::white, 1.4));
    p.setFont(QFont(QStringLiteral("Segoe UI"), 11, QFont::Bold));
    p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("w"));
    p.end();

    return QIcon(pm);
}

} // namespace

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
    , m_accent(QColor(QStringLiteral("#0078D4"))) // Theme.accent default (D-16)
{
    m_tray = new QSystemTrayIcon(makeTrayIcon(m_accent), this);

    // NOT parented to this: QWidgets cannot be object-parented to non-widgets
    // (QObject::setParent asserts on QWidget children of non-widget parents).
    // Owned explicitly by ~TrayIcon.
    m_menu = new QMenu;

    // Menu order LOCKED (D-03 + UI-SPEC tray menu contract):
    // Open wisp / Settings / Change hotkey… / separator / Quit.
    auto *openAction = m_menu->addAction(QStringLiteral("Open wisp"));
    QObject::connect(openAction, &QAction::triggered, this, &TrayIcon::openWisp);

    auto *settingsAction = m_menu->addAction(QStringLiteral("Settings"));
    QObject::connect(settingsAction, &QAction::triggered, this, &TrayIcon::settingsRequested);

    auto *changeAction = m_menu->addAction(QStringLiteral("Change hotkey…"));
    QObject::connect(changeAction, &QAction::triggered, this, &TrayIcon::changeHotkeyRequested);

    m_menu->addSeparator();

    auto *quitAction = m_menu->addAction(QStringLiteral("Quit"));
    QObject::connect(quitAction, &QAction::triggered, this, &TrayIcon::quitRequested);

    m_tray->setContextMenu(m_menu);
}

TrayIcon::~TrayIcon()
{
    delete m_menu;   // QSystemTrayIcon does not own the context menu
    m_menu = nullptr;
}

void TrayIcon::show()
{
    m_tray->show();
}

void TrayIcon::setAccent(const QColor &accent)
{
    // D-16: invalid colors are silently ignored (Phase-5 setAccent discipline);
    // same-color no-ops skip the repaint entirely.
    if (!accent.isValid() || accent == m_accent)
        return;
    m_accent = accent;
    m_tray->setIcon(makeTrayIcon(m_accent));
}

void TrayIcon::notifyHotkeyConflict(const QString &combo)
{
    m_tray->showMessage(
        QStringLiteral("wisp — hotkey in use"),
        QStringLiteral("%1 is already registered by another application. "
                       "Use tray → Change hotkey… to pick a different one.")
            .arg(combo),
        QSystemTrayIcon::Warning, 5000);
}
