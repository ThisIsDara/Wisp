// Phase 8 tst_updater — offline coverage of the UpdateService engine
// (08-VALIDATION.md Wave 0 deliverable). ZERO network: instance tests run
// against a closed port with injected gates/clock/paths; pure statics are
// exercised directly.

#include <QCryptographicHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "core/UpdateService.h"

using US = UpdateService;

namespace {
QString sha256Hex(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

// Instant-failure URL (nonexistent local file) — a closed TCP port can HANG
// on Windows firewall drops; file:// errors immediately through QNAM.
const char *kDeadUrl = "file:///nonexistent-wisp-test-path/asset";
} // namespace

class TestUpdateService : public QObject
{
    Q_OBJECT

private slots:

    // ── compareVersions ──
    void compareVersions_orders()
    {
        bool valid = false;
        QVERIFY(US::compareVersions(QStringLiteral("v0.1.2"), QStringLiteral("v0.1.10"), &valid) < 0);
        QVERIFY(valid);
        QCOMPARE(US::compareVersions(QStringLiteral("v0.1.3"), QStringLiteral("v0.1.3"), &valid), 0);
        QVERIFY(valid);
        QVERIFY(US::compareVersions(QStringLiteral("v0.1.10"), QStringLiteral("v0.1.9")) > 0);
        // Missing components count as zero ("v0.2" == "v0.2.0").
        QCOMPARE(US::compareVersions(QStringLiteral("v0.2"), QStringLiteral("v0.2.0")), 0);
        // Uppercase V also stripped.
        QVERIFY(US::compareVersions(QStringLiteral("V0.2.0"), QStringLiteral("v0.1.9")) > 0);
    }

    void compareVersions_rejectsGarbage()
    {
        bool valid = true;
        US::compareVersions(QString(), QStringLiteral("v0.1.0"), &valid);
        QVERIFY(!valid);
        valid = true;
        US::compareVersions(QStringLiteral("abc"), QStringLiteral("v0.1.0"), &valid);
        QVERIFY(!valid);
        valid = true;
        US::compareVersions(QStringLiteral("v0.x1"), QStringLiteral("v0.1.0"), &valid);
        QVERIFY(!valid);
    }

    // ── parseChecksums ──
    void parseChecksums_twoSpaceAndCRLF()
    {
        const QString sums = QStringLiteral(
            "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789  wisp-setup.exe\r\n"
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef  SHA256SUMS\n");
        const QHash<QString, QString> m = US::parseChecksums(sums);
        QCOMPARE(m.size(), 2);
        QCOMPARE(m.value(QStringLiteral("wisp-setup.exe")),
                 QStringLiteral("ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789"));
        QVERIFY(m.contains(QStringLiteral("SHA256SUMS")));
    }

    void parseChecksums_skipsMalformed()
    {
        const QString sums = QStringLiteral(
            "short  name.exe\n"
            "zzz  not-hex.exe\n"
            "\n"
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef ok-file.exe\n");
        const QHash<QString, QString> m = US::parseChecksums(sums);
        QCOMPARE(m.size(), 1);
        QVERIFY(m.contains(QStringLiteral("ok-file.exe")));
        QVERIFY(!m.contains(QStringLiteral("name.exe")));
        QVERIFY(US::parseChecksums(QString()).isEmpty());
    }

    // ── verifySha256 ──
    void verifySha256_passTamperAndCase()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("payload.bin"));
        const QByteArray bytes = QByteArrayLiteral("wisp test payload\n");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(bytes), qint64(bytes.size()));
        f.close();

        const QString hex = sha256Hex(bytes);
        QVERIFY(US::verifySha256(path, hex));                    // exact
        QVERIFY(US::verifySha256(path, hex.toUpper()));          // case-insensitive (research landmine)

        // Flip one byte -> mismatch.
        QFile mutated(path);
        QVERIFY(mutated.open(QIODevice::ReadWrite));
        QVERIFY(mutated.seek(0));
        QCOMPARE(mutated.write(QByteArrayLiteral("X")), qint64(1));
        mutated.close();
        QVERIFY(!US::verifySha256(path, hex));

        QVERIFY(!US::verifySha256(dir.filePath(QStringLiteral("missing.exe")), hex)); // absent file
    }

    // ── Installed-build gate (D-07 / T-08-03) ──
    void gateFalse_blocksCheck()
    {
        US svc(QStringLiteral("0.1.2"));
        int gateCalls = 0;
        svc.setInstalledGate([&gateCalls] { ++gateCalls; return false; });
        svc.setBaseUrlOverride(QLatin1String(kDeadUrl)); // instant error if ever hit (gate blocks first)

        QSignalSpy stateSpy(&svc, &US::stateChanged);
        svc.checkForUpdates(true); // force bypasses the guard, NOT the gate

        QCOMPARE(gateCalls, 1);
        QCOMPARE(stateSpy.count(), 0);                       // never left Idle
        QCOMPARE(int(svc.state()), int(US::State::Idle));
    }

    // ── Daily guard (D-decision: once-a-day startup check) ──
    void guard_skipsWithinWindow_runsAfterInterval()
    {
        US svc(QStringLiteral("0.1.2"));
        svc.setInstalledGate([] { return true; });
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        svc.setSettingsPath(dir.path() + QStringLiteral("/updates.ini"));
        qint64 now = 1'000'000'000;
        svc.setClock([&now] { return now; });
        svc.setBaseUrlOverride(QLatin1String(kDeadUrl)); // instant error, no network

        // First auto check: never checked -> runs (ends CheckFailed at port 9).
        svc.checkForUpdates(false);
        QTest::qWait(500);
        QCOMPARE(int(svc.state()), int(US::State::CheckFailed));

        // 1h later: inside the 20h window -> guard blocks (state must not move).
        now += 3600 * 1000;
        svc.checkForUpdates(false);
        QTest::qWait(200);
        QCOMPARE(int(svc.state()), int(US::State::CheckFailed));

        // Past the window: runs again.
        now += US::kCheckIntervalMs + 1000;
        svc.checkForUpdates(false);
        QTest::qWait(500);
        QCOMPARE(int(svc.state()), int(US::State::CheckFailed)); // failed again, but it RAN

        // Manual force bypasses the guard regardless of lastCheckMs.
        now += 60 * 1000; // way inside the window
        svc.checkForUpdates(true);
        QTest::qWait(500);
        QCOMPARE(int(svc.state()), int(US::State::CheckFailed));
    }

    // ── Happy-path pipeline via file:// URLs (offline) ──
    void downloadPipeline_verifiesAndKeepsInstaller()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray bytes(1024 * 512, 'w');
        const QString exePath = dir.filePath(QStringLiteral("wisp-setup.exe"));
        QFile exe(exePath);
        QVERIFY(exe.open(QIODevice::WriteOnly));
        QCOMPARE(exe.write(bytes), qint64(bytes.size()));
        exe.close();

        const QString sumsPath = dir.filePath(QStringLiteral("SHA256SUMS"));
        QFile sums(sumsPath);
        QVERIFY(sums.open(QIODevice::WriteOnly));
        const QByteArray line = sha256Hex(bytes).toUpper().toUtf8() + QByteArrayLiteral("  wisp-setup.exe\r\n");
        QCOMPARE(sums.write(line), qint64(line.size()));
        sums.close();

        US svc(QStringLiteral("0.1.2")); // older than 9.9.9 -> would be Available anyway
        svc.testEnterAvailable(QStringLiteral("9.9.9"),
                               QUrl::fromLocalFile(exePath).toString(),
                               QUrl::fromLocalFile(sumsPath).toString());

        QSignalSpy verified(&svc, &US::downloadVerified);
        svc.downloadAndInstall();
        QVERIFY(verified.wait(3000));

        const QString expected = QDir::tempPath()
                                 + QStringLiteral("/wisp-setup-9.9.9.exe");
        QCOMPARE(verified.first().at(0).toString(), expected);
        QCOMPARE(int(svc.state()), int(US::State::Verified));
        // D-13: final file kept and byte-identical; .part gone.
        QVERIFY(QFile::exists(expected));
        QVERIFY(!QFile::exists(expected + QStringLiteral(".part")));
        QVERIFY(US::verifySha256(expected, sha256Hex(bytes)));
        QFile::remove(expected); // test cleanup only
    }

    // ── Retry policy (D-08): 3 attempts then give up until tomorrow ──
    void retriesThreeTimesThenGivesUp()
    {
        US svc(QStringLiteral("0.1.2"));
        svc.setRetryBackoffMs(10, 10);
        svc.testEnterAvailable(QStringLiteral("9.9.9"),
                               QLatin1String(kDeadUrl),
                               QLatin1String(kDeadUrl));

        QSignalSpy giveUp(&svc, &US::giveUpUntilTomorrow);
        QSignalSpy failed(&svc, &US::downloadFailed);

        svc.downloadAndInstall();
        QVERIFY(giveUp.wait(4000)); // 3 refused-connection attempts + backoffs

        QCOMPARE(failed.count(), 1);
        QCOMPARE(giveUp.count(), 1);
        QCOMPARE(int(svc.state()), int(US::State::DownloadFailed));
    }
};

QTEST_MAIN(TestUpdateService)
#include "tst_updater.moc"
