#include <QtTest>

#include <QUrl>

#include "win/WinIconExtractor.h"
#include "win/WinUwpLogo.h"

// WinIconExtractor/WinUwpLogo seam contracts (Shared Pattern 2): ONLY the
// inline pure helpers are unit-tested here — no COM/WinRT in tests
// (PATTERNS "tst_icons cases" rule; the live COM/WinRT paths are verified
// by the phase's manual UI pass). The iconKey id-encoding round trip
// (wave-0 spike) is locked by iconKeyUrlRoundTrip.

class TstIcons : public QObject
{
    Q_OBJECT

private slots:
    void parseKeyPlainPath();
    void parseKeyWithIndex();
    void parseKeyPathPrefix();
    void parseKeyUwp();
    void parseKeyInvalidUwpMissingPipe();
    void parseKeyEmptyIsInvalid();
    void iconKeyUrlRoundTrip();
    void scaleVariantForCases();
};

void TstIcons::parseKeyPlainPath()
{
    using namespace WinIconExtractor;
    const IconKey key = parseKey(QStringLiteral(R"(C:\Program Files\App\app.exe)"));
    QVERIFY(key.isValid());
    QCOMPARE(key.kind, Kind::Path);
    QCOMPARE(key.path, QStringLiteral(R"(C:\Program Files\App\app.exe)"));
    QCOMPARE(key.index, -1);
    QVERIFY(key.packageFullName.isEmpty());
    QVERIFY(key.appId.isEmpty());
}

void TstIcons::parseKeyWithIndex()
{
    using namespace WinIconExtractor;
    // Directory name contains ';' — the LAST ';' splits path from index.
    const IconKey key = parseKey(QStringLiteral(R"(C:\Prog;ram Files\x.exe;2)"));
    QVERIFY(key.isValid());
    QCOMPARE(key.kind, Kind::Path);
    QCOMPARE(key.path, QStringLiteral(R"(C:\Prog;ram Files\x.exe)"));
    QCOMPARE(key.index, 2);
}

void TstIcons::parseKeyPathPrefix()
{
    using namespace WinIconExtractor;
    const IconKey key = parseKey(QStringLiteral(R"(path:C:\a\b.txt)"));
    QVERIFY(key.isValid());
    QCOMPARE(key.kind, Kind::Path);
    QCOMPARE(key.path, QStringLiteral(R"(C:\a\b.txt)"));
    QCOMPARE(key.index, -1);
}

void TstIcons::parseKeyUwp()
{
    using namespace WinIconExtractor;
    const IconKey key =
        parseKey(QStringLiteral("uwp:PFN_8wekyb3d8bbwe|AppId"));
    QVERIFY(key.isValid());
    QCOMPARE(key.kind, Kind::Uwp);
    QCOMPARE(key.packageFullName, QStringLiteral("PFN_8wekyb3d8bbwe"));
    QCOMPARE(key.appId, QStringLiteral("AppId"));
    QVERIFY(key.path.isEmpty());
}

void TstIcons::parseKeyInvalidUwpMissingPipe()
{
    using namespace WinIconExtractor;
    QVERIFY(!parseKey(QStringLiteral("uwp:OnlyPackage")).isValid());
    QVERIFY(!parseKey(QStringLiteral("uwp:|AppOnly")).isValid());
    QVERIFY(!parseKey(QStringLiteral("uwp:Package|")).isValid());
}

void TstIcons::parseKeyEmptyIsInvalid()
{
    using namespace WinIconExtractor;
    QVERIFY(!parseKey(QStringLiteral("")).isValid());
    QVERIFY(!parseKey(QStringLiteral("   ")).isValid());
}

void TstIcons::iconKeyUrlRoundTrip()
{
    // Wave-0 spike locked here: QML sends encodeURIComponent(model.iconKey);
    // the engine percent-decodes before handing the id to requestImage
    // (QUrl::fromPercentEncoding semantics). The round trip must be lossless
    // for every special char used in iconKeys (; : | # % space ! \).
    using namespace WinIconExtractor;
    const QStringList samples = {
        QStringLiteral(R"(C:\Program Files\App.exe;0)"),
        QStringLiteral(R"(uwp:Microsoft.WindowsCalculator_8wekyb3d8bbwe|App)"),
        QStringLiteral(R"(C:\weird;name.exe;1)"),
        QStringLiteral(R"(path:C:\Users\me\My Documents\report.txt)"),
        QStringLiteral(R"(app#1%2|3;2)"),
        QStringLiteral(R"(C:\Prog;ram Files\x.exe;2)"),
    };
    for (const QString &original : samples) {
        const QByteArray encoded = QUrl::toPercentEncoding(original);
        const QString decoded = QUrl::fromPercentEncoding(encoded);
        QVERIFY2(decoded == original, "percent-encoding round trip must be lossless");
        QCOMPARE(parseKey(decoded), parseKey(original));
    }
}

void TstIcons::scaleVariantForCases()
{
    using namespace WinUwpLogo;
    const QString base = QStringLiteral(R"(C:\pkg\Assets\AppIcon.png)");
    QCOMPARE(scaleVariantFor(base, 1.0),
             QStringLiteral(R"(C:\pkg\Assets\AppIcon.scale-100.png)"));
    QCOMPARE(scaleVariantFor(base, 1.25),
             QStringLiteral(R"(C:\pkg\Assets\AppIcon.scale-125.png)"));
    QCOMPARE(scaleVariantFor(base, 1.5),
             QStringLiteral(R"(C:\pkg\Assets\AppIcon.scale-150.png)"));
    QCOMPARE(scaleVariantFor(base, 1.75),
             QStringLiteral(R"(C:\pkg\Assets\AppIcon.scale-200.png)"));
    QCOMPARE(scaleVariantFor(base, 2.0),
             QStringLiteral(R"(C:\pkg\Assets\AppIcon.scale-200.png)"));
    QCOMPARE(scaleVariantFor(base, 3.0),
             QStringLiteral(R"(C:\pkg\Assets\AppIcon.scale-300.png)"));
    QCOMPARE(scaleVariantFor(base, 3.4),
             QStringLiteral(R"(C:\pkg\Assets\AppIcon.scale-400.png)"));
    QCOMPARE(scaleVariantFor(base, 0.8),
             QStringLiteral(R"(C:\pkg\Assets\AppIcon.scale-100.png)"));
}

QTEST_MAIN(TstIcons)
#include "tst_icons.moc"
