#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class QQmlEngine;
class QQuickWindow;
class WinKeyCapture;

// Theme-driven hotkey capture (D-02.6): hosts the QML dialog, validates the
// captured combo (F12 / modifier-only rejected), and emits the accepted
// portable sequence for HotkeyManager::setHotkey (HOTK-01 configurable).
class HotkeyCaptureDialog : public QObject
{
    Q_OBJECT

public:
    explicit HotkeyCaptureDialog(QQmlEngine *engine, QObject *parent = nullptr);

    // Shows the dialog, pre-filling the current hotkey display.
    void open(const QString &currentSequence);

    // Pure validation — unit-tested (tst_capture), no QML involved.
    // Rejects: empty, any F12 token (kernel-reserved), modifier-only combos.
    static bool validateSequence(const QString &portable);

    // Called from QML (Ok button) once dialogHost is injected.
    Q_INVOKABLE void submitSequence(const QString &portable);

signals:
    void accepted(const QString &portableSequence);
    void cancelled();

private:
    QQmlEngine *m_engine;
    QPointer<QQuickWindow> m_dialog;
    WinKeyCapture *m_keyCapture;
};