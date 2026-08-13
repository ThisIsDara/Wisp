#include <QtTest>

#include "ui/HotkeyCaptureDialog.h"

// Pure validation-function tests — no QML, no window, no hotkey registration.
class TstCapture : public QObject
{
    Q_OBJECT

private slots:
    void validationMatrix();
};

void TstCapture::validationMatrix()
{
    // Valid combos.
    QVERIFY(HotkeyCaptureDialog::validateSequence(QStringLiteral("Alt+Space")));
    QVERIFY(HotkeyCaptureDialog::validateSequence(QStringLiteral("Ctrl+Shift+F9")));
    QVERIFY(HotkeyCaptureDialog::validateSequence(QStringLiteral("Ctrl+Alt+Delete")));
    QVERIFY(HotkeyCaptureDialog::validateSequence(QStringLiteral("Alt+F4")));

    // F12 is kernel-reserved — anywhere in the combo.
    QVERIFY(!HotkeyCaptureDialog::validateSequence(QStringLiteral("F12")));
    QVERIFY(!HotkeyCaptureDialog::validateSequence(QStringLiteral("Alt+F12")));
    QVERIFY(!HotkeyCaptureDialog::validateSequence(QStringLiteral("Ctrl+Shift+F12")));

    // Modifier-only combos are not combos.
    QVERIFY(!HotkeyCaptureDialog::validateSequence(QStringLiteral("Alt")));
    QVERIFY(!HotkeyCaptureDialog::validateSequence(QStringLiteral("Ctrl")));
    QVERIFY(!HotkeyCaptureDialog::validateSequence(QStringLiteral("Shift")));
    QVERIFY(!HotkeyCaptureDialog::validateSequence(QStringLiteral("Meta")));

    // Empty.
    QVERIFY(!HotkeyCaptureDialog::validateSequence(QString()));
}

QTEST_MAIN(TstCapture)
#include "tst_capture.moc"