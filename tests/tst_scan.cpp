#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

#include <windows.h>

#include <memory>

#include "core/FileIndex.h"
#include "win/WinDirectoryWalk.h"

// Scan seam contract (07-01 task 3): FileIndex wired to the REAL
// WinDirectoryWalk (FindFirstFileExW) against a QTemporaryDir fixture —
// end-to-end proof that the walker's attributes reach the index filter, the
// D-06 skip list works on real trees, hidden/system bits are honored via the
// Win32 attribute path, persistence round-trips with real mtimes, and the
// index never crashes on garbage files (D-07 corruption contract).
class TstScan : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void realWalkIndexesExesAndFolders();
    void skipListSkipsRealSubtrees();
    void hiddenFileExcluded();
    void persistThenRequery();
    void corruptIndexFileTolerated();

private:
    // Recreated per test — fixtures must never leak between cases.
    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_root;
    QString m_indexPath;
};

void TstScan::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_root = FileIndex::normalize(m_dir->path());
    m_indexPath = m_dir->filePath(QStringLiteral("index.dat"));
}

void TstScan::realWalkIndexesExesAndFolders()
{
    QVERIFY(QDir().mkpath(m_root + QStringLiteral("\\sub")));
    auto writeFile = [](const QString &path, const QByteArray &data) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(data);
        return true;
    };
    QVERIFY(writeFile(m_root + QStringLiteral("\\app.exe"), "MZ"));
    QVERIFY(writeFile(m_root + QStringLiteral("\\sub\\tool.exe"), "MZ"));
    QVERIFY(writeFile(m_root + QStringLiteral("\\sub\\lib.dll"), "junk"));
    QVERIFY(writeFile(m_root + QStringLiteral("\\readme.txt"), "not indexed"));

    FileIndex index(m_indexPath);
    const auto outcome = index.walkAndDelta(QStringList{m_root}, WinDirectoryWalk::winListDirectory);
    QCOMPARE(outcome.failedListings, 0);
    QCOMPARE(outcome.dirsListed, 2);

    index.apply(outcome);
    QCOMPARE(index.entryCount(), 3); // app.exe + sub + sub\tool.exe

    const auto exes = index.queryCandidates(QStringLiteral("tool"));
    QCOMPARE(exes.size(), 1);
    QCOMPARE(exes.at(0).path, m_root + QStringLiteral("\\sub\\tool.exe"));
    QVERIFY(!exes.at(0).isFolder);

    // Subsequence prefilter: "sub" also matches sub\tool.exe (path-wide) —
    // assert the folder row is present and correctly flagged, not uniqueness.
    const auto folder = index.queryCandidates(QStringLiteral("sub"));
    bool foundFolder = false;
    for (const auto &c : folder)
        if (c.path == m_root + QStringLiteral("\\sub") && c.isFolder)
            foundFolder = true;
    QVERIFY(foundFolder);
}

void TstScan::skipListSkipsRealSubtrees()
{
    const QString junk = m_root + QStringLiteral("\\node_modules");
    QVERIFY(QDir().mkpath(junk));
    QFile f(junk + QStringLiteral("\\dep.exe"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("MZ");
    f.close();

    FileIndex index(m_indexPath);
    const auto outcome = index.walkAndDelta(QStringList{m_root}, WinDirectoryWalk::winListDirectory);
    QCOMPARE(outcome.dirsListed, 1); // root only — node_modules never descended
    QCOMPARE(outcome.added.size(), 0); // nothing indexable at the root
    QVERIFY(index.queryCandidates(QStringLiteral("dep")).isEmpty());
}

void TstScan::hiddenFileExcluded()
{
    QFile f(m_root + QStringLiteral("\\secret.exe"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("MZ");
    f.close();
    // Real Win32 attribute bit — the walker must read it from the find data.
    QVERIFY(SetFileAttributesW(reinterpret_cast<LPCWSTR>((m_root + QStringLiteral("\\secret.exe")).utf16()),
                               FILE_ATTRIBUTE_HIDDEN));

    FileIndex index(m_indexPath);
    index.apply(index.walkAndDelta(QStringList{m_root}, WinDirectoryWalk::winListDirectory));
    QCOMPARE(index.entryCount(), 0);
    QVERIFY(index.queryCandidates(QStringLiteral("secret")).isEmpty());
}

void TstScan::persistThenRequery()
{
    QFile f(m_root + QStringLiteral("\\persist.exe"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("MZ");
    f.close();

    FileIndex index(m_indexPath);
    index.apply(index.walkAndDelta(QStringList{m_root}, WinDirectoryWalk::winListDirectory));
    QCOMPARE(index.entryCount(), 1);
    QVERIFY(index.save());

    FileIndex reloaded(m_indexPath);
    QVERIFY(reloaded.load());
    QCOMPARE(reloaded.entryCount(), 1);
    QCOMPARE(reloaded.queryCandidates(QStringLiteral("persist")).size(), 1);
}

void TstScan::corruptIndexFileTolerated()
{
    QFile f(m_indexPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(4096, '\xff'));
    f.close();

    FileIndex index(m_indexPath);
    QVERIFY(!index.load()); // truncated garbage → false, never a crash
    QCOMPARE(index.entryCount(), 0);
    QVERIFY(index.queryCandidates(QStringLiteral("x")).isEmpty());
}

QTEST_MAIN(TstScan)
#include "tst_scan.moc"
