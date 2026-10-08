#include <QtTest>

#include <QDir>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "core/SettingsStore.h"

// SettingsStore contract (D-13/D-14/D-16): the accent value persists to the
// existing wisp INI (QSettings IniFormat, LaunchHistory makeSettings factory)
// under the non-colliding key "theme/accent"; a missing, corrupt, or
// unparseable value silently falls back to #0078D4; setAccent persists +
// syncs + notifies (Phase-6 picker path). Every suite round-trips through a
// REAL temp INI (QTemporaryDir seam) — nothing touches %APPDATA% in CI.

class TstSettings : public QObject
{
    Q_OBJECT

private slots:
    void missingKeyDefaultsTo0078D4();
    void corruptValueFallsBack();
    void setReadRoundTrip();
    void accentChangedEmitted();
    void persistsAcrossInstances();
    void invalidSetIgnored();

    // 07-05 scan settings: normalized-root persistence (Pitfall 5), empty/
    // duplicate cleaning, clamped interval (OQ4), INI durability.
    void scanRootsRoundTrip();
    void scanRootsDropsEmptyAndDuplicates();
    void scanIntervalClamped();
    void scanKeysSurviveReopen();
    void corruptRootsDoNotBecomePhantomRoots_20261008();
};

void TstSettings::missingKeyDefaultsTo0078D4()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    // Fresh INI — the key does not exist yet: D-16 default, silently.
    SettingsStore store(iniPath);
    QCOMPARE(store.accent(), QColor("#0078D4"));
}

void TstSettings::corruptValueFallsBack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    // Pre-write a corrupt value BEFORE constructing the store — the read
    // path must fall back to the default with no warning/toast (D-16).
    {
        QSettings seed(iniPath, QSettings::IniFormat);
        seed.setValue(QStringLiteral("theme/accent"), QStringLiteral("not-a-color"));
        seed.sync();
    }
    SettingsStore store(iniPath);
    QCOMPARE(store.accent(), QColor("#0078D4"));
}

void TstSettings::setReadRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    SettingsStore store(iniPath);
    store.setAccent(QColor("#E81123"));
    QCOMPARE(store.accent(), QColor("#E81123"));
}

void TstSettings::accentChangedEmitted()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    SettingsStore store(iniPath);
    QSignalSpy spy(&store, &SettingsStore::accentChanged);
    store.setAccent(QColor("#E81123"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<QColor>(), QColor("#E81123"));
}

void TstSettings::persistsAcrossInstances()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    SettingsStore store1(iniPath);
    store1.setAccent(QColor("#107C10"));

    // NEW instance on the SAME ini path → the value survived to disk
    // (persistence round-trip, not just in-memory state).
    SettingsStore store2(iniPath);
    QCOMPARE(store2.accent(), QColor("#107C10"));
}

void TstSettings::invalidSetIgnored()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    SettingsStore store(iniPath);
    QSignalSpy spy(&store, &SettingsStore::accentChanged);
    store.setAccent(QColor()); // invalid — D-16: silently ignored

    QCOMPARE(store.accent(), QColor("#0078D4")); // unchanged
    QCOMPARE(spy.count(), 0);                    // no notify for a no-op
}

// ── 07-05 scan settings ──

void TstSettings::scanRootsRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    // QFileDialog yields '/'-separated paths — Pitfall 5: they must come
    // back as native separators (the form walkAndDelta memo keys use).
    const auto desktop = QStringLiteral("C:/Users/me/Desktop");
    const auto drive = QStringLiteral("D:");
    const auto expectedNative = QStringList{ QStringLiteral("C:\\Users\\me\\Desktop"),
                                             QStringLiteral("D:") };
    SettingsStore store(iniPath);
    store.setScanRoots({ desktop, drive });
    QCOMPARE(store.scanRoots(), expectedNative);

    // 2026-10-08: the live INI came back with ZERO backslashes —
    // "roots=C:sersrickppDataoamingicrosoftindowstart Menurograms". A root
    // containing a SPACE (Program Files, AppData, "Start Menu\Programs" — most
    // real roots) is the case that matters; the space-free case above passed and
    // hid the problem.
    //
    // The encoding itself is QSettings' INI list format, which ESCAPES each
    // backslash as \\ on write and unescapes on read. Hand-editing this file with
    // single backslashes silently destroys every root — worth stating here so
    // nobody repairs the INI by hand again.
    const QStringList spaced = {
        QStringLiteral("C:/Users/me/AppData/Local/Discord"),
        QStringLiteral("C:/Program Files/Steam"),
        QStringLiteral("C:/Users/me/Downloads/Netch/Netch"),
    };
    const QStringList spacedNative = {
        QStringLiteral("C:\\Users\\me\\AppData\\Local\\Discord"),
        QStringLiteral("C:\\Program Files\\Steam"),
        QStringLiteral("C:\\Users\\me\\Downloads\\Netch\\Netch"),
    };
    store.setScanRoots(spaced);
    QCOMPARE(store.scanRoots(), spacedNative);
}

void TstSettings::scanRootsDropsEmptyAndDuplicates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    const QStringList expected = QStringList{ QStringLiteral("C:\\a"), QStringLiteral("C:\\b") };
    SettingsStore store(iniPath);
    store.setScanRoots({ QStringLiteral(""), QStringLiteral("C:/a"),
                         QStringLiteral("C:/a"), QStringLiteral("C:/b") });
    QCOMPARE(store.scanRoots(), expected);
}

// 2026-10-08: the live INI held a corrupt `roots=@Invalid()`. toStringList() on
// that plain string yields a ONE-element list, so the app gained a root literally
// named "@Invalid()" and scanned a directory that cannot exist. A corrupt or
// hand-mangled value must degrade to "no roots" (the D-09 no-locations state the
// UI already handles), never to a phantom one.
void TstSettings::corruptRootsDoNotBecomePhantomRoots_20261008()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    QSettings s(iniPath, QSettings::IniFormat);
    s.setValue(QStringLiteral("scan/roots"), QStringLiteral("@Invalid()"));
    s.sync();

    SettingsStore store(iniPath);
    QCOMPARE(store.scanRoots(), QStringList());

    // A path that lost its separators must be rejected, not scanned as-is.
    s.setValue(QStringLiteral("scan/roots"),
               QStringLiteral("C:sersrickppDataoamingicrosoft"));
    s.sync();
    QCOMPARE(store.scanRoots(), QStringList());

    // A real root still reads back. Established through the real setter so the
    // INI is encoded by QSettings itself — hand-writing the value here would
    // double-escape it, which is precisely the mistake that destroyed the live
    // roots in the first place.
    store.setScanRoots({ QStringLiteral("C:/Program Files/Steam") });
    QCOMPARE(store.scanRoots(), QStringList{QStringLiteral("C:\\Program Files\\Steam")});
}

void TstSettings::scanIntervalClamped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    SettingsStore store(iniPath);
    QCOMPARE(store.scanIntervalMinutes(), 10); // missing key → D-09 default

    store.setScanIntervalMinutes(0);
    QCOMPARE(store.scanIntervalMinutes(), 1); // OQ4 floor
    store.setScanIntervalMinutes(5000);
    QCOMPARE(store.scanIntervalMinutes(), 1440); // OQ4 ceiling
    store.setScanIntervalMinutes(10);
    QCOMPARE(store.scanIntervalMinutes(), 10);
}

void TstSettings::scanKeysSurviveReopen()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    SettingsStore store1(iniPath);
    store1.setScanRoots({ QStringLiteral("C:\\Apps") });
    store1.setScanIntervalMinutes(30);

    // NEW instance on the same ini path → both keys persisted to disk
    // (durability round-trip, not in-memory state).
    const QStringList expectedRoots = QStringList{ QStringLiteral("C:\\Apps") };
    SettingsStore store2(iniPath);
    QCOMPARE(store2.scanRoots(), expectedRoots);
    QCOMPARE(store2.scanIntervalMinutes(), 30);
}

QTEST_MAIN(TstSettings)
#include "tst_settings.moc"
