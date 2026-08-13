#include <QtTest>

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

QTEST_MAIN(TstSettings)
#include "tst_settings.moc"
