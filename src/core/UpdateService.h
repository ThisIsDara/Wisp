#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <functional>

class QNetworkAccessManager;
class QCryptographicHash;
class QNetworkReply;
class QFile;

// Phase 8 auto-update engine (CONTEXT decisions D-05..D-11, D-15; research
// sections 1-4, 7). Owns the GitHub releases/latest check, the streamed
// installer download with SHA256 verification, the once-a-day startup guard,
// the installed-build gate, and the 3-attempt same-day retry policy.
//
// THREADING CONTRACT: everything runs on the constructing (UI) thread — the
// QNetworkAccessManager member pins reply delivery here, matching the
// UI-thread-store discipline of SettingsStore/CurationStore. Never blocking,
// never touching the hotkey path (v0.1.3 plan note).
//
// TESTABILITY CONTRACT: zero network in tests. All decision points are
// injected seams (installed gate, clock, INI path, base URL, backoff) and the
// pure helpers (compareVersions/parseChecksums/verifySha256) are static.
class UpdateService : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,          // nothing in flight
        Checking,      // releases/latest request in flight
        Available,     // newer release found - waiting on user/policy
        UpToDate,      // checked, running latest
        CheckFailed,   // check could not complete (offline/rate/parse)
        Downloading,   // SHA256SUMS + installer downloading
        Verified,      // hash OK - installerPath ready for hand-off
        DownloadFailed // attempts exhausted (or terminal verification miss)
    };
    Q_ENUM(State)

    // "once a day" guard window; slightly under 24h so a fixed boot time
    // still re-checks each calendar day.
    static constexpr qint64 kCheckIntervalMs = 20 * 3600 * 1000;
    // D-08: up to 3 same-day attempts, then give up until tomorrow.
    static constexpr int kMaxDownloadAttempts = 3;

    explicit UpdateService(const QString &appVersion, QObject *parent = nullptr);

    // ── Pure statics (tst_updater exercises these directly, offline) ──

    // -1 if a<b, 0 equal, 1 if a>b. Strips ONE leading 'v'/'V'. Numeric
    // per-component; missing components count as 0 ("v0.2" == "v0.2.0").
    // Any non-numeric component or empty string sets *valid=false and
    // returns 0 - callers treat invalid as "no update", never crash.
    static int compareVersions(const QString &a, const QString &b, bool *valid = nullptr);

    // Parses sha256sum-style lines "<64-hex><ws*><name>" (two spaces is the
    // producer convention from Get-FileHash; any whitespace run accepted).
    // Malformed lines skipped silently. Hash keys kept as-written - compare
    // CaseInsensitive downstream (Get-FileHash emits UPPERCASE, our digest
    // is lowercase; research section 1 landmine).
    static QHash<QString, QString> parseChecksums(const QString &sumsText);

    // Streams filePath through QCryptographicHash::Sha256 in 64KiB chunks and
    // compares hex output against expectedHex case-insensitively. Missing or
    // unreadable file -> false. True ONLY on full read + match.
    static bool verifySha256(const QString &filePath, const QString &expectedHex);

    // ── Seams (test injection; defaults are the real implementations) ──

    // D-07 installed-build gate. Default: HKCU Uninstall\wisp key contains
    // UninstallString (written by packaging/installer.nsi).
    void setInstalledGate(std::function<bool()> fn);
    // Milliseconds-since-epoch clock for the daily guard. Default: QDateTime.
    void setClock(std::function<qint64()> fn);
    // Non-empty -> INI at this exact path (QTemporaryDir test seam, mirrors
    // SettingsStore's ctor seam). Empty -> default UserScope TID/wisp INI.
    void setSettingsPath(const QString &path);
    // Test hook: override the API base URL (closed-port retry testing).
    void setBaseUrlOverride(const QString &url);
    // Test hook: shrink the retry backoff schedule (ms between attempts).
    void setRetryBackoffMs(int firstMs, int secondMs);

    // Test-only: force Available with explicit asset URLs. Offline pipeline
    // tests pass file:/// URLs; retry tests pass a closed port. Never call
    // from production code paths.
    void testEnterAvailable(const QString &version, const QString &installerUrl,
                            const QString &sumsUrl);

    // ── Operations ──

    // force=true bypasses the daily guard (manual Check-for-updates button,
    // D-10 manual path). Gate-false -> silent no-op (T-08-03).
    Q_INVOKABLE void checkForUpdates(bool force);
    // Begins SHA256SUMS + installer download; only meaningful in Available.
    Q_INVOKABLE void downloadAndInstall();

    State state() const { return m_state; }
    QString availableVersion() const { return m_availableVersion; }

signals:
    void stateChanged();
    void updateAvailable(const QString &version);
    void upToDate();
    void checkFailed(const QString &reason);              // auto path: log-only consumer
    void downloadProgress(qint64 received, qint64 total); // S3 determinate bar consumer
    void downloadVerified(const QString &installerPath);  // hash-OK hand-off (08-04)
    void downloadFailed(const QString &reason);
    void giveUpUntilTomorrow(); // D-08 terminal state after 3 attempts

private slots:
    void onCheckFinished();
    void onSumsFinished();
    void onInstallerDownloadProgress(qint64 received, qint64 total);
    void onInstallerReadyRead();
    void onInstallerFinished();

private:
    void setState(State s);
    bool shouldAutoCheck();
    class QSettings makeUpdateSettings() const;
    QString apiUrl() const;
    void startSumsFetch();
    void startInstallerFetch();
    void attemptFailure(const QString &reason);
    void scheduleRetryOrGiveUp(const QString &reason);

    QString m_appVersion;
    State m_state = State::Idle;
    QString m_availableVersion;
    QString m_installerUrl;
    QString m_sumsUrl;

    QNetworkAccessManager *m_nam = nullptr;
    std::function<bool()> m_installedGate;
    std::function<qint64()> m_clock;
    QString m_settingsPath;
    QString m_baseUrlOverride;
    int m_backoffFirstMs = 5000;
    int m_backoffSecondMs = 15000;

    int m_attempt = 0;
    QString m_sumsText;          // fetched checksum manifest
    QString m_expectedHash;      // parsed expectation for the installer asset
    QString m_tempInstallerPath; // %TEMP%/wisp-setup-<version>.exe (kept per D-13)

    QNetworkReply *m_activeReply = nullptr;
    QFile *m_sink = nullptr;                // streaming .part sink for the installer
    QCryptographicHash *m_hasher = nullptr; // digest accumulated while writing
};
