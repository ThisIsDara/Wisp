#pragma once

#include "core/AppEntry.h"

// Thin Win32/COM firewall for launching (03-04): classic apps via
// ShellExecuteEx (open / runas elevation), UWP/Store apps via
// IApplicationActivationManager::ActivateApplication. Pure C++ interface —
// no QML, no QProcess (STACK "What NOT to Use": QProcess cannot elevate).
//
// COM objects are created per call; the calling thread must have an
// initialized COM apartment (the UI thread qualifies; the 03-03 worker
// shows the CoInitializeEx reuse discipline).
namespace WinLaunch {

enum class LaunchResult { Launched, CancelledByUser, Failed };

// Classic app: ShellExecuteEx on the RESOLVED .lnk target
// (entry.targetPath, already resolved by the 03-02 enumerator) with
// entry.arguments carried verbatim (lpParameters — RESEARCH §1) and
// lpDirectory = the target's parent dir, never the launcher's cwd
// (PITFALLS #13). elevated=true → runas verb + SEE_MASK_NOCLOSEPROCESS;
// the returned process handle is closed immediately, NO wait (D-13 instant
// path; STACK's "wait" was for the legacy no-handle path). A non-elevated
// `open` on an admin-required app (manifest requireAdministrator) returns
// SE_ERR_ACCESSDENIED with NO UAC prompt — Netch-class: the launcher retries
// that once via `runas` so the app opens (and a real UAC prompt shows)
// instead of failing silently.
// User-cancelled UAC (ERROR_CANCELLED) → CancelledByUser
// — a quiet no-op, never an error dialog (D-11).
LaunchResult launchClassic(const AppEntry &entry, bool elevated);

// UWP/Store: CoCreateInstance(CLSID_ApplicationActivationManager) →
// ActivateApplication(entry.aumid, nullptr, AO_NONE, &pid). No runas variant
// exists for UWP — elevation refusal happens in LaunchController BEFORE this
// is ever called (D-11). Any HRESULT failure → Failed (qWarning, no UI,
// never a crash).
LaunchResult launchUwp(const AppEntry &entry);

// Ctrl+Enter reveal (LAUN-03): explorer.exe /select,"<path>" via
// ShellExecuteExW (open verb). The path is quoted INSIDE the argument
// — unquoted, paths with spaces break /select (RESEARCH §5). Path is
// native-normalized first (a leading '/' can never be parsed as a
// switch). Same LaunchResult classification; empty path → Failed.
// explorer.exe is a system binary — lpDirectory = nullptr (never the
// launcher's cwd). No process handle is taken (SEE_MASK_FLAG_NO_UI only).
LaunchResult revealInExplorer(const QString &path);

// Phase-11 typed-command runner (D-09): cmd.exe /D /K "<command>" via
// CreateProcess in a NEW console window. The command is quoted as ONE unit
// (embedded spaces and && survive); /D skips AutoRun and /K KEEPS the console
// open after the command finishes so the output stays visible (a /C console
// closes instantly for fast commands and reads as a no-op). lpApplicationName
// is NULL so the first lpCommandLine token resolves through PATH — a bare
// name there fails with ERROR_FILE_NOT_FOUND (see WinLaunch.cpp). cwd = the
// user-profile dir — never the launcher's cwd (WinLaunch cwd discipline,
// PITFALLS #13 analog). Empty command → Failed. No elevation variant (runas
// deferred — the LaunchController Command branch never requests it). The new
// console owns the child; the process/thread handles are closed immediately,
// no wait (D-13 instant path). Known limitation: a literal double-quote inside
// the command can break cmd /K "…" parsing (the documented two-quote rule) —
// accepted; command rows are single-line text.
LaunchResult launchCommand(const QString &command);

} // namespace WinLaunch