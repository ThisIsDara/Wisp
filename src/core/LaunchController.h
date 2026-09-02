#pragma once

#include <QObject>
#include <QPointer>
#include <functional>

#include "core/AppEntry.h"
#include "core/LaunchHistory.h"
#include "win/WinLaunch.h"

class ResultsModel;

// Launch policy for the resident launcher (03-04) — the D-11..D-13 contract:
//   D-12: launch targets are snapshotted at keypress (snapshotSelected, a
//         value copy) — a result shift mid-launch can never launch the wrong
//         app.
//   D-11: elevation is refused for UWP/Store entries BEFORE any launch
//         attempt (adminRequestRefused → 03-05's transient hint); a
//         user-cancelled UAC prompt is a quiet no-op (no signals, no UI).
//   D-13: a successful launch dismisses instantly via the injectable dismiss
//         handler (03-05 wires LauncherController::hideNow()); default no-op.
//
// Window-light by construction (PATTERNS §2): the OS calls live behind the
// injectable Launcher (default: WinLaunch bridge), and outcome classification
// flows through the injectable ResultReporter (default: maps LaunchResult →
// signals + dismissal). Tests inject recording fakes — no real apps, no real
// UAC prompts.
class LaunchController : public QObject
{
    Q_OBJECT

public:
    // Outcome classification seam (D-11/D-13 policy mapping). Default:
    //   Launched          → invoke the dismiss handler (hideNow, D-13)
    //   CancelledByUser   → silent no-op (quiet UAC-cancel, D-11)
    //   Failed            → emit launchFailed(displayName)
    using ResultReporter = std::function<void(const AppEntry &, WinLaunch::LaunchResult)>;

    // The launcher executes the actual process/activation call and reports
    // the outcome through the controller's ResultReporter (passed in at call
    // time so classification policy stays controller-owned). Default =
    // WinLaunch bridge (launchClassic / launchUwp by source).
    using Launcher = std::function<void(const AppEntry &, bool elevated,
                                        const ResultReporter &report)>;

    explicit LaunchController(QObject *parent = nullptr);

    // 03-05 wires the live model; the D-12 snapshot source. Null-safe
    // (launchSelected/launchIndex are no-ops without a model).
    void setModel(ResultsModel *model);
    // Injectable launch call; default = WinLaunch bridge (PATTERNS §2).
    void setLauncher(Launcher fn);
    // Injectable outcome classification; default = signals + dismiss mapping.
    void setResultReporter(ResultReporter fn);
    // Injectable dismissal; default no-op; 03-05 wires
    // LauncherController::hideNow() (D-13 instant path).
    void setDismissHandler(std::function<void()> fn);

    // Enter / Ctrl+Shift+Enter on the CURRENT selection — the D-12 snapshot
    // is taken here, at keypress time.
    Q_INVOKABLE void launchSelected(bool elevated = false);
    // Mouse click path: explicit row, independent of the keyboard selection
    // (selection is updated first so the snapshot reflects the click).
    Q_INVOKABLE void launchIndex(int index, bool elevated = false);

    // ── Phase-4 additions (04-03): file/folder launch policy ──
    // D-10: launch-tracking store; default null (no recording). Real
    // instance wired in main.cpp (04-05). Not owned by the controller.
    void setHistory(LaunchHistory *history);
    // LAUN-03 Ctrl+Enter seam; default = WinLaunch::revealInExplorer
    // bridge. Tests inject a recording fake — no real Explorer.
    using Revealer = std::function<WinLaunch::LaunchResult(const QString &path)>;
    void setRevealer(Revealer fn);
    // Phase-11 (D-09): typed-command runner seam — default = the WinLaunch
    // firewall (launchCommand). Tests inject recording fakes — no real
    // cmd.exe, no real consoles.
    using CommandRunner = std::function<WinLaunch::LaunchResult(const QString &command)>;
    void setCommandRunner(CommandRunner fn);
    // Ctrl+Enter on the CURRENT selection (D-12 snapshot at keypress):
    // Source::File rows (files AND folders) → reveal the containing
    // folder; Lnk/Uwp rows → quiet no-op (LAUN-03 is file-only). Success
    // dismisses (D-13); failure emits launchFailed; cancel is quiet.
    Q_INVOKABLE void revealSelected();

signals:
    // D-11: elevation was requested for a UWP/Store entry — refused. The
    // 03-05 QML shows a transient status hint; the launcher stays open.
    void adminRequestRefused(const QString &displayName);
    // A launch attempt failed (HRESULT / ShellExecuteEx error or empty
    // target/aumid). No dismissal — the user can pick another row.
    void launchFailed(const QString &displayName);

private:
    void launchEntry(const AppEntry &snap, bool elevated);

    QPointer<ResultsModel> m_model;
    Launcher m_launcher;
    ResultReporter m_reporter;
    std::function<void()> m_dismiss;
    LaunchHistory *m_history = nullptr;
    Revealer m_revealer;
    CommandRunner m_commandRunner;
};