#include "core/UpdateService.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>

namespace {

// PATTERNS Shared Pattern 1 (SettingsStore.cpp:13-19 verbatim shape): the
// wisp INI. An explicit path is the QTemporaryDir test seam.
QSettings makeUpdateSettingsFor(const QString &path)
{
    if (path.isEmpty())
        return QSettings(QSettings::IniFormat, QSettings::UserScope,
                         QStringLiteral("TID"), QStringLiteral("wisp"));
    return QSettings(path, QSettings::IniFormat);
}

QString stripVersionPrefix(const QString &v)
{
    QString s = v.trimmed();
    if (!s.isEmpty() && (s.at(0) == QLatin1Char('v') || s.at(0) == QLatin1Char('V')))
        s.remove(0, 1);
    return s;
}

bool isHex64(const QString &s)
{
    if (s.size() != 64)
        return false;
    for (const QChar c : s) {
        const char16_t u = c.unicode();
        const bool hex = (u >= u'0' && u <= u'9') || (u >= u'a' && u <= u'f')
                         || (u >= u'A' && u <= u'F');
        if (!hex)
            return false;
    }
    return true;
}

constexpr int kChunkSize = 64 * 1024;

} // namespace

int UpdateService::compareVersions(const QString &a, const QString &b, bool *valid)
{
    if (valid)
        *valid = false;
    const QString pa = stripVersionPrefix(a);
    const QString pb = stripVersionPrefix(b);
    if (pa.isEmpty() || pb.isEmpty())
        return 0;
    const QStringList ca = pa.split(QLatin1Char('.'));
    const QStringList cb = pb.split(QLatin1Char('.'));
    const int n = qMax(ca.size(), cb.size());
    for (int i = 0; i < n; ++i) {
        bool oka = true, okb = true;
        long long va = 0, vb = 0;
        if (i < ca.size())
            va = ca.at(i).toLongLong(&oka);
        if (i < cb.size())
            vb = cb.at(i).toLongLong(&okb);
        // Empty or non-numeric component ("v0.", "v0.x1") -> invalid tag.
        if ((i < ca.size() && !oka) || (i < cb.size() && !okb))
            return 0; // empty or non-numeric component -> invalid tag
        if (va != vb) {
            if (valid)
                *valid = true; // components parsed fine - ordering is trustworthy
            return va < vb ? -1 : 1;
        }
    }
    if (valid)
        *valid = true;
    return 0;
}

QHash<QString, QString> UpdateService::parseChecksums(const QString &sumsText)
{
    QHash<QString, QString> out;
    const QStringList lines = sumsText.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        // sha256sum layout: "<hash><ws*><name>" — hash must be exactly 64 hex.
        const qsizetype sep = line.indexOf(QLatin1Char(' '));
        if (sep <= 0)
            continue;
        const QString hash = line.left(sep);
        QString name = line.mid(sep).trimmed();
        if (!isHex64(hash) || name.isEmpty())
            continue;
        out.insert(name, hash);
    }
    return out;
}

bool UpdateService::verifySha256(const QString &filePath, const QString &expectedHex)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    char buf[kChunkSize];
    while (!f.atEnd()) {
        const qint64 got = f.read(buf, kChunkSize);
        if (got < 0)
            return false;
        hasher.addData(QByteArrayView(buf, int(got)));
    }
    f.close();
    const QString actual = QString::fromLatin1(hasher.result().toHex());
    // Get-FileHash publishes UPPERCASE; our digest is lowercase
    // (research section 1 landmine) — compare case-insensitively.
    return actual.compare(expectedHex, Qt::CaseInsensitive) == 0;
}

UpdateService::UpdateService(const QString &appVersion, QObject *parent)
    : QObject(parent)
    , m_appVersion(appVersion)
    , m_nam(new QNetworkAccessManager(this))
{
    m_installedGate = [] {
        // D-07: installed builds only. The uninstall key is written by
        // packaging/installer.nsi:61 — dev builds never carry it.
        const QSettings key(QStringLiteral(
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\wisp"),
            QSettings::NativeFormat);
        return key.contains(QStringLiteral("UninstallString"));
    };
    m_clock = [] { return QDateTime::currentMSecsSinceEpoch(); };
}

void UpdateService::setInstalledGate(std::function<bool()> fn) { m_installedGate = std::move(fn); }

void UpdateService::setClock(std::function<qint64()> fn) { m_clock = std::move(fn); }

void UpdateService::setSettingsPath(const QString &path) { m_settingsPath = path; }

void UpdateService::setBaseUrlOverride(const QString &url) { m_baseUrlOverride = url; }

void UpdateService::setRetryBackoffMs(int firstMs, int secondMs)
{
    m_backoffFirstMs = firstMs;
    m_backoffSecondMs = secondMs;
}

void UpdateService::testEnterAvailable(const QString &version, const QString &installerUrl,
                                       const QString &sumsUrl)
{
    m_availableVersion = version;
    m_installerUrl = installerUrl;
    m_sumsUrl = sumsUrl;
    setState(State::Available);
}

QSettings UpdateService::makeUpdateSettings() const
{
    return makeUpdateSettingsFor(m_settingsPath);
}

QString UpdateService::apiUrl() const
{
    return m_baseUrlOverride.isEmpty()
               ? QStringLiteral("https://api.github.com/repos/ThisIsDara/Wisp/releases/latest")
               : m_baseUrlOverride;
}

void UpdateService::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

bool UpdateService::shouldAutoCheck()
{
    QSettings ini = makeUpdateSettings();
    const qint64 last = static_cast<qint64>(
        ini.value(QStringLiteral("updates/lastCheckMs"), 0).toLongLong());
    const qint64 now = m_clock();
    // Tampered/future timestamps read as "never checked" (T-08-04 clamp):
    if (last < 0 || last > now + kCheckIntervalMs)
        return true;
    return (now - last) >= kCheckIntervalMs;
}

void UpdateService::checkForUpdates(bool force)
{
    // D-07 gate FIRST: dev/portable builds never phone GitHub (T-08-03).
    if (!m_installedGate())
        return;
    if (!force && !shouldAutoCheck())
        return;

    {
        QSettings ini = makeUpdateSettings();
        ini.setValue(QStringLiteral("updates/lastCheckMs"), m_clock());
        ini.sync(); // persist BEFORE the request: a crash mid-check still counts as a check
    }

    setState(State::Checking);

    QNetworkRequest req{QUrl(apiUrl())};
    req.setRawHeader("User-Agent", "wisp/" + m_appVersion.toUtf8()); // GitHub rejects header-less clients
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(15000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy); // never https -> http

    m_activeReply = m_nam->get(req);
    connect(m_activeReply, &QNetworkReply::finished, this, &UpdateService::onCheckFinished);
}

void UpdateService::onCheckFinished()
{
    QNetworkReply *reply = m_activeReply;
    m_activeReply = nullptr;
    reply->deleteLater();

    if (m_state != State::Checking) {
        // A newer operation superseded this one (rapid manual clicks).
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        setState(State::CheckFailed);
        // Auto path stays silent (D-10); manual consumers render inline text.
        emit checkFailed(reply->errorString());
        qWarning("UpdateService: release check failed: %s", qPrintable(reply->errorString()));
        return;
    }

    const QByteArray body = reply->readAll();
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    const QJsonObject root = doc.object();
    const QString tag = root.value(QStringLiteral("tag_name")).toString();
    if (parseError.error != QJsonParseError::NoError || doc.isNull() || tag.isEmpty()) {
        setState(State::CheckFailed);
        emit checkFailed(QStringLiteral("unexpected response from GitHub"));
        qWarning("UpdateService: malformed releases payload");
        return;
    }

    bool validTag = false;
    const int cmp = compareVersions(tag, m_appVersion, &validTag);
    if (!validTag) {
        // Garbage tag -> "no update" + log, never crash (plan note).
        setState(State::UpToDate);
        qWarning("UpdateService: unparseable tag_name '%s'", qPrintable(tag));
        return;
    }

    for (const QJsonValue &av : root.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject a = av.toObject();
        const QString name = a.value(QStringLiteral("name")).toString();
        const QString url = a.value(QStringLiteral("browser_download_url")).toString();
        if (name == QLatin1String("wisp-setup.exe"))
            m_installerUrl = url;
        else if (name == QLatin1String("SHA256SUMS"))
            m_sumsUrl = url;
    }

    if (cmp > 0) {
        m_availableVersion = stripVersionPrefix(tag);
        setState(State::Available);
        emit updateAvailable(m_availableVersion);
    } else {
        m_availableVersion.clear();
        setState(State::UpToDate);
        emit upToDate();
    }
}

void UpdateService::downloadAndInstall()
{
    if (m_state != State::Available || m_installerUrl.isEmpty() || m_sumsUrl.isEmpty())
        return;
    m_attempt = 1;
    startSumsFetch();
}

void UpdateService::startSumsFetch()
{
    setState(State::Downloading);
    QNetworkRequest req{QUrl(m_sumsUrl)};
    req.setRawHeader("User-Agent", "wisp/" + m_appVersion.toUtf8());
    req.setTransferTimeout(30000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy); // T-08-02
    m_activeReply = m_nam->get(req);
    connect(m_activeReply, &QNetworkReply::finished, this, &UpdateService::onSumsFinished);
}

void UpdateService::onSumsFinished()
{
    QNetworkReply *reply = m_activeReply;
    m_activeReply = nullptr;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        attemptFailure(QStringLiteral("checksum manifest fetch failed"));
        return;
    }
    m_sumsText = QString::fromUtf8(reply->readAll());

    const QHash<QString, QString> sums = parseChecksums(m_sumsText);
    const QString installerAsset = QStringLiteral("wisp-setup.exe");
    if (!sums.contains(installerAsset)) {
        attemptFailure(QStringLiteral("checksum manifest missing installer entry"));
        return;
    }
    m_expectedHash = sums.value(installerAsset);
    startInstallerFetch();
}

void UpdateService::startInstallerFetch()
{
    const QString tempDir = QDir::tempPath();
    // Versioned name keeps prior installers intact (D-13 keep-last-installer).
    m_tempInstallerPath = tempDir + QStringLiteral("/wisp-setup-%1.exe").arg(m_availableVersion);
    const QString partPath = m_tempInstallerPath + QStringLiteral(".part");

    QFile *sink = new QFile(partPath, this);
    if (!sink->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete sink;
        attemptFailure(QStringLiteral("cannot write download file"));
        return;
    }
    m_sink = sink;
    m_hasher = new QCryptographicHash(QCryptographicHash::Sha256);

    QNetworkRequest req{QUrl(m_installerUrl)};
    req.setRawHeader("User-Agent", "wisp/" + m_appVersion.toUtf8());
    req.setTransferTimeout(600000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy); // T-08-02

    m_activeReply = m_nam->get(req);
    connect(m_activeReply, &QNetworkReply::downloadProgress, this,
            &UpdateService::onInstallerDownloadProgress);
    connect(m_activeReply, &QNetworkReply::readyRead, this, &UpdateService::onInstallerReadyRead);
    connect(m_activeReply, &QNetworkReply::finished, this, &UpdateService::onInstallerFinished);
}

void UpdateService::onInstallerDownloadProgress(qint64 received, qint64 total)
{
    emit downloadProgress(received, total); // S3 determinate bar consumer
}

void UpdateService::onInstallerReadyRead()
{
    if (!m_sink || !m_activeReply)
        return;
    const QByteArray chunk = m_activeReply->read(kChunkSize);
    if (chunk.isEmpty())
        return;
    m_hasher->addData(chunk);
    if (!m_sink->write(chunk)) {
        attemptFailure(QStringLiteral("disk write failed"));
        if (m_activeReply)
            m_activeReply->abort();
    }
}

void UpdateService::onInstallerFinished()
{
    QNetworkReply *reply = m_activeReply;
    m_activeReply = nullptr;
    reply->deleteLater();

    QFile *sink = m_sink;
    QCryptographicHash *hasher = m_hasher;
    m_sink = nullptr;
    m_hasher = nullptr;

    if (!sink || !hasher) {
        // A failure path already cleaned up and scheduled a retry/give-up.
        return;
    }
    // Drain whatever readyRead did not consume (file:// replies may skip
    // readyRead entirely and buffer everything by finished-time).
    while (!reply->atEnd()) {
        const QByteArray chunk = reply->read(kChunkSize);
        if (chunk.isEmpty())
            break;
        hasher->addData(chunk);
        if (!sink->write(chunk)) {
            delete hasher;
            sink->close();
            delete sink;
            attemptFailure(QStringLiteral("disk write failed"));
            return;
        }
    }

    if (reply->error() != QNetworkReply::NoError) {
        delete hasher;
        sink->close();
        delete sink;
        attemptFailure(QStringLiteral("download interrupted"));
        return;
    }

    sink->close();
    delete sink;
    const QString partPath = m_tempInstallerPath + QStringLiteral(".part");
    // T-08-01: hash BEFORE anything downstream may execute the file.
    if (!verifySha256(partPath, m_expectedHash)) {
        delete hasher;
        // D-11 discard immediately on any mismatch.
        QFile::remove(partPath);
        attemptFailure(QStringLiteral("SHA256 verification failed"));
        return;
    }
    delete hasher;

    // Promote .part -> final name (kept per D-13 keep-last-installer).
    QFile::remove(m_tempInstallerPath);
    if (!QFile::rename(partPath, m_tempInstallerPath)) {
        attemptFailure(QStringLiteral("cannot finalize downloaded file"));
        return;
    }

    setState(State::Verified);
    emit downloadVerified(m_tempInstallerPath);
}

void UpdateService::attemptFailure(const QString &reason)
{
    if (m_sink) {
        m_sink->close();
        delete m_sink;
        m_sink = nullptr;
    }
    if (m_hasher) {
        delete m_hasher;
        m_hasher = nullptr;
    }
    // Discard partial bytes (D-11) but keep prior complete installers (D-13).
    QFile::remove(m_tempInstallerPath + QStringLiteral(".part"));

    if (m_attempt < kMaxDownloadAttempts) {
        ++m_attempt;
        scheduleRetryOrGiveUp(reason);
        return;
    }
    scheduleRetryOrGiveUp(reason); // exhausted -> terminal branch inside
}

void UpdateService::scheduleRetryOrGiveUp(const QString &reason)
{
    if (m_attempt < kMaxDownloadAttempts) {
        const int delay = (m_attempt == 2) ? m_backoffFirstMs : m_backoffSecondMs;
        QTimer::singleShot(delay, this, [this] {
            if (m_state == State::Downloading || m_state == State::DownloadFailed)
                startSumsFetch();
        });
        setState(State::Downloading); // stay in-flight across the backoff window
        return;
    }
    setState(State::DownloadFailed);
    emit downloadFailed(reason);
    emit giveUpUntilTomorrow(); // D-08: next chance is tomorrow's daily check
}
