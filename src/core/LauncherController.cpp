#include "core/LauncherController.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QQuickWindow>
#include <QTimer>
#include <QWindow>

namespace {
constexpr int kDeactivationGraceMs = 150;

// Locked focus sequence (CONTEXT.md): show → raise → requestActivate, with
// requestActivate deferred OFF the WM_HOTKEY handler (the native filter must
// return immediately). The QML Window type exposes requestActivate(); if name
// resolution fails for any reason, fall back to the QWindow member on a 0ms
// timer — still deferred.
void showAndActivate(QQuickWindow *win)
{
    win->show();
    win->raise();
    if (!QMetaObject::invokeMethod(win, "requestActivate", Qt::QueuedConnection))
        QTimer::singleShot(0, win, &QWindow::requestActivate);
}
} // namespace

LauncherController::LauncherController(QObject *parent)
    : QObject(parent)
    , m_graceTimer(new QTimer(this))
{
    m_graceTimer->setSingleShot(true);
    m_graceTimer->setInterval(kDeactivationGraceMs);
    connect(m_graceTimer, &QTimer::timeout, this, [this] {
        if (m_state != Visible)
            return;
        if (!m_win.isNull() && m_win->isActive())
            return; // focus came back during the grace window — never force-hide
        hideAnimated();
    });
}

void LauncherController::setWindow(QQuickWindow *win)
{
    m_win = win;
}

void LauncherController::setFullscreenGuard(std::function<WinFullscreenGuard::State()> guard)
{
    m_guard = std::move(guard);
}

bool LauncherController::canShow() const
{
    return m_guard() != WinFullscreenGuard::FullscreenActive;
}

void LauncherController::show()
{
    if (!canShow())
        return; // HOTK-04: defer silently — game keeps focus, launcher stays hidden
    m_state = Visible;
    showWindow();
}

void LauncherController::showUserRequested()
{
    m_guard(); // consulted for observability; verdict ignored (D-02.3 — user intent wins)
    m_state = Visible;
    showWindow();
}

void LauncherController::hideAnimated()
{
    if (m_state != Visible)
        return;
    if (!m_win.isNull() && !m_win->isVisible())
        return; // already hidden (Esc/launch path) — never ghost-dismiss
    m_state = Hidden;
    if (m_win.isNull())
        return;
    if (!QMetaObject::invokeMethod(m_win, "dismiss", Qt::QueuedConnection))
        m_win->hide(); // T-02-02-04: QML API missing — plain hide, no crash
}

void LauncherController::hideNow()
{
    m_state = Hidden;
    if (m_win.isNull())
        return;
    // The QML hideNow() resets the closing flag + stops the close animation,
    // then hides instantly — one source of truth for the instant dismiss.
    if (!QMetaObject::invokeMethod(m_win, "hideNow", Qt::QueuedConnection))
        m_win->hide();
}

void LauncherController::toggle()
{
    // The WINDOW's actual visibility is the source of truth: Esc/launch
    // dismissals in QML hide the window without telling this controller, so
    // a m_state-based toggle would ghost-dismiss an already-hidden window
    // and leave QML's closing flag stuck (observed bug: Esc/click-away stop
    // working after one close).
    if (!m_win.isNull() && m_win->isVisible())
        hideAnimated();
    else
        show();
}

void LauncherController::onWindowActiveChanged(bool active)
{
    if (active) {
        m_graceTimer->stop();
        return;
    }
    if (m_state == Visible)
        m_graceTimer->start();
}

void LauncherController::showWindow()
{
    if (m_win.isNull())
        return;
    showAndActivate(m_win.data());
    // WM placement fix (2026-08-10): QML x/y assignments lose to the show
    // placement of frameless tool windows (window landed at 0,0 despite
    // re-apply in onVisibleChanged). Re-assert the CENTER from C++ after
    // the show/raise/activate dance settles — observed: this sticks.
    QTimer::singleShot(0, m_win.data(), [win = m_win.data()] {
        if (win->screen()) {
            const QRect avail = win->screen()->availableGeometry();
            win->setPosition(avail.x() + (avail.width() - win->width()) / 2,
                             avail.y() + (avail.height() - win->height()) / 2);
        }
    });
}

void LauncherController::stateNote(const QString &tag)
{
    QFile f(QDir::temp().filePath(QStringLiteral("wisp-events.log")));
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    f.write(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")).toUtf8());
    f.write("  ");
    f.write(tag.toUtf8());
    f.write("\n");
}