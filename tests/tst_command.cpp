#include <QtTest>

#include "core/CommandProvider.h"

// Phase-11 (D-06/D-07): pure-logic coverage of the cmd/ CommandProvider —
// prefix gate, strip, instructional row, score tiers. No OS calls, no Qt
// event loop — the provider is a plain SearchProvider.
class TstCommand : public QObject
{
    Q_OBJECT

private slots:
    void emptyQueryReturnsNothing();
    void noPrefixReturnsNothing();
    void bareCmdNoSlashReturnsNothing();
    void emptyRemainderInstructionalRow();
    void commandRowStripsPrefix();
    void commandWithSpaces();
    void caseInsensitivePrefix();
    void leadingWhitespaceTolerated();
    void limitIgnored();
};

void TstCommand::emptyQueryReturnsNothing()
{
    CommandProvider p;
    QCOMPARE(p.query(QString(), 80, false).size(), 0);
}

void TstCommand::noPrefixReturnsNothing()
{
    CommandProvider p;
    QCOMPARE(p.query(QStringLiteral("ipconfig"), 80, false).size(), 0);
    QCOMPARE(p.query(QStringLiteral("calc 1+1"), 80, false).size(), 0);
    QCOMPARE(p.query(QStringLiteral("C/dir"), 80, false).size(), 0);
}

void TstCommand::bareCmdNoSlashReturnsNothing()
{
    // The prefix is "cmd/" — without the slash there is no command surface
    // (D-06): "cmd" is ordinary app-search text, never a runner.
    CommandProvider p;
    QCOMPARE(p.query(QStringLiteral("cmd"), 80, false).size(), 0);
}

void TstCommand::emptyRemainderInstructionalRow()
{
    CommandProvider p;
    const auto rows = p.query(QStringLiteral("cmd/"), 80, false);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).entry.source, AppEntry::Source::Command);
    QCOMPARE(rows.at(0).entry.displayName, QStringLiteral("cmd/ \u2014 type a command"));
    QVERIFY(rows.at(0).entry.targetPath.isEmpty());
    QCOMPARE(rows.at(0).totalScore, 1000);
}

void TstCommand::commandRowStripsPrefix()
{
    CommandProvider p;
    const auto rows = p.query(QStringLiteral("cmd/ipconfig"), 80, false);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).entry.source, AppEntry::Source::Command);
    QCOMPARE(rows.at(0).entry.displayName, QStringLiteral("ipconfig"));
    QCOMPARE(rows.at(0).entry.targetPath, QStringLiteral("ipconfig"));
    QCOMPARE(rows.at(0).totalScore, 2000);
}

void TstCommand::commandWithSpaces()
{
    CommandProvider p;
    const auto rows = p.query(QStringLiteral("cmd/echo hello world"), 80, false);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).entry.displayName, QStringLiteral("echo hello world"));
    QCOMPARE(rows.at(0).entry.targetPath, QStringLiteral("echo hello world"));
}

void TstCommand::caseInsensitivePrefix()
{
    CommandProvider p;
    const auto rows = p.query(QStringLiteral("CMD/ver"), 80, false);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).entry.targetPath, QStringLiteral("ver"));
}

void TstCommand::leadingWhitespaceTolerated()
{
    CommandProvider p;
    const auto rows = p.query(QStringLiteral("  cmd/ver"), 80, false);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).entry.targetPath, QStringLiteral("ver"));
}

void TstCommand::limitIgnored()
{
    // The provider returns its single row regardless of limit — exact/tier
    // gates are irrelevant to a prefix provider (D-07).
    CommandProvider p;
    QCOMPARE(p.query(QStringLiteral("cmd/ver"), 0, false).size(), 1);
    QCOMPARE(p.query(QStringLiteral("cmd/ver"), 80, true).size(), 1);
}

QTEST_APPLESS_MAIN(TstCommand)
#include "tst_command.moc"