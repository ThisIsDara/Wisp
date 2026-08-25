#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class QQmlEngine;
class QQuickWindow;

// Phase 8 update dialog host (HotkeyCaptureDialog precedent): owns the
// UpdatePrompt + UpdateProgress QML windows, injects itself as their
// `updateUi` host (per-instance beginCreate/setProperty), and re-centers on
// the primary screen on every show. Pure presentation - all flow decisions
// live in main.cpp wiring; the engine's signals drive this surface.
class UpdateDialogs : public QObject
{
    Q_OBJECT

public:
    explicit UpdateDialogs(QQmlEngine *engine, QObject *parent = nullptr);

    // Download now / Later prompt (UI-SPEC S2). Esc = Later.
    // (Progress moved INTO Settings - UX pass 08-05; floating window removed.)
    void showPrompt(const QString &version);
    void closePrompt();

    // Called from QML buttons.
    Q_INVOKABLE void accept();
    Q_INVOKABLE void dismiss();

signals:
    void promptAccepted(); // "Download now"
    void promptRejected(); // "Later" / Esc - tomorrow's daily check re-toasts (D-02)

private:
    QQuickWindow *ensureWindow(QPointer<QQuickWindow> &slot, const QString &qmlName);

    QQmlEngine *m_engine;
    QPointer<QQuickWindow> m_prompt;
};
