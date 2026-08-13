#include <QtTest>

#include <QSignalSpy>

#include "win/WinSingleInstance.h"

// WinSingleInstance contract (D-09, SYS-01): the session-local named mutex
// admits exactly one instance (duplicate acquisition -> false); the named
// auto-reset event delivers "show the launcher" from a duplicate to the
// surviving instance's watcher. Same-process duplicate CreateMutexW with the
// same name yields ERROR_ALREADY_EXISTS, so the whole channel is testable
// through the pure interface — no Windows.h, no cross-process harness.
//
// NOTE: the named mutex/event are session-local kernel objects; while these
// tests run, a concurrently running wisp.exe instance would break them. That
// is an environment caveat, not a test bug (the app itself is expected to be
// closed during the unit suite).
class TstSingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void acquireFirstThenDuplicateRejected();
    void signalShowSetsEvent();
};

void TstSingleInstance::acquireFirstThenDuplicateRejected()
{
    WinSingleInstance first;
    QVERIFY(first.tryAcquire()); // creates the gate — this "process" is first

    // Second object, same names, same process: ERROR_ALREADY_EXISTS surfaces
    // as false through the pure interface (the real second PROCESS hits the
    // same kernel object).
    WinSingleInstance second;
    QVERIFY(!second.tryAcquire());

    // Idempotence: a re-acquire on the same instance stays true.
    QVERIFY(first.tryAcquire());
}

void TstSingleInstance::signalShowSetsEvent()
{
    WinSingleInstance first;
    QVERIFY(first.tryAcquire());
    first.startWatching();
    QSignalSpy spy(&first, &WinSingleInstance::showRequested);

    // A "duplicate" signals the show channel; the watcher must fire. The
    // duplicate's signalShow() create-or-opens the event, SetEvent wakes the
    // watcher, auto-reset clears it — spy sees exactly one showRequested.
    WinSingleInstance second;
    QVERIFY(!second.tryAcquire());
    second.signalShow();

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() == 1, 3000);
    first.stopWatching();
}

QTEST_MAIN(TstSingleInstance)
#include "tst_singleinstance.moc"
