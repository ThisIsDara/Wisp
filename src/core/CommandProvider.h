#pragma once
#include "core/SearchProvider.h"

// Phase-11 typed-command runner provider (D-06/D-07): a query that starts
// with the explicit "cmd/" prefix yields ONE Command row (displayName =
// targetPath = the stripped command, scored at calc's 2000 top tier); the
// empty remainder yields a single instructional "cmd/ — type a command" row
// (score 1000, targetPath empty — never launched); anything else returns {}.
// The prefix itself is the D-12 security guard — there is no bare run-$PATH
// mode.
class CommandProvider : public SearchProvider
{
public:
    QVector<ScoredEntry> query(const QString &q, int limit, bool exact) override;
};