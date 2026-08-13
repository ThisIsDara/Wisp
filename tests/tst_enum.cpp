#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

#include "core/AppEntry.h"
#include "win/WinStartMenuEnumerator.h"
#include "win/WinUwpEnumerator.h"

#include <windows.h>
#include <shlobj.h> // CLSID_ShellLink, IShellLinkW, IPersistFile

// Enumeration contract (03-02, ROADMAP criterion 2 "no junk entries"):
// the pure decision helpers (junk filter matrix, AUMID exact format, name
// fallback never empty) and the broken-link policy proven against a
// QTemporaryDir fixture: a REAL IShellLinkW-created .lnk next to a garbage
// .lnk and an ignored .txt — the scan must survive both without aborting.
class TstEnum : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void junkFilter();
    void aumidBuilder();
    void nameFallback();
    void brokenLinkPolicy();
    void sourceTagging();

private:
    QTemporaryDir m_dir;
    QString m_validTarget;
    bool m_comInitialized = false;
};

void TstEnum::initTestCase()
{
    QVERIFY(m_dir.isValid());

    // (b) garbage .lnk — must be skipped with a log line, never crash.
    // NOTE: no "MZ" PE magic in the junk — Windows Defender's heuristic
    // writer-quarantines malformed-executable-looking files in TEMP and the
    // fixture would silently vanish (observed during 03-02 test bring-up).
    {
        QFile broken(m_dir.filePath(QStringLiteral("broken.lnk")));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("this-is-definitely-not-a-valid-shortcut\xff\xfe\x00");
        broken.write(QByteArray(512, '\x7f')); // poison the rest of the file
        broken.close();
    }

    // (c) non-lnk file — must be ignored by the *.lnk name filter.
    {
        QFile readme(m_dir.filePath(QStringLiteral("readme.txt")));
        QVERIFY(readme.open(QIODevice::WriteOnly));
        readme.write("not a shortcut");
        readme.close();
    }

    // (a) valid.lnk built programmatically via IShellLinkW (no description
    // set → the base-name fallback path is exercised deterministically).
    // notepad.exe and cmd.exe are universal on Win10/11.
    QString target = QStringLiteral("C:/Windows/System32/notepad.exe");
    if (!QFileInfo::exists(target))
        target = QFileInfo(qEnvironmentVariable("WINDIR"),
                           QStringLiteral("System32/cmd.exe"))
                     .absoluteFilePath();
    QVERIFY(QFileInfo::exists(target));
    m_validTarget = QDir::toNativeSeparators(target);

    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    m_comInitialized = (initHr == S_OK); // S_FALSE / RPC_E_CHANGED_MODE: reuse

    IShellLinkW *link = nullptr;
    QVERIFY(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr,
                                       CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&link))));
    QVERIFY(SUCCEEDED(link->SetPath(reinterpret_cast<LPCWSTR>(target.utf16()))));

    IPersistFile *persist = nullptr;
    QVERIFY(SUCCEEDED(link->QueryInterface(IID_IPersistFile,
                                           reinterpret_cast<void **>(&persist))));
    const QString lnkPath = m_dir.filePath(QStringLiteral("valid.lnk"));
    QVERIFY(SUCCEEDED(persist->Save(reinterpret_cast<LPCWSTR>(lnkPath.utf16()),
                                    TRUE)));

    persist->Release();
    link->Release();
}

void TstEnum::cleanupTestCase()
{
    if (m_comInitialized)
        CoUninitialize();
}

void TstEnum::junkFilter()
{
    QVERIFY(WinUwpEnumerator::isSkippable(true, true, true));   // framework pkg
    QVERIFY(WinUwpEnumerator::isSkippable(false, false, true)); // no AppListEntry → non-launchable
    QVERIFY(WinUwpEnumerator::isSkippable(false, true, false)); // empty display name
    QVERIFY(!WinUwpEnumerator::isSkippable(false, true, true)); // the only keep case
}

void TstEnum::aumidBuilder()
{
    QCOMPARE(WinUwpEnumerator::buildAumid(
                 QStringLiteral("Microsoft.WindowsCalculator_8wekyb3d8bbwe"),
                 QStringLiteral("Calculator")),
             QStringLiteral("Microsoft.WindowsCalculator_8wekyb3d8bbwe!Calculator"));
}

void TstEnum::nameFallback()
{
    QCOMPARE(WinUwpEnumerator::displayNameOr(QStringLiteral("Calculator"),
                                             QStringLiteral("pkg")),
             QStringLiteral("Calculator"));
    QCOMPARE(WinUwpEnumerator::displayNameOr(QString(),
                                             QStringLiteral("Microsoft.WindowsCalculator")),
             QStringLiteral("Microsoft.WindowsCalculator"));
}

void TstEnum::brokenLinkPolicy()
{
    // Garbage .lnk + ignored .txt in the fixture: exactly the ONE valid
    // entry must come back — the broken link is skipped, never fatal.
    const auto entries =
        WinStartMenuEnumerator::scanRoots(QStringList{ m_dir.path() });
    QCOMPARE(entries.size(), 1);
}

void TstEnum::sourceTagging()
{
    const auto entries =
        WinStartMenuEnumerator::scanRoots(QStringList{ m_dir.path() });
    QCOMPARE(entries.size(), 1);
    QCOMPARE(int(entries.at(0).source), int(AppEntry::Source::Lnk));
    // GetDescription is empty on a programmatically-created shortcut → the
    // RESEARCH §1 base-name fallback is exercised deterministically.
    QCOMPARE(entries.at(0).displayName, QStringLiteral("valid"));
    QCOMPARE(entries.at(0).targetPath, m_validTarget);
}

QTEST_MAIN(TstEnum)
#include "tst_enum.moc"