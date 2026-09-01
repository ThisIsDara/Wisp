#include "core/CalculatorProvider.h"
#include "core/Calculator.h"

QVector<ScoredEntry> CalculatorProvider::query(const QString &q, int limit, bool exact)
{
    Q_UNUSED(limit) Q_UNUSED(exact)
    auto res = Calculator::evaluate(q);
    if (!res) return {};
    AppEntry e;
    e.source = AppEntry::Source::Calculator;
    e.displayName = q.trimmed() + QStringLiteral(" = ") + *res;
    e.targetPath = *res;
    FuzzyMatcher::Result r{2000, {}};
    return {{e, r, 2000}};
}
