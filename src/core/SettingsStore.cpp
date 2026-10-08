#include "core/SettingsStore.h"

#include <QDir>

namespace {

// PATTERNS Shared Pattern 1 — LaunchHistory.cpp:24-30 VERBATIM: IniFormat +
// UserScope + "TID"/"wisp" → %APPDATA%\TID\wisp\wisp.ini; an explicit path
// is the QTemporaryDir test seam. A factory is required because QSettings
// is neither copyable nor movable — a conditional-expression member-init
// would need a deleted copy (C2280); returning a prvalue uses guaranteed
// elision instead (LaunchHistory precedent, Phase 04 lesson).
QSettings makeSettings(const QString &settingsPath)
{
    if (settingsPath.isEmpty())
        return QSettings(QSettings::IniFormat, QSettings::UserScope,
                         QStringLiteral("TID"), QStringLiteral("wisp"));
    return QSettings(settingsPath, QSettings::IniFormat);
}

// 2026-10-08: a root is only accepted if it names a real filesystem location.
// QDir::isAbsolutePath() is NOT sufficient: it returns true for a DRIVE-RELATIVE
// path like "C:sersrickppData..." (drive + anything), which is exactly what a
// mis-encoded INI produces — every backslash swallowed by the reader's escape
// handling, leaving a drive letter glued to the rest of the path. Requiring a
// separator after the drive colon rejects that, while still allowing a bare
// drive root ("D:"), which is a legitimate scan target.
bool isScannableRootPath(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QChar first = path.at(0);
    if (first == QLatin1Char('/') || first == QLatin1Char('\\'))
        return true;   // rooted — UNC, or absolute on the current drive
    if (path.size() == 2 && path.at(1) == QLatin1Char(':'))
        return true;   // bare drive root, e.g. "D:"
    if (path.size() >= 3 && path.at(1) == QLatin1Char(':')
        && (path.at(2) == QLatin1Char('\\') || path.at(2) == QLatin1Char('/')))
        return true;   // drive + absolute path
    return false;
}

} // namespace

SettingsStore::SettingsStore(const QString &settingsPath, QObject *parent)
    : QObject(parent)
    , m_settings(makeSettings(settingsPath))
{
}

QColor SettingsStore::accent() const
{
    // Live read — always fresh (an external INI edit shows up on the next
    // read); the QML binding consumes once at startup + via accentChanged.
    return readAccent();
}

void SettingsStore::setAccent(const QColor &c)
{
    if (!c.isValid())
        return; // D-16: an invalid color is silently ignored, no notify
    // c.name(): canonical "#rrggbb", or "#aarrggbb" when alpha < 255 — full
    // fidelity for Phase 6's picker; round-trips through QColor::fromString.
    m_settings.setValue(QStringLiteral("theme/accent"), c.name());
    m_settings.sync(); // LaunchHistory.cpp:48/58 discipline — sync after EVERY write
    emit accentChanged(c);
}

QColor SettingsStore::readAccent() const
{
    // D-16 semantics: missing key, corrupt string, and unparseable color ALL
    // silently return the default (no warnings, no toasts). Stored values are
    // canonical c.name() forms, so fromString succeeds on everything we write.
    const QString raw = m_settings.value(QStringLiteral("theme/accent"),
                                         QStringLiteral("#0078D4")).toString();
    QColor c = QColor::fromString(raw);
    return c.isValid() ? c : QColor(QStringLiteral("#0078D4"));
}

QStringList SettingsStore::scanRoots() const
{
    // 07-04 D-01: live read — an external edit / 07-05 Settings write is
    // picked up on the next ScanService snapshot (Pitfall 4 discipline).
    //
    // 2026-10-08: a corrupt value used to become a PHANTOM ROOT. The live INI
    // carried the literal `roots=@Invalid()` — QSettings' marker for a value it
    // could not serialize — and toStringList() on a QString yields a one-element
    // list, so the app gained a "root" literally named "@Invalid()" and scanned a
    // directory that does not exist. Every entry is now required to be an
    // absolute path, so a garbage value degrades to "no roots" (the D-09 no-locations
    // state the UI already handles) instead of a broken one.
    const QVariant raw = m_settings.value(QStringLiteral("scan/roots"));
    QStringList roots = raw.toStringList();
    QStringList sane;
    sane.reserve(roots.size());
    for (const QString &root : std::as_const(roots)) {
        const QString norm = QDir::toNativeSeparators(root).trimmed();
        if (norm.isEmpty() || !isScannableRootPath(norm))
            continue;
        if (!sane.contains(norm))
            sane.append(norm);
    }
    return sane;
}

int SettingsStore::scanIntervalMinutes() const
{
    // Silent fallback to the D-09 default (10 min) on missing/garbage values;
    // the clamp (OQ4: 1 min .. 24 h) is applied at READ so a tampered INI
    // still reads a legal value.
    return qBound(1, m_settings.value(QStringLiteral("scan/intervalMinutes"), 10).toInt(), 1440);
}

void SettingsStore::setScanRoots(const QStringList &roots)
{
    // Pitfall 5: QFileDialog returns '/'-separated paths — one normalization
    // site so memo/query keys agree across every producer. Empty entries are
    // dropped and duplicates collapsed (order preserved — UI shows insertion
    // order).
    QStringList cleaned;
    for (const QString &root : roots) {
        const QString norm = QDir::toNativeSeparators(root).trimmed();
        if (norm.isEmpty() || cleaned.contains(norm))
            continue;
        cleaned.append(norm);
    }
    m_settings.setValue(QStringLiteral("scan/roots"), cleaned);
    m_settings.sync(); // LaunchHistory.cpp:48/58 discipline — sync after EVERY write
}

void SettingsStore::setScanIntervalMinutes(int minutes)
{
    m_settings.setValue(QStringLiteral("scan/intervalMinutes"), qBound(1, minutes, 1440));
    m_settings.sync();
}

bool SettingsStore::updatesAutoInstall() const
{
    // Silent fallback: missing/garbage -> OFF (default locked in discuss-phase).
    return m_settings.value(QStringLiteral("updates/autoInstall"), false).toBool();
}

void SettingsStore::setUpdatesAutoInstall(bool on)
{
    m_settings.setValue(QStringLiteral("updates/autoInstall"), on);
    m_settings.sync(); // LaunchHistory.cpp:48/58 discipline — sync after EVERY write
}
