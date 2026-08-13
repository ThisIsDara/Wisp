#include <QtTest>

#include <QRegularExpression>

#include "core/AutostartManager.h"

// AutostartManager contract (D-10/D-12, SYS-02): "start with Windows" state
// lives in the Run key value "wisp" with the EXACT quoted form
// "\"<exe>\" --autostart". The injection seam points the manager at a
// test-only key (HKEY_CURRENT_USER\Software\wisp-tests\Run) — the suite
// writes/removes REAL registry values under that scoped key and deletes the
// whole key in cleanup(); %APPDATA% and the real Run key are never touched.
//
// NOTE: NativeFormat registry keys under HKCU are session-local to the user;
// the wisp-tests key is ours and cleanup() guarantees no residue.
class TstAutostart : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void initiallyDisabled();
    void enableWritesQuotedValue();
    void disableRemovesValue();

private:
    static QString testKeyPath();
};

QString TstAutostart::testKeyPath()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\wisp-tests\\Run");
}

void TstAutostart::init()
{
    // Fresh, empty test key before every test — a leftover from a failed
    // previous run must never leak state into the next test.
    cleanup();
}

void TstAutostart::cleanup()
{
    // Remove the WHOLE wisp-tests key tree so no test residue survives (the
    // Run seam's parent gets created as a side effect of the first write and
    // must not linger after the suite). QSettings NativeFormat treats child
    // registry keys as groups — remove() from the parent deletes the tree.
    QSettings parent(QStringLiteral("HKEY_CURRENT_USER\\Software"),
                     QSettings::NativeFormat);
    parent.remove(QStringLiteral("wisp-tests"));
    parent.sync();
}

void TstAutostart::initiallyDisabled()
{
    AutostartManager manager(testKeyPath());
    QVERIFY(!manager.isEnabled()); // fresh key, no "wisp" value (D-16: silent false)
}

void TstAutostart::enableWritesQuotedValue()
{
    AutostartManager manager(testKeyPath());
    manager.setEnabled(true);

    QVERIFY(manager.isEnabled());

    // Read back through an independent QSettings on the SAME test key — the
    // value must be exactly the D-12 form: quoted exe path + space +
    // --autostart. The plan's regex is anchored on the test binary's own
    // applicationFilePath() so it can never drift from what setEnabled wrote
    // (a fixed "wisp.exe" literal would fail because the test binary is
    // tst_autostart.exe, not wisp.exe).
    QSettings readback(testKeyPath(), QSettings::NativeFormat);
    const QString value = readback.value(QStringLiteral("wisp")).toString();
    const QString expected = QLatin1Char('"')
        + QCoreApplication::applicationFilePath() + QStringLiteral("\" --autostart");
    QCOMPARE(value, expected); // exact equality — the strongest D-12 check

    // Shape check (plan's intent): starts with a quote, non-empty path ending
    // in .exe, space, --autostart, closing quote.
    QRegularExpression shape(QStringLiteral("^\".*\\.exe\" --autostart$"));
    QVERIFY2(shape.match(value).hasMatch(), qPrintable(value));
}

void TstAutostart::disableRemovesValue()
{
    AutostartManager manager(testKeyPath());
    manager.setEnabled(true);
    QVERIFY(manager.isEnabled());

    manager.setEnabled(false);
    QVERIFY(!manager.isEnabled());

    // The value must be GONE from disk, not merely state-hidden.
    QSettings readback(testKeyPath(), QSettings::NativeFormat);
    QVERIFY(!readback.contains(QStringLiteral("wisp")));
}

QTEST_MAIN(TstAutostart)
#include "tst_autostart.moc"
