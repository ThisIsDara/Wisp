#include "core/LaunchController.h"

#include "core/ResultsModel.h"

#include <QClipboard>
#include <QGuiApplication>

LaunchController::LaunchController(QObject *parent)
    : QObject(parent)
{
    // Default outcome policy (D-11/D-13): Launched → record the launch
    // (D-10) then instant dismiss; cancelled UAC → quiet no-op with zero
    // signals (the launcher stays open for the next attempt); Failed →
    // launchFailed(displayName), no dismiss.
    m_reporter = [this](const AppEntry &entry, WinLaunch::LaunchResult result) {
        switch (result) {
        case WinLaunch::LaunchResult::Launched:
            // D-10: every successful executable launch is tracked — file
            // rows and classic apps alike; UWP is skipped by the empty-path
            // guard inside recordLaunch. m_history is null until 04-05 wires
            // the real instance (no recording in tests that don't set it).
            if (m_history && !entry.targetPath.isEmpty())
                m_history->recordLaunch(entry);
            m_dismiss();
            break;
        case WinLaunch::LaunchResult::CancelledByUser:
            break; // quiet — D-11
        case WinLaunch::LaunchResult::Failed:
            emit launchFailed(entry.displayName);
            break;
        }
    };

    // Default launcher: the WinLaunch firewall (STACK). The source decides
    // the call — UWP has no elevation-verb variant (STACK), so elevated UWP
    // must never arrive here (launchEntry refuses it first, D-11).
    m_launcher = [this](const AppEntry &entry, bool elevated,
                        const ResultReporter &report) {
        const WinLaunch::LaunchResult result =
            entry.source == AppEntry::Source::Uwp ? WinLaunch::launchUwp(entry)
                                                  : WinLaunch::launchClassic(entry, elevated);
        report(entry, result);
    };

    // Default revealer: the WinLaunch firewall bridge (LAUN-03). Tests
    // inject a recording fake — no real Explorer in CI.
    m_revealer = [](const QString &path) { return WinLaunch::revealInExplorer(path); };

    // Window-light default: no-op; 03-05 wires LauncherController::hideNow()
    // (D-13) — the seam keeps tests independent of the window controller.
    m_dismiss = [] {};
}

void LaunchController::setModel(ResultsModel *model)
{
    m_model = model;
}

void LaunchController::setLauncher(Launcher fn)
{
    if (fn)
        m_launcher = std::move(fn);
}

void LaunchController::setResultReporter(ResultReporter fn)
{
    if (fn)
        m_reporter = std::move(fn);
}

void LaunchController::setDismissHandler(std::function<void()> fn)
{
    if (fn)
        m_dismiss = std::move(fn);
}

void LaunchController::setHistory(LaunchHistory *history)
{
    m_history = history;
}

void LaunchController::setRevealer(Revealer fn)
{
    if (fn)
        m_revealer = std::move(fn);
}

void LaunchController::launchSelected(bool elevated)
{
    if (!m_model || m_model->rowCount() == 0)
        return; // null-model / empty-list no-op (Test 7)
    // D-12: the target is frozen at keypress — a single value copy
    // (ResultsModel::snapshotSelected); a query/selection shift between this
    // call and the launcher execution cannot change what gets launched.
    launchEntry(m_model->snapshotSelected(), elevated);
}

void LaunchController::launchIndex(int index, bool elevated)
{
    if (!m_model || m_model->rowCount() == 0)
        return;
    m_model->selectIndex(index); // model must reflect the clicked row for the snapshot
    launchEntry(m_model->snapshotSelected(), elevated);
}

void LaunchController::launchEntry(const AppEntry &snap, bool elevated)
{
    if (snap.source == AppEntry::Source::Calculator) {
        if (QClipboard *cb = QGuiApplication::clipboard())
            cb->setText(snap.targetPath);
        // Don't dismiss — keep launcher open so user can keep calculating
        return;
    }
    if (elevated && snap.source == AppEntry::Source::Uwp) {
        // D-11: UWP/Store apps structurally cannot elevate (STACK — no
        // elevation verb for UWP). Refuse BEFORE any attempt; the 03-05 QML
        // shows the transient "Only desktop apps can run as administrator"
        // hint and the launcher stays open for the next selection.
        emit adminRequestRefused(snap.displayName);
        return;
    }
    // D-05: elevation applies only to classic apps — file/folder rows
    // (Source::File, isFolder or not) launch silently NORMAL: the default
    // launcher gets elevated=false, so the open verb is used — no hint, no
    // refusal UI. The launcher never sees an elevated file row.
    const bool effectiveElevated =
        snap.source == AppEntry::Source::File ? false : elevated;
    m_launcher(snap, effectiveElevated, m_reporter);
}

void LaunchController::revealSelected()
{
    if (!m_model || m_model->rowCount() == 0)
        return; // null-model / empty-list no-op (mirror launchSelected)
    // D-12: the target is frozen at keypress — a value copy; a selection
    // shift during the revealer call cannot change what gets revealed.
    const AppEntry snap = m_model->snapshotSelected();
    // LAUN-03 is file-only (T-04-09): Lnk/Uwp rows are quiet no-ops —
    // explorer.exe is structurally unreachable for app rows.
    if (snap.source != AppEntry::Source::File)
        return;
    const WinLaunch::LaunchResult r = m_revealer(snap.targetPath);
    switch (r) {
    case WinLaunch::LaunchResult::Launched:
        if (m_dismiss)
            m_dismiss(); // D-13: reveal success dismisses instantly
        break;
    case WinLaunch::LaunchResult::CancelledByUser:
        break; // quiet — same discipline as launchClassic
    case WinLaunch::LaunchResult::Failed:
        emit launchFailed(snap.displayName); // never a crash; no dismissal
        break;
    }
}