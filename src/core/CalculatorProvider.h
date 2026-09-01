#pragma once
#include "core/SearchProvider.h"

class CalculatorProvider : public SearchProvider
{
public:
    QVector<ScoredEntry> query(const QString &q, int limit, bool exact) override;
};
