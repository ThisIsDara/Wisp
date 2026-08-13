#include "core/CurationRules.h"

#include <QFileInfo>
#include <QStringList>

namespace {

// Word-boundary normalization: fold case, collapse every non-alphanumeric
// run to a single space. Multi-word tokens match as whole sequences.
QString normalized(const QString &s)
{
    QString out;
    out.reserve(s.size());
    bool lastWasSpace = true;
    for (const QChar &c : s) {
        if (c.isLetterOrNumber()) { out.append(c.toCaseFolded()); lastWasSpace = false; }
        else if (!lastWasSpace) { out.append(QLatin1Char(' ')); lastWasSpace = true; }
    }
    return out;
}
bool containsWord(const QString &normalized, const QString &token)
{
    return normalized == token
        || normalized.startsWith(token + QLatin1Char(' '))
        || normalized.endsWith(QLatin1Char(' ') + token)
        || normalized.contains(QLatin1Char(' ') + token + QLatin1Char(' '));
}

// ── Curated allowlist (05.1 checkpoint feedback) ──
// The DEFAULT catalog shows a handpicked allowlist of well-known popular
// apps (~280 tokens: game launchers, famous games, messengers, media,
// browsers, productivity/dev tools, VPNs, utilities, well-known Store
// apps). An entry is visible when its displayName OR its targetPath
// basename (Lnk rows) contains any allowlist token at a word boundary
// (case-insensitive); anything else is hidden by default — the user's
// real apps are covered, the noise is not. User overrides still beat
// rules (shownIds wins before this is consulted; hiddenIds hides after).
//
// Token style: natural names ("epic games launcher", "7-zip", "cpu-z").
// Matching normalizes BOTH sides (fold case, collapse non-alnum runs to
// spaces), so punctuation/pluses never break a match. Multi-word tokens
// match as whole sequences at word boundaries ("battle.net" matches
// "Battle.net", not "Battle.networx").
const QStringList kCuratedNames = {
    // ── Game launchers & stores ──
    QStringLiteral("steam"), QStringLiteral("steamvr"), QStringLiteral("epic games launcher"),
    QStringLiteral("epic games"), QStringLiteral("battle.net"),
    QStringLiteral("blizzard battle.net"), QStringLiteral("riot client"),
    QStringLiteral("gog galaxy"), QStringLiteral("ubisoft connect"),
    QStringLiteral("ea app"), QStringLiteral("ea play"), QStringLiteral("curseforge"),
    QStringLiteral("xbox"), QStringLiteral("playnite"), QStringLiteral("itch.io"),
    QStringLiteral("itch"), QStringLiteral("wowup"), QStringLiteral("mobalytics"),
    QStringLiteral("rockstar games"), QStringLiteral("rockstar games launcher"),
    // ── Famous games (broad: user's measured list + popular titles) ──
    QStringLiteral("lies of p"), QStringLiteral("terraria"),
    QStringLiteral("torchlight 2"), QStringLiteral("oxygen not included"),
    QStringLiteral("machinarium"), QStringLiteral("shapez 2"), QStringLiteral("shapez"),
    QStringLiteral("cyberpunk 2077"), QStringLiteral("cyberpunk"),
    QStringLiteral("elden ring"), QStringLiteral("dark souls"),
    QStringLiteral("dark souls ii"), QStringLiteral("dark souls iii"),
    QStringLiteral("sekiro"), QStringLiteral("baldur's gate 3"),
    QStringLiteral("baldur's gate"), QStringLiteral("skyrim"),
    QStringLiteral("elder scrolls"), QStringLiteral("the witcher 3"),
    QStringLiteral("witcher"), QStringLiteral("red dead redemption 2"),
    QStringLiteral("red dead redemption"), QStringLiteral("gta v"),
    QStringLiteral("grand theft auto"), QStringLiteral("minecraft"),
    QStringLiteral("fortnite"), QStringLiteral("rocket league"),
    QStringLiteral("counter-strike 2"), QStringLiteral("counter strike"),
    QStringLiteral("cs2"), QStringLiteral("cs go"), QStringLiteral("dota 2"),
    QStringLiteral("overwatch"), QStringLiteral("overwatch 2"),
    QStringLiteral("apex legends"), QStringLiteral("call of duty"),
    QStringLiteral("warzone"), QStringLiteral("battlefield"), QStringLiteral("halo"),
    QStringLiteral("halo infinite"), QStringLiteral("forza"),
    QStringLiteral("forza horizon"), QStringLiteral("doom"), QStringLiteral("doom eternal"),
    QStringLiteral("stardew valley"), QStringLiteral("factorio"),
    QStringLiteral("satisfactory"), QStringLiteral("rimworld"),
    QStringLiteral("hollow knight"), QStringLiteral("celeste"), QStringLiteral("hades"),
    QStringLiteral("undertale"), QStringLiteral("outer wilds"),
    QStringLiteral("subnautica"), QStringLiteral("don't starve"),
    QStringLiteral("dead cells"), QStringLiteral("slay the spire"),
    QStringLiteral("monster hunter"), QStringLiteral("nier automata"),
    QStringLiteral("persona 5"), QStringLiteral("persona"), QStringLiteral("yakuza"),
    QStringLiteral("resident evil"), QStringLiteral("final fantasy"),
    QStringLiteral("final fantasy xiv"), QStringLiteral("ffxiv"),
    QStringLiteral("it takes two"), QStringLiteral("a way out"),
    QStringLiteral("borderlands"), QStringLiteral("far cry"),
    QStringLiteral("assassins creed"), QStringLiteral("watch dogs"),
    QStringLiteral("just cause"), QStringLiteral("mafia"), QStringLiteral("sleeping dogs"),
    QStringLiteral("days gone"), QStringLiteral("ghost of tsushima"),
    QStringLiteral("god of war"), QStringLiteral("horizon zero dawn"),
    QStringLiteral("the last of us"), QStringLiteral("uncharted"),
    QStringLiteral("spider-man"), QStringLiteral("armored core"),
    QStringLiteral("tekken"), QStringLiteral("street fighter"),
    QStringLiteral("mortal kombat"), QStringLiteral("sonic"),
    QStringLiteral("age of empires"), QStringLiteral("age of empires ii"),
    QStringLiteral("age of empires iv"), QStringLiteral("civilisation vi"),
    QStringLiteral("civilization"), QStringLiteral("total war"),
    QStringLiteral("crusader kings"), QStringLiteral("crusader kings iii"),
    QStringLiteral("hearts of iron"), QStringLiteral("hearts of iron iv"),
    QStringLiteral("europa universalis"), QStringLiteral("stellaris"),
    QStringLiteral("victoria 3"), QStringLiteral("xcom"),
    QStringLiteral("cities skylines"), QStringLiteral("planet coaster"),
    QStringLiteral("planet zoo"), QStringLiteral("the sims"), QStringLiteral("sims 4"),
    QStringLiteral("spore"), QStringLiteral("ark survival evolved"), QStringLiteral("ark"),
    QStringLiteral("valheim"), QStringLiteral("raft"), QStringLiteral("sons of the forest"),
    QStringLiteral("the forest"), QStringLiteral("green hell"), QStringLiteral("grounded"),
    QStringLiteral("conan exiles"), QStringLiteral("project zomboid"),
    QStringLiteral("kenshi"), QStringLiteral("mount & blade"), QStringLiteral("mount blade"),
    QStringLiteral("bannerlord"), QStringLiteral("warhammer"), QStringLiteral("vermintide"),
    QStringLiteral("darktide"), QStringLiteral("helldivers"), QStringLiteral("helldivers 2"),
    QStringLiteral("deep rock galactic"), QStringLiteral("payday"),
    QStringLiteral("left 4 dead"), QStringLiteral("team fortress 2"),
    QStringLiteral("pubg"), QStringLiteral("escape from tarkov"), QStringLiteral("dayz"),
    QStringLiteral("rust"), QStringLiteral("7 days to die"), QStringLiteral("starbound"),
    QStringLiteral("core keeper"), QStringLiteral("sun haven"), QStringLiteral("coral island"),
    QStringLiteral("palworld"), QStringLiteral("enshrouded"), QStringLiteral("disco elysium"),
    QStringLiteral("divinity original sin"), QStringLiteral("pathfinder"),
    QStringLiteral("pillars of eternity"), QStringLiteral("tyranny"),
    QStringLiteral("wasteland"), QStringLiteral("fallout"), QStringLiteral("fallout 4"),
    QStringLiteral("fallout 76"), QStringLiteral("starfield"), QStringLiteral("mass effect"),
    QStringLiteral("dragon age"), QStringLiteral("outer worlds"), QStringLiteral("alan wake"),
    QStringLiteral("alan wake 2"), QStringLiteral("death stranding"),
    QStringLiteral("kingdom come deliverance"), QStringLiteral("la noire"),
    QStringLiteral("max payne"), QStringLiteral("bioshock"), QStringLiteral("prey"),
    QStringLiteral("dishonored"), QStringLiteral("half-life"), QStringLiteral("half-life 2"),
    QStringLiteral("portal"), QStringLiteral("portal 2"), QStringLiteral("metro"),
    QStringLiteral("metro exodus"), QStringLiteral("stalker"), QStringLiteral("stalker 2"),
    QStringLiteral("smite"), QStringLiteral("warframe"), QStringLiteral("destiny 2"),
    QStringLiteral("path of exile"), QStringLiteral("last epoch"), QStringLiteral("grim dawn"),
    QStringLiteral("lost ark"), QStringLiteral("new world"), QStringLiteral("black desert"),
    QStringLiteral("albion online"), QStringLiteral("eve online"),
    QStringLiteral("elite dangerous"), QStringLiteral("star citizen"),
    QStringLiteral("no man's sky"), QStringLiteral("kerbal space program"),
    QStringLiteral("euro truck simulator"), QStringLiteral("american truck simulator"),
    QStringLiteral("farming simulator"), QStringLiteral("snowrunner"),
    QStringLiteral("surviving mars"), QStringLiteral("frostpunk"),
    QStringLiteral("this war of mine"), QStringLiteral("papers please"),
    QStringLiteral("league of legends"), QStringLiteral("teamfight tactics"),
    QStringLiteral("legends of runeterra"), QStringLiteral("valorant"),
    // ── Messengers ──
    QStringLiteral("discord"), QStringLiteral("telegram"), QStringLiteral("whatsapp"),
    QStringLiteral("slack"), QStringLiteral("signal"), QStringLiteral("microsoft teams"),
    QStringLiteral("teams"), QStringLiteral("zoom"), QStringLiteral("skype"),
    QStringLiteral("messenger"), QStringLiteral("viber"),
    // ── Media ──
    QStringLiteral("spotify"), QStringLiteral("vlc"), QStringLiteral("mpc-hc"),
    QStringLiteral("media player classic"), QStringLiteral("obs studio"),
    QStringLiteral("obs"), QStringLiteral("netflix"), QStringLiteral("plex"),
    QStringLiteral("kodi"), QStringLiteral("itunes"), QStringLiteral("foobar2000"),
    QStringLiteral("winamp"), QStringLiteral("audacity"), QStringLiteral("twitch"),
    QStringLiteral("youtube"), QStringLiteral("hulu"), QStringLiteral("disney+"),
    QStringLiteral("prime video"), QStringLiteral("hbo max"), QStringLiteral("crunchyroll"),
    QStringLiteral("paramount+"), QStringLiteral("photoshop"), QStringLiteral("lightroom"),
    QStringLiteral("premiere pro"), QStringLiteral("after effects"), QStringLiteral("gimp"),
    QStringLiteral("krita"), QStringLiteral("inkscape"), QStringLiteral("blender"),
    QStringLiteral("davinci resolve"), QStringLiteral("capcut"), QStringLiteral("bandicam"),
    QStringLiteral("handbrake"), QStringLiteral("shotcut"), QStringLiteral("kdenlive"),
    // ── Browsers ──
    QStringLiteral("chrome"), QStringLiteral("microsoft edge"), QStringLiteral("edge"),
    QStringLiteral("firefox"), QStringLiteral("opera"), QStringLiteral("opera gx"),
    QStringLiteral("brave"), QStringLiteral("vivaldi"), QStringLiteral("tor browser"),
    // ── Productivity / dev tools ──
    QStringLiteral("visual studio code"),
    QStringLiteral("cursor"), QStringLiteral("trae"), QStringLiteral("obsidian"),
    QStringLiteral("notion"), QStringLiteral("microsoft word"), QStringLiteral("word"),
    QStringLiteral("excel"), QStringLiteral("powerpoint"), QStringLiteral("outlook"),
    QStringLiteral("onenote"), QStringLiteral("windows terminal"), QStringLiteral("7-zip"),
    QStringLiteral("winrar"), QStringLiteral("winzip"), QStringLiteral("cpu-z"),
    QStringLiteral("hwinfo"), QStringLiteral("core temp"), QStringLiteral("screentogif"),
    QStringLiteral("termius"), QStringLiteral("putty"), QStringLiteral("ollama"),
    QStringLiteral("lm studio"), QStringLiteral("docker desktop"), QStringLiteral("postman"),
    QStringLiteral("insomnia"), QStringLiteral("wireshark"), QStringLiteral("notepad++"),
    QStringLiteral("notepad"), QStringLiteral("sublime text"), QStringLiteral("intellij idea"),
    QStringLiteral("pycharm"), QStringLiteral("webstorm"), QStringLiteral("android studio"),
    QStringLiteral("unity hub"), QStringLiteral("unreal engine"), QStringLiteral("github desktop"),
    QStringLiteral("sourcetree"), QStringLiteral("gitkraken"), QStringLiteral("godot"),
    QStringLiteral("rpg maker"), QStringLiteral("game maker"), QStringLiteral("aseprite"),
    // ── VPN / network ──
    QStringLiteral("nordvpn"), QStringLiteral("expressvpn"), QStringLiteral("protonvpn"),
    QStringLiteral("mullvad"), QStringLiteral("surfshark"), QStringLiteral("cyberghost"),
    QStringLiteral("private internet access"), QStringLiteral("windscribe"),
    QStringLiteral("openvpn"), QStringLiteral("wireguard"), QStringLiteral("tailscale"),
    QStringLiteral("zerotier"),
    // ── Gaming extras / remote / streaming ──
    QStringLiteral("teamspeak"), QStringLiteral("chatterino"), QStringLiteral("mumble"),
    QStringLiteral("ventrilo"), QStringLiteral("parsec"), QStringLiteral("anydesk"),
    QStringLiteral("teamviewer"), QStringLiteral("rustdesk"), QStringLiteral("streamlabs"),
    QStringLiteral("streamlabs obs"), QStringLiteral("xsplit"), QStringLiteral("voicemeeter"),
    QStringLiteral("moonlight"), QStringLiteral("sunshine"), QStringLiteral("exitlag"),
    // ── Utilities / downloads ──
    QStringLiteral("qbittorrent"), QStringLiteral("utorrent"), QStringLiteral("transmission"),
    QStringLiteral("deluge"), QStringLiteral("internet download manager"),
    QStringLiteral("jdownloader"), QStringLiteral("powertoys"), QStringLiteral("power toys"),
    QStringLiteral("autohotkey"), QStringLiteral("sharex"), QStringLiteral("greenshot"),
    QStringLiteral("snipaste"), QStringLiteral("lightshot"), QStringLiteral("hwmonitor"),
    QStringLiteral("msi afterburner"), QStringLiteral("crystaldiskinfo"),
    QStringLiteral("crystaldiskmark"), QStringLiteral("defraggler"), QStringLiteral("recuva"),
    QStringLiteral("ccleaner"), QStringLiteral("malwarebytes"), QStringLiteral("adwcleaner"),
    QStringLiteral("speccy"),
    // ── Well-known Store (UWP) apps — name-only matching ──
    QStringLiteral("microsoft store"), QStringLiteral("disney+"), QStringLiteral("tiktok"),
    QStringLiteral("instagram"), QStringLiteral("adobe express"), QStringLiteral("hulu"),
    QStringLiteral("apple tv"), QStringLiteral("apple music"),
};

// Pre-normalized tokens (folded once — matching then compares two normalized
// strings). C++20 lambda-init constant: definition order guarantees
// kCuratedNames is constructed first (same TU).
// 05.1 review (M-02): each name ALSO contributes its apostrophe-stripped
// variant — "baldur's gate 3" → "baldurs gate 3", matching the common
// GOG/EGS install name "Baldurs Gate 3" (normalized() collapses the
// apostrophe to a space, so the plain variant alone never matches).
const QStringList kCuratedTokens = [] {
    QStringList tokens;
    tokens.reserve(kCuratedNames.size() * 2);
    for (const QString &name : kCuratedNames) {
        tokens.append(normalized(name));
        QString bare = name;
        bare.remove(QLatin1Char('\''));
        tokens.append(normalized(bare));
    }
    return tokens;
}();

} // namespace

bool CurationRules::matches(const AppEntry &e)
{
    // CUR-04 belt-and-braces: added executables (Source::File) are never
    // curated — the escape hatch holds even if a caller forgets the guard.
    if (e.source == AppEntry::Source::File)
        return false;

    // Allowlist semantics (checkpoint fix): hidden by default == NOT on the
    // curated list. Match against BOTH the display name and the targetPath
    // basename (no extension) — a launcher whose .lnk name differs from its
    // exe ("Game Hub" → steam.exe) still resolves to the right token.
    const QString name = normalized(e.displayName);
    for (const QString &token : kCuratedTokens)
        if (containsWord(name, token))
            return false;

    // Path-based matching applies to Lnk rows only (UWP rows have no path —
    // Store apps are matched by name alone).
    if (!e.targetPath.isEmpty()) {
        const QString base = normalized(QFileInfo(e.targetPath).completeBaseName());
        for (const QString &token : kCuratedTokens)
            if (containsWord(base, token))
                return false;
    }
    return true; // not on the curated allowlist → hidden by default
}
