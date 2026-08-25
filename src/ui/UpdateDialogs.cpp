#include "ui/UpdateDialogs.h"

#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>

namespace {

// UI-SPEC Geometry: re-centered on the primary screen on EVERY show (same
// contract as HotkeyCaptureDialog/SettingsWindow - QML onCompleted
// positioning is first-show-only).
void centerOnPrimary(QQuickWindow *win)
{
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        win->setX(avail.x() + (avail.width() - win->width()) / 2);
        win->setY(avail.y() + (avail.height() - win->height()) / 2);
    }
}

} // namespace

UpdateDialogs::UpdateDialogs(QQmlEngine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
}

QQuickWindow *UpdateDialogs::ensureWindow(QPointer<QQuickWindow> &slot, const QString &qmlName)
{
    if (slot)
        return slot;

    // Module-import load (HotkeyCaptureDialog CR fix): resources live under
    // :/qt/qml/wisp/qml/ — a literal qrc URL never resolves.
    QQmlComponent component(m_engine);
    const QString source = QStringLiteral("import wisp\n%1 {\n}").arg(qmlName);
    component.setData(source.toUtf8(),
                      QUrl(QStringLiteral("qrc:/qt/qml/wisp/") + qmlName + QStringLiteral(".qml")));
    if (!component.isReady()) {
        for (const auto &e : component.errors())
            qWarning() << e.toString();
        return nullptr;
    }

    // Host injection: per-instance beginCreate/setProperty, no global
    // context pollution (REQUIRED pattern from the capture dialog).
    QObject *obj = component.beginCreate(m_engine->rootContext());
    obj->setProperty("updateUi", QVariant::fromValue(this));
    component.completeCreate();

    slot = qobject_cast<QQuickWindow *>(obj);
    if (!slot) {
        delete obj;
        qWarning("UpdateDialogs: %s did not produce a window", qPrintable(qmlName));
    }
    return slot;
}

void UpdateDialogs::showPrompt(const QString &version)
{
    QQuickWindow *win = ensureWindow(m_prompt, QStringLiteral("UpdatePrompt"));
    if (!win)
        return;
    win->setProperty("updateVersion", version);
    centerOnPrimary(win);
    QMetaObject::invokeMethod(win, "show");
    win->requestActivate();
}

void UpdateDialogs::closePrompt()
{
    if (m_prompt)
        m_prompt->close();
}

void UpdateDialogs::showProgress(const QString &version)
{
    QQuickWindow *win = ensureWindow(m_progress, QStringLiteral("UpdateProgress"));
    if (!win)
        return;
    win->setProperty("progressLabel",
                     QStringLiteral("Downloading wisp v%1...").arg(version));
    win->setProperty("progressRatio", 0.0);
    centerOnPrimary(win);
    QMetaObject::invokeMethod(win, "show");
}

void UpdateDialogs::setProgress(qint64 received, qint64 total)
{
    if (!m_progress)
        return;
    m_progress->setProperty("progressRatio", total > 0 ? double(received) / double(total) : 0.0);
}

void UpdateDialogs::setVerifying()
{
    if (!m_progress)
        return;
    m_progress->setProperty("progressLabel", QStringLiteral("Verifying..."));
    m_progress->setProperty("progressRatio", 1.0);
}

void UpdateDialogs::closeProgress()
{
    if (m_progress)
        m_progress->close();
}

void UpdateDialogs::accept()
{
    closePrompt();
    emit promptAccepted();
}

void UpdateDialogs::dismiss()
{
    closePrompt();
    emit promptRejected();
}
