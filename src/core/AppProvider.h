#pragma once
#include "core/SearchProvider.h"
#include <QHash>
#include <QSet>
#include <QVector>
#include "core/AppEntry.h"

class AppProvider : public SearchProvider
{
public:
    void setEntries(const QVector<AppEntry> &entries,
                    const QVector<QString> &lowers,
                    const QVector<QVector<char>> &bounds);
    void setMeta(const QSet<QString> &favIds, bool showHidden, bool favOnly,
                 const QHash<QString,int> &frecencyMap);
    QVector<ScoredEntry> query(const QString &q, int limit, bool exact) override;

private:
    QVector<AppEntry> m_entries;
    QVector<QString> m_lowers;
    QVector<QVector<char>> m_bounds;
    QSet<QString> m_favIds;
    bool m_showHidden = false;
    bool m_favOnly = false;
    QHash<QString,int> m_frecency;
};
