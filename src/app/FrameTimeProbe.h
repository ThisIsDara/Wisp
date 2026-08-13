#pragma once

#include <QObject>
#include <QElapsedTimer>

class QQuickWindow;

class FrameTimeProbe : public QObject
{
    Q_OBJECT
public:
    explicit FrameTimeProbe(QQuickWindow *window, QObject *parent = nullptr);

private slots:
    void onFrameRendered();

private:
    QQuickWindow *m_window;
    QElapsedTimer m_timer;
    qint64 m_lastMs = -1;
    int m_slowFrames = 0;
};
