#pragma once
#include "core/SearchProvider.h"
#include "core/FileIndex.h"
#include <functional>

class FileProvider : public SearchProvider
{
public:
    void setIndex(FileIndex *index) { m_index = index; }
    // Manual picks (LaunchHistory::addedExecutables) — searchable alongside index rows.
    using AddedSource = std::function<QVector<AppEntry>()>;
    void setAddedSource(AddedSource fn) { m_addedSource = fn; }
    QVector<ScoredEntry> query(const QString &q, int limit, bool exact) override;
private:
    FileIndex *m_index = nullptr;
    AddedSource m_addedSource;
};
