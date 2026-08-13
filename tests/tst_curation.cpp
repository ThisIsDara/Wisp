#include <QtTest>

#include <QTemporaryDir>

#include "core/AppEntry.h"
#include "core/CurationRules.h"
#include "core/CurationStore.h"

// Curation contract (CUR-01..CUR-04): CurationRules is the pure default-rule
// matcher over a curated ALLOWLIST of ~280 well-known popular apps (game
// launchers, famous games, messengers, media, browsers, productivity/dev
// tools, VPNs, utilities, well-known Store apps). An entry is visible when
// its displayName OR its targetPath basename (Lnk rows) contains an allowlist
// token at a word boundary (case-insensitive); everything else is hidden by
// default. Source::File rows are NEVER curated (CUR-04 escape hatch). UWP
// rows match by name only (no path). CurationStore persists per-row
// hide/show overrides to the wisp INI ("curationHidden"/"curationShown"
// groups) with last-action-wins; ids are targetPath (Lnk) / aumid (UWP) — an
// id whose app was uninstalled is inert. Every store suite round-trips
// through a REAL temp INI (QTemporaryDir seam, tst_settings.cpp pattern) —
// nothing touches %APPDATA% in CI.

namespace {

AppEntry lnkEntry(const QString &name,
                  const QString &targetPath = QStringLiteral("C:\\apps\\x.exe"))
{
    AppEntry e;
    e.source = AppEntry::Source::Lnk;
    e.displayName = name;
    e.targetPath = targetPath;
    return e;
}

AppEntry uwpEntry(const QString &name)
{
    AppEntry e;
    e.source = AppEntry::Source::Uwp;
    e.displayName = name;
    e.aumid = QStringLiteral("SomeFamily!SomeAppId");
    return e;
}

AppEntry fileEntry(const QString &name, const QString &targetPath)
{
    AppEntry e;
    e.source = AppEntry::Source::File;
    e.displayName = name;
    e.targetPath = targetPath;
    return e;
}

} // namespace

class TstCuration : public QObject
{
    Q_OBJECT

private slots:
    void allowlistedAppsVisible_CUR01();
    void nonAllowlistedHidden_CUR01();
    void basenameMatch_CUR01();
    void uwpNameOnlyMatch_CUR01();
    void fileRowsNeverCurated_CUR01();
    void hidePersistsAcrossInstances_CUR02();
    void lastActionWins();
    void missingGroupReturnsEmpty();
    void uninstalledIdInert_CUR02();
    void uwpHideByAumid_CUR02();
};

void TstCuration::allowlistedAppsVisible_CUR01()
{
    // The curated allowlist (CONTEXT.md reference apps + checkpoint list) —
    // none of these may be hidden by a default rule. Punctuation-heavy names
    // ("7-Zip", "Battle.net", "EA app") exercise the normalization path.
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Discord"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Spotify"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Steam"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("OBS Studio"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Obsidian"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("7-Zip"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("WinRAR"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Ollama"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("LM Studio"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Cursor"))));
    // Game launchers.
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Epic Games Launcher"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Battle.net"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Riot Client"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("GOG Galaxy"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Ubisoft Connect"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("EA app"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("CurseForge"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Xbox"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Playnite"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("ExitLag"))));
    // Messengers & media.
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Telegram"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("WhatsApp"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Slack"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Microsoft Teams"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Zoom"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("VLC"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("MPC-HC"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("HandBrake"))));
    // Browsers.
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Google Chrome"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Microsoft Edge"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Mozilla Firefox"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Opera GX"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Brave"))));
    // Productivity / dev tools.
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Visual Studio Code"),
                                             QStringLiteral("C:\\Users\\T\\AppData\\Local\\Programs\\Microsoft VS Code\\Code.exe"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Microsoft Word"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Notion"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Windows Terminal"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("CPU-Z"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Core Temp"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("HWiNFO"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("ScreenToGif"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Termius"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("PuTTY"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Aseprite"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("TeamSpeak"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Chatterino"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Moonlight"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Sunshine"))));
    // Famous games (CONTEXT.md measured list + broad popular titles).
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Lies of P"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Terraria"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Torchlight 2"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Oxygen Not Included"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Machinarium"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("shapez 2"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Cyberpunk 2077"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Elden Ring"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Minecraft"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Baldur's Gate 3"))));
    // Well-known Store apps (UWP, name-only).
    QVERIFY(!CurationRules::matches(uwpEntry(QStringLiteral("Microsoft Store"))));
    QVERIFY(!CurationRules::matches(uwpEntry(QStringLiteral("Netflix"))));
    QVERIFY(!CurationRules::matches(uwpEntry(QStringLiteral("Disney+"))));
    QVERIFY(!CurationRules::matches(uwpEntry(QStringLiteral("Spotify"))));
    QVERIFY(!CurationRules::matches(uwpEntry(QStringLiteral("WhatsApp"))));
    QVERIFY(!CurationRules::matches(uwpEntry(QStringLiteral("Instagram"))));
    QVERIFY(!CurationRules::matches(uwpEntry(QStringLiteral("Adobe Express"))));
}

void TstCuration::nonAllowlistedHidden_CUR01()
{
    // Anything NOT on the curated allowlist is hidden by default — the
    // measured noise buckets (uninstallers, dev toolchains, Windows system
    // utils, installer cruft) plus arbitrary unknown apps.
    // Bucket 1: uninstallers.
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Uninstall Foo"))));
    // Bucket 4: installer cruft.
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Release Notes"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("What is new in the latest version"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Check For Updates"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("License"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Help"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Configure Java"))));
    // Bucket 2: dev toolchains (Visual Studio 2022 stays hidden — the
    // allowlist covers VS Code, not the full IDE).
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("MSYS2 MSYS"),
                                            QStringLiteral("C:\\msys64\\usr\\bin\\bash.exe"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Qt 6.11.1"),
                                            QStringLiteral("C:\\Qt\\6.11.1\\bin\\qmake.exe"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Python 3.12"),
                                            QStringLiteral("C:\\Python312\\python.exe"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Git GUI"),
                                            QStringLiteral("C:\\Program Files\\Git\\cmd\\git-gui.exe"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Visual Studio 2022"),
                                            QStringLiteral("C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\devenv.exe"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Windows SDK"),
                                            QStringLiteral("C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.22621.0\\x64\\makeappx.exe"))));
    // Bucket 3: Windows system utils.
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Control Panel"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Command Prompt"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Windows PowerShell"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Registry Editor"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Event Viewer"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Task Scheduler"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Disk Cleanup"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Services"))));
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("cmd"),
                                            QStringLiteral("C:\\Windows\\System32\\cmd.exe"))));
    // Unknown / non-curated apps of any kind.
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Mystery App X"))));
    QVERIFY(CurationRules::matches(uwpEntry(QStringLiteral("Windows Calculator"))));
    QVERIFY(CurationRules::matches(uwpEntry(QStringLiteral("Random Store App"))));
}

void TstCuration::basenameMatch_CUR01()
{
    // Allowlist matching covers BOTH the display name and the targetPath
    // basename (no extension): a launcher whose .lnk name differs from its
    // exe still resolves to the right token.
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Game Hub"),
                                             QStringLiteral("C:\\Games\\Steam\\steam.exe"))));
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Chat Client"),
                                             QStringLiteral("C:\\apps\\discord.exe"))));
    // Basename with punctuation: "battle.net" token vs "Battle.net.exe".
    QVERIFY(!CurationRules::matches(lnkEntry(QStringLiteral("Blizzard Launcher"),
                                             QStringLiteral("C:\\Battle.net\\Battle.net.exe"))));
    // Negative: a basename NOT on the allowlist stays hidden.
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Game Hub"),
                                            QStringLiteral("C:\\Games\\someobscurelauncher.exe"))));
    // Word-boundary guard: "battle.net" must NOT match "Battle.networx"
    // (token is a whole word, not a prefix run).
    QVERIFY(CurationRules::matches(lnkEntry(QStringLiteral("Battle.networx"))));
}

void TstCuration::uwpNameOnlyMatch_CUR01()
{
    // UWP rows have no path — matching is name-only (basename path skipped).
    QVERIFY(!CurationRules::matches(uwpEntry(QStringLiteral("Netflix"))));
    QVERIFY(CurationRules::matches(uwpEntry(QStringLiteral("Random Store App"))));
}

void TstCuration::fileRowsNeverCurated_CUR01()
{
    // CUR-04 escape hatch: Source::File rows are NEVER curated, even when
    // their name is not on the allowlist — matches() must return false.
    QVERIFY(!CurationRules::matches(fileEntry(QStringLiteral("random_tool.exe"),
                                              QStringLiteral("C:\\apps\\random_tool.exe"))));
    QVERIFY(!CurationRules::matches(fileEntry(QStringLiteral("some_game_launcher.exe"),
                                              QStringLiteral("C:\\apps\\some_game_launcher.exe"))));
}

void TstCuration::hidePersistsAcrossInstances_CUR02()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    CurationStore store(iniPath);
    store.hide(QStringLiteral("C:\\apps\\Noise.exe"));

    // NEW instance on the SAME ini path → the hide survived to disk
    // (persistence round-trip, not just in-memory state — CUR-02).
    CurationStore reloaded(iniPath);
    QVERIFY(reloaded.hiddenIds().contains(QStringLiteral("C:\\apps\\Noise.exe")));
}

void TstCuration::lastActionWins()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    // hide then show → shown wins (the entry is visible again).
    CurationStore store(iniPath);
    store.hide(QStringLiteral("C:\\apps\\A.exe"));
    store.show(QStringLiteral("C:\\apps\\A.exe"));
    QVERIFY(store.shownIds().contains(QStringLiteral("C:\\apps\\A.exe")));
    QVERIFY(!store.hiddenIds().contains(QStringLiteral("C:\\apps\\A.exe")));

    // show then hide → hidden wins, on a FRESH store too.
    CurationStore fresh(iniPath);
    fresh.show(QStringLiteral("C:\\apps\\B.exe"));
    fresh.hide(QStringLiteral("C:\\apps\\B.exe"));
    QVERIFY(fresh.hiddenIds().contains(QStringLiteral("C:\\apps\\B.exe")));
    QVERIFY(!fresh.shownIds().contains(QStringLiteral("C:\\apps\\B.exe")));
}

void TstCuration::missingGroupReturnsEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    // Fresh INI — no curation groups yet: D-16, silently empty.
    CurationStore store(iniPath);
    QVERIFY(store.hiddenIds().isEmpty());
    QVERIFY(store.shownIds().isEmpty());
}

void TstCuration::uninstalledIdInert_CUR02()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    CurationStore store(iniPath);
    store.hide(QStringLiteral("C:\\gone\\app.exe"));

    // The key survives to disk (nothing crashes); nothing matches it — inert
    // until an app comes back under the same identity (CUR-02).
    CurationStore reloaded(iniPath);
    QVERIFY(reloaded.hiddenIds().contains(QStringLiteral("C:\\gone\\app.exe")));
}

void TstCuration::uwpHideByAumid_CUR02()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));

    // UWP identity = "PackageFamilyName!AppId" — the '!' must survive the
    // QSettings INI key round-trip.
    CurationStore store(iniPath);
    store.hide(QStringLiteral("SomeFamily!SomeAppId"));

    CurationStore reloaded(iniPath);
    QVERIFY(reloaded.hiddenIds().contains(QStringLiteral("SomeFamily!SomeAppId")));
}

QTEST_MAIN(TstCuration)
#include "tst_curation.moc"
