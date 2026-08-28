#pragma once
#include <QString>
#include <optional>

// Pure calculator evaluation — no side effects, no JS injection surface beyond
// arithmetic. Returns the formatted result string on success, nullopt otherwise.
class Calculator
{
public:
    // "2+2*3" -> "8", "10/3" -> "3.3333333333" (trimmed), "2^8" -> "256".
    // Accepts digits, operators + - * / % ^ ( ) . and whitespace. At least one
    // operator must be present and the result must be finite.
    static std::optional<QString> evaluate(const QString &query);

    // True if evaluate() would produce a result (cheap pre-check for the model).
    static bool isCalculable(const QString &query);

private:
    static QString formatResult(double value);
    static QString sanitize(const QString &query);
};
