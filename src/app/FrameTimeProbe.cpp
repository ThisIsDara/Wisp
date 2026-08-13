#include "FrameTimeProbe.h"

#include <QQuickWindow>
#include <QElapsedTimer>
#include <QDebug>

#ifdef QT_DEBUG

FrameTimeProbe::FrameTimeProbe(QQuickWindow *window, QObject *parent)
    : QObject(parent), m_window(window)
{
    m_timer.start();
    connect(window, &QQuickWindow::afterRendering, this, &FrameTimeProbe::onFrameRendered);
}

void FrameTimeProbe::onFrameRendered()
{
    const qint64 now = m_timer.restart();
    if (m_lastMs >= 0) {
        const qint64 dt = now - m_lastMs;
        if (dt > 17) {   // >16.7ms = dropped 60fps frame (VISU-01 frame budget)
            ++m_slowFrames;
            qDebug() << "[perf] slow frame:" << dt << "ms (accumulated:" << m_slowFrames << ")";
        }
    }
    m_lastMs = now;
}

#else

FrameTimeProbe::FrameTimeProbe(QQuickWindow *window, QObject *parent)
    : QObject(parent), m_window(window)
{
    // Probe compiled out in release builds — zero overhead.
}

void FrameTimeProbe::onFrameRendered()
{
}

#endif
