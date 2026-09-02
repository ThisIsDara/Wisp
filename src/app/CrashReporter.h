#pragma once

#include <QString>

// Once-only Windows crash-reporting install for the wisp launcher.
//
// Responsibilities (launcher crash-capture contract):
//  * SetUnhandledExceptionFilter — an unhandled SEH exception (access
//    violation, etc.) is caught and a readable timestamped crash log written
//    to the app-data dir BEFORE the process dies.
//  * qInstallMessageHandler — Qt warnings/critical/fatal lines are appended to
//    an in-memory ring buffer AND forwarded to Qt's default handler, so the
//    crash log carries the final messages that led up to the fault.
//  * No CAPTURE_STOP_WRAPPING dependency: only the Windows SDK + Qt Core are
//    used — no minidump, no external symbols, so the log is always available.
//
// Crash log location: %APPDATA%\TID\wisp\crash\wisp-crash-<timestamp>.log
// (same AppData base the index and settings use). A rolling cap keeps only the
// newest kMaxLogs logs so a crashing session cannot fill the disk.
namespace CrashReporter
{

// Install the exception filter + message handler. Safe to call more than once
// (subsequent calls are no-ops). Call this at the VERY TOP of main(), before
// any QApplication / window construction. Must be called on the GUI thread.
void install();

} // namespace CrashReporter