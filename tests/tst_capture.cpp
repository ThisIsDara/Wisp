#include <QtTest>

#include "ui/HotkeyCaptureDialog.h"
#include "win/WinKeyCapture.h"

#include <windows.h>

// Pure validation-function tests — no QML, no window, no hotkey registration.
class TstCapture : public QObject
{
    Q_OBJECT

private slots:
    void validationMatrix();
    void portableFromVkMatrix();
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

void TstCapture::portableFromVkMatrix()
{
    // Mirrors QML keyToken/composePortable: modifier order Ctrl, Alt, Shift,
    // Meta + key.
    QCOMPARE(WinKeyCapture::portableFromVk(VK_SPACE, false, true, false, false),
             QStringLiteral("Alt+Space"));
    QCOMPARE(WinKeyCapture::portableFromVk(VK_SPACE, true, true, false, false),
             QStringLiteral("Ctrl+Alt+Space"));
    QCOMPARE(WinKeyCapture::portableFromVk(VK_SPACE, true, true, true, true),
             QStringLiteral("Ctrl+Alt+Shift+Meta+Space"));
    QCOMPARE(WinKeyCapture::portableFromVk('A', false, true, false, false),
             QStringLiteral("Alt+A"));
    QCOMPARE(WinKeyCapture::portableFromVk('9', false, false, true, false),
             QStringLiteral("Shift+9"));
    QCOMPARE(WinKeyCapture::portableFromVk(VK_F9, true, false, true, false),
             QStringLiteral("Ctrl+Shift+F9"));
    QCOMPARE(WinKeyCapture::portableFromVk(VK_TAB, false, true, false, false),
             QStringLiteral("Alt+Tab"));
    QCOMPARE(WinKeyCapture::portableFromVk(VK_RIGHT, false, true, false, false),
             QStringLiteral("Alt+Right"));
    // A bare modifier carries no key token — modifier-only combo.
    QCOMPARE(WinKeyCapture::portableFromVk(VK_MENU, false, true, false, false),
             QStringLiteral("Alt"));
}

QTEST_MAIN(TstCapture)
#include "tst_capture.moc"