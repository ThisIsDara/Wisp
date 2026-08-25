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

    // Phase 8 (UI-SPEC S4): persistent pending-update entry. Hidden until an
    // update is pending (D-03); placed after the locked top block, before the
    // separator, so the D-03 order contract above is untouched.
    m_updateAction = m_menu->addAction(QStringLiteral("Download update"));
    m_updateAction->setVisible(false);
    QObject::connect(m_updateAction, &QAction::triggered, this,
                     &TrayIcon::updateDownloadRequested);

    m_menu->addSeparator();

    auto *quitAction = m_menu->addAction(QStringLiteral("Quit"));
    QObject::connect(quitAction, &QAction::triggered, this, &TrayIcon::quitRequested);

    m_tray->setContextMenu(m_menu);

    // Toast-click routing: only the update-available toast is actionable
    // (opens the Download now / Later prompt in main.cpp). The conflict
    // toast points at the menu instead.
    QObject::connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this] {
        if (m_lastToastKind == ToastKind::UpdateAvailable)
            emit updateToastClicked();
        m_lastToastKind = ToastKind::None;
    });
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
    m_lastToastKind = ToastKind::HotkeyConflict;
    m_tray->showMessage(
        QStringLiteral("wisp — hotkey in use"),
        QStringLiteral("%1 is already registered by another application. "
                       "Use tray → Change hotkey… to pick a different one.")
            .arg(combo),
        QSystemTrayIcon::Warning, 5000);
}

void TrayIcon::notifyUpdateAvailable(const QString &version)
{
    // UI-SPEC S4 copy; Info icon (conflict stays Warning). The click routes
    // through messageClicked -> updateToastClicked via the kind discriminator.
    m_lastToastKind = ToastKind::UpdateAvailable;
    m_tray->showMessage(
        QStringLiteral("wisp update available"),
        QStringLiteral("wisp v%1 is ready to install.").arg(version),
        QSystemTrayIcon::Information, 5000);
}

void TrayIcon::setUpdatePending(bool pending, const QString &version)
{
    if (!m_updateAction)
        return;
    m_updateAction->setText(pending
                                ? QStringLiteral("Download update v%1").arg(version)
                                : QStringLiteral("Download update"));
    m_updateAction->setVisible(pending); // D-03: visible only while pending
}

void TrayIcon::notifyUpdated(const QString &version)
{
    // D-14: informational only - no click action, kind stays None.
    m_lastToastKind = ToastKind::None;
    m_tray->showMessage(
        QStringLiteral("wisp updated"),
        QStringLiteral("wisp is now v%1.").arg(version),
        QSystemTrayIcon::Information, 5000);
}

void TrayIcon::notifyUpdateFailed(const QString &version)
{
    // Auto path give-up notice (D-08 terminal copy). No click action.
    m_lastToastKind = ToastKind::None;
    m_tray->showMessage(
        QStringLiteral("Update failed"),
        QStringLiteral("Couldn't download v%1 - wisp will try again tomorrow.").arg(version),
        QSystemTrayIcon::Warning, 5000);
}
