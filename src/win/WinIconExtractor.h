#pragma once

#include <QImage>
#include <QString>

// Classic app/file/folder icon extraction firewall (COM) — the pure
// id-parsing helpers live INLINE here so tst_icons can unit-test them
// without linking the COM code in the .cpp (Shared Pattern 2).
//
// id round trip verified lossless via QUrl percent-encoding — QML MUST
// encodeURIComponent(iconKey); provider receives the decoded id
// (spike 2026-08-10: QUrl::toPercentEncoding/fromPercentEncoding round trip
// is lossless for every special char used in iconKeys: ; : | # % space ! \).
namespace WinIconExtractor {

enum class Kind { Path, Uwp };

struct IconKey {
    Kind kind = Kind::Path;
    QString path;            // file/dir path (Path kind) — never empty for valid Path keys
    int index = -1;          // icon index in a multi-icon .exe/.dll (Path kind; -1 = default)
    QString packageFullName; // Uwp kind — full package identity (PFN)
    QString appId;           // Uwp kind — application Id from AppxManifest

    bool isValid() const
    {
        return kind == Kind::Uwp ? !packageFullName.isEmpty() && !appId.isEmpty()
                                 : !path.isEmpty();
    }

    friend bool operator==(const IconKey &a, const IconKey &b)
    {
        return a.kind == b.kind && a.path == b.path && a.index == b.index
               && a.packageFullName == b.packageFullName && a.appId == b.appId;
    }
};

// Decode a provider id ("image://wispicons/{id}" suffix, already
// percent-decoded by the engine) into an IconKey. Exact rules, IN ORDER:
//   1. empty/blank id                    → IconKey{} (isValid() false)
//   2. prefix "path:"                    → strip; Path kind, index -1
//   3. prefix "uwp:"                     → strip; split on '|':
//        packageFullName (left), appId (right); missing '|' or empty half
//        → invalid
//   4. otherwise                         → split at the LAST ';'
//        (QString::lastIndexOf); if the suffix parses as int → path = prefix,
//        index = int; else path = whole string, index = -1
// Invalid keys are never misinterpreted as a different kind (T-05-05).
inline IconKey parseKey(const QString &id)
{
    if (id.trimmed().isEmpty())
        return IconKey{};

    if (id.startsWith(QLatin1String("path:"))) {
        IconKey key;
        key.kind = Kind::Path;
        key.path = id.mid(5);
        key.index = -1;
        return key;
    }

    if (id.startsWith(QLatin1String("uwp:"))) {
        const QString rest = id.mid(4);
        const qsizetype pipe = rest.indexOf(u'|');
        if (pipe <= 0 || pipe == rest.size() - 1) // missing '|' or empty half
            return IconKey{};
        IconKey key;
        key.kind = Kind::Uwp;
        key.packageFullName = rest.left(pipe);
        key.appId = rest.mid(pipe + 1);
        return key;
    }

    // Plain path with optional ";index" suffix — split at the LAST ';'
    // (directories may contain ';'; the icon index never does).
    const qsizetype lastSemi = id.lastIndexOf(u';');
    if (lastSemi > 0) {
        bool ok = false;
        const int index = id.mid(lastSemi + 1).toInt(&ok);
        if (ok) {
            IconKey key;
            key.kind = Kind::Path;
            key.path = id.left(lastSemi);
            key.index = index;
            return key;
        }
    }

    IconKey key;
    key.kind = Kind::Path;
    key.path = id;
    key.index = -1;
    return key;
}

// Live COM extraction (defined in WinIconExtractor.cpp). Called ONLY from
// Qt's provider thread — blocking extraction is safe there; never call from
// the UI thread (STACK HIGH rule; T-05-01/T-05-02). Any failure returns a
// silent null QImage (D-16 — the monogram placeholder stays).
QImage extract(const QString &id);

} // namespace WinIconExtractor
