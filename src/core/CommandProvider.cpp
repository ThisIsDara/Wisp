#include "core/CommandProvider.h"

#include <QtGlobal>

// Phase-11 (D-06/D-07): mirrors the CalculatorProvider pattern — pure prefix
// gate, never fuzzy-scored, so the 1-2 char tier-gate in ResultsModel can
// never skip it (the row IS an exact prefix match by construction). exact is
// moot: dispatchProviderQuery always passes false, and the prefix check is
// exact anyway.
QVector<ScoredEntry> CommandProvider::query(const QString &q, int limit, bool exact)
{
    Q_UNUSED(limit) Q_UNUSED(exact)
    const QString full = q.trimmed();
    // The explicit "cmd/" prefix is the whole surface (D-06/D-12) — case-
    // insensitive so CMD/ works like cmd/.
    if (!full.startsWith(QLatin1String("cmd/"), Qt::CaseInsensitive))
        return {};

const QString cmd = full.mid(4).trimmed();
    AppEntry e;
    e.source = AppEntry::Source::Command;
    if (cmd.isEmpty()) {
        // Instructional row - targetPath empty so Enter is a quiet no-op
        // (the LaunchController Command branch guards it). Title previews the
        // template so the user sees the live format ("Command: "<typed>") as
        // they keep typing.
        e.displayName = QStringLiteral("Command: \"<type a command>\"");
        return {{e, FuzzyMatcher::Result{1000, {}}, 1000}};
    }
    // Live title mirrors the composed command ("Command: "<typed>") so the
    // row visibly tracks what is typed after cmd/ (the user-facing preview).
    e.displayName = QStringLiteral("Command: \"%1\"").arg(cmd);
    e.targetPath = cmd;
    // 2000 = calc's top tier - the command always beats app/file matches.
    return {{e, FuzzyMatcher::Result{2000, {}}, 2000}};
}