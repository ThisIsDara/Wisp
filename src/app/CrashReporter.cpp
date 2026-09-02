#include "CrashReporter.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QTextStream>

#ifndef WISP_VERSION
#define WISP_VERSION "0"
#endif

#include <windows.h>

namespace {

// Rolling cap: keep only the newest crash logs so a crash-looping session can
// never grow %APPDATA% unbounded (a crashed launcher must not become disk debt).
constexpr int kMaxLogs = 10;
// Ring buffer cap for the trailing Qt log captured into the crash file.
constexpr int kTrailLines = 256;

int g_installedOnce = 0;          // guarded by g_mutex
QMutex g_mutex;
QStringList g_trail;              // trailing Qt log lines (newest last)
QString g_logDir;                 // resolved crash dir (set on first install)

QString crashDir()
{
    return QStringLiteral("%APPDATA%/TID/wisp/crash").replace(
        QStringLiteral("%APPDATA%"), qEnvironmentVariable("APPDATA"));
}

QString logPath(const QString &stamp)
{
    return g_logDir + QStringLiteral("/wisp-crash-") + stamp + QStringLiteral(".log");
}

void writeLog(const QString &path, ULONG code, const char *module, void *addr)
{
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream ts(&out);
    ts << "=== wisp crash report ===\r\n";
    ts << "time: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\r\n";
    ts << "version: " << QStringLiteral(WISP_VERSION) << "\r\n";
    ts << "exception code: 0x" << QString::number(ulong(code), 16) << "\r\n";
    if (module && module[0])
        ts << "faulting module: " << module << "\r\n";
    if (addr)
        ts << "fault address: " << QString::number(quintptr(addr), 16) << "\r\n";
    ts << "--- trailing log ---\r\n";
    {
        QMutexLocker lock(&g_mutex);
        for (const QString &line : g_trail)
            ts << line << "\r\n";
    }
    ts.flush();
    out.close();
}

void pruneOldLogs()
{
    QDir dir(g_logDir);
    const QStringList files = dir.entryList(
        { QStringLiteral("wisp-crash-*.log") }, QDir::Files, QDir::Name);
    // Names sort chronologically (timestamp prefix); delete the oldest when
    // past the cap.
    for (int i = 0; i + kMaxLogs < files.size(); ++i)
        dir.remove(files.at(i));
}

long __stdcall crashFilter(EXCEPTION_POINTERS *ep)
{
    ULONG code = ep->ExceptionRecord->ExceptionCode;
    void *addr = ep->ExceptionRecord->ExceptionAddress;
    HMODULE mod = nullptr;
    char modName[MAX_PATH + 1] = {};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(addr), &mod);
    if (mod)
        GetModuleFileNameA(mod, modName, MAX_PATH);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    writeLog(logPath(stamp), code, modName, addr);
    // Also surface the fault line to stderr so a debug run with
    // QT_FORCE_STDERR_LOGGING captures it in the terminal too.
    qWarning("CRASH: exception 0x%08lx at 0x%p module=%s",
             code, addr, modName);
    return EXCEPTION_EXECUTE_HANDLER; // terminate the process after our log
}

// Qt message handler: keep the trailing ring buffer + forward to Qt defaults.
// The default handler may be installed before us (e.g. qInstallMessageHandler
// earlier), but the practical contract is that QApplication installs the
// default stderr handler; we re-point to that behavior via qDebug's fallback.
void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    QString line;
    switch (type) {
    case QtDebugMsg:    line = QStringLiteral("D"); break;
    case QtInfoMsg:     line = QStringLiteral("I"); break;
    case QtWarningMsg:  line = QStringLiteral("W"); break;
    case QtCriticalMsg: line = QStringLiteral("C"); break;
    case QtFatalMsg:    line = QStringLiteral("F"); break;
    }
    line += QStringLiteral(" ") +
            QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")) +
            QStringLiteral(" ") + msg;
    {
        QMutexLocker lock(&g_mutex);
        g_trail.append(line);
        while (g_trail.size() > kTrailLines)
            g_trail.removeFirst();
    }
    // Fall back to stderr (matches Qt's default handler so debug captures keep
    // working); qFatal still aborts as Qt expects.
    fprintf(stderr, "%s: %s\n", line.toLatin1().constData(),
            ctx.category ? ctx.category : "default");
}

} // namespace

namespace CrashReporter {

void install()
{
    QMutexLocker lock(&g_mutex);
    if (g_installedOnce)
        return;
    g_installedOnce = 1;

    g_logDir = crashDir();
    QDir().mkpath(g_logDir);
    pruneOldLogs(); // trim before any new one lands

    // Must be installed on the GUI thread; the filter can run on any thread.
    SetUnhandledExceptionFilter(&crashFilter);
    qInstallMessageHandler(&messageHandler);
}

} // namespace CrashReporter