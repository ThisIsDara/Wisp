#include "core/Calculator.h"

#include <QJSEngine>
#include <QJSValue>
#include <QRegularExpression>

namespace {

bool isAllowedChar(QChar c)
{
    return c.isDigit() || c.isSpace() || QStringLiteral("+-*/%().^").contains(c);
}

} // namespace

QString Calculator::sanitize(const QString &query)
{
    QString s = query.trimmed();
    // JS exponent is **, not ^ — translate a^b -> a**b via pow for portability
    // (QJSEngine supports **, but Math.pow is explicit).
    // Handle "2 ^ 3" and "2^3" uniformly.
    s.replace(QRegularExpression(QStringLiteral("\\^")), QStringLiteral("**"));
    return s;
}

bool Calculator::isCalculable(const QString &query)
{
    const QString t = query.trimmed();
    if (t.isEmpty() || t.size() > 64)
        return false;

    bool hasOperator = false;
    bool hasDigit = false;
    for (QChar c : t) {
        if (!isAllowedChar(c))
            return false;
        if (QStringLiteral("+-*/%^").contains(c))
            hasOperator = true;
        if (c.isDigit())
            hasDigit = true;
    }
    if (!hasOperator || !hasDigit)
        return false;

    // Must contain at least one digit on each side of an operator is too
    // strict — "2*3" is valid, "(2)" is not calculable (no operator effect).
    // The operator check above is sufficient; evaluate() does the final gate.
    return true;
}

std::optional<QString> Calculator::evaluate(const QString &query)
{
    if (!isCalculable(query))
        return std::nullopt;

    const QString expr = sanitize(query);
    // Extra safety: after sanitization only the allow-list must remain plus *.
    for (QChar c : expr) {
        if (!isAllowedChar(c) && c != QLatin1Char('*'))
            return std::nullopt;
    }

    QJSEngine engine;
    // Prevent access to globals — evaluate pure arithmetic only.
    QJSValue result = engine.evaluate(expr);
    if (result.isError() || !result.isNumber())
        return std::nullopt;

    const double v = result.toNumber();
    if (!std::isfinite(v))
        return std::nullopt;

    return formatResult(v);
}

QString Calculator::formatResult(double value)
{
    // Integer? Show without decimal.
    if (std::floor(value) == value && std::abs(value) < 1e15)
        return QString::number(qint64(value));

    // Otherwise up to 10 decimal places, trimmed.
    QString s = QString::number(value, 'f', 10);
    while (s.contains(QLatin1Char('.')) && s.endsWith(QLatin1Char('0')))
        s.chop(1);
    if (s.endsWith(QLatin1Char('.')))
        s.chop(1);
    if (s.isEmpty())
        s = QStringLiteral("0");
    // Cap display length
    if (s.size() > 16)
        s = QString::number(value, 'g', 12);
    return s;
}
