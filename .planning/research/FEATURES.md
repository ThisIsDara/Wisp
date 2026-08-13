# Feature Research

**Domain:** Windows application launcher (Rofi-style)
**Researched:** 2026-08-09
**Confidence:** HIGH — every table-stakes claim verified against official sources (Rofi GitHub/manpage, Microsoft Learn PowerToys Run docs, Flow Launcher GitHub/docs, Listary docs, Ulauncher site, Wox repo, fzf algorithm source). Launchy claims are archival (development ceased 2010). Third-party articles are marked MEDIUM.

## Feature Landscape

The category consensus after surveying Rofi, PowerToys Run, Launchy, Albert, Wox, Ulauncher, Listary, and Flow Launcher: **every successful launcher converges on the same core loop — hotkey → type → arrow/Enter → launch → dismiss — and differentiates on ranking quality, polish, and extensibility.** The v1 bar is: instant fuzzy app+file search, keyboard-first interaction, recency ranking, configurable hotkey, tray presence, and a settings surface. Everything else (calculator, clipboard, window switching, plugins) is either a plugin-era add-on or a category *anti-feature* for a tight Rofi-style core.

### Table Stakes (Users Expect These)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Global hotkey, configurable, default Alt+Space | Universal across Launchy, Flow, PowerToys, Wox, Listary; Alt+Space is the de facto standard | Med | Win32 `RegisterHotKey`; must handle registration failure (conflict) with a tray notification, not silence. Fullscreen/game suppression ("disable in full-screen games") is a common companion — Listary ships it, Flow has Ctrl+F12 game mode. |
| Instant fuzzy app search (Start Menu shortcuts + UWP/Store apps) | The core job. Fuzzy subsequence matching with boundary bonuses is the baseline bar (fzf-style); users type partial names and expect hits | Med | Match on app name + path tokens, case-insensitive. Acronym matching ("vs" → Visual Studio Code, "obsi" → Obsidian) falls out of word-boundary/camelCase bonuses for free — no special code. |
| Keyboard-first navigation: ↑/↓, Enter, Esc | Universal interaction model; mouse optional for every competitor | Low | Add PageUp/PageDown; Home/End expected (Rofi keybindings). Mouse hover+click retained for casual users. |
| Recency/frequency ranking | Launchy (frequency-of-use, 2005), Rofi ("last 25 choices on top"), Ulauncher ("remembers previous picks, auto-selects best"), Listary ("smart ranking by habits"), Flow (top results) all do it | Low–Med | Persist a small JSON store; boost by count + last-used timestamp. This also powers "show recent apps on empty query," which users expect. |
| Launch selected result on Enter | Universal | Low | ShellExecute for Win32 apps; UWP via ShellExecute/AUMID. |
| Dismiss on Escape, launch, and click-away | Universal (Launchy "hide when it loses focus" since 2007; PowerToys Esc; Flow hides on outside click) | Low–Med | Frameless popup + focus-out handling. Launch path must dismiss *immediately* — no animation wait. |
| File search via Windows Search index; Enter opens with default app | Flow (Explorer plugin), PowerToys (`?` prefix), Listary all search files; opening with default app is the expected action | Med | Index coverage gaps and stale index entries are the known UX hazards — "no results" state needs guidance, not dead air. |
| Settings: hotkey capture + accent color | Configurable hotkey is an explicit v1 requirement; theme/accent customization is expected in any modern launcher | Med | Settings window + JSON config persistence. Hotkey capture UI is a small but fiddly control. |
| Tray icon with Open/Settings/Quit | Launchers live in the background; the tray is the escape hatch (Launchy, Flow, Listary all tray-based) | Low | Single-instance enforcement; left-click opens launcher or menu. |
| Autostart with Windows, toggleable | Launchy defaulted to autostart; Flow/Listary offer the toggle; users expect the launcher to survive reboot | Low | HKCU `...\CurrentVersion\Run` registry key + settings toggle. |
| Run as administrator (Ctrl+Shift+Enter) | PowerToys, Flow (Ctrl+Shift+Enter), and Launchy (Shift+Ctrl+Enter) all ship it; Windows power users launch elevated tools constantly | Low–Med | ShellExecute `runas` verb; launcher itself stays non-elevated. **UWP/Store apps cannot be elevated** — must disable or fall back gracefully. |
| Open containing folder for file results (Ctrl+Enter) | Flow (Ctrl+Enter), PowerToys (Ctrl+Shift+E) ship it; power users hit it constantly when file search returns a result | Low | `explorer /select,"<path>"`. Requires file results to carry the full path (schema decision early). |

### Differentiators (Competitive Advantage)

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| Scale+fade open/close animation holding 60fps | The project's stated core value ("neat, sweet, and smooth"). Competitors animate minimally or not at all — Flow and PowerToys use utilitarian fades; Rofi pops instantly. This is the visible polish gap | Med | QML is ideal for this; 150–200ms; focus must be typable before animation finishes. |
| Rofi-faithful instant feel (sub-second open→launch, no bloat) | The product's identity: "Rofi on Windows." Competitors grow feature-fat and slow — PowerToys Run startup/plugin lag is a documented complaint; Flow's plugin-first design adds boot cost | Med | Startup-to-hotkey-ready < 1s; warm the app catalog on idle after launch; lazy-load file search connection. |
| Sleek dark theme + accent color + blur/transparency | Modern polish. Notably, **Listary Free is light-theme-only and sells dark mode in Pro** — a free, polished dark theme is a real contrast in the market | Low–Med | Qt6: DWM acrylic/blur on Win11 with a QML translucent fallback for Win10. |
| Native, small binary (Qt6, no web runtime) | vs. Electron-based new-Wox and Raycast for Windows; ~10–30MB install vs 100MB+ web-runtime bundles | Low | A packaging decision that shows up as a differentiator in marketing and RAM usage. |
| Fuzzy match highlighting (matched characters in accent color) | Rofi highlights matches; Flow/PowerToys generally do not. Instant readability when results scroll | Med | Requires the matcher to return match positions (fzf-style scoring does). Clean v1.x polish item. |

### Anti-Features (Commonly Requested, Often Problematic)

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| Plugin SDK / marketplace | "Power users" ask; Flow and Wox built their brands on plugin ecosystems | Massive scope: API design, security vetting, docs, discovery UI. Dilutes v1 tightness. PROJECT.md explicitly defers plugins | Keep the core tight; revisit v2+ only if there is validated demand for extensibility |
| Clipboard history | New-Wox ships it; Flow has a popular Clipboard+ plugin; PowerToys v2/Command Palette bundles it | Clipboard contents are sensitive; storage/privacy questions; Windows 11 Win+V already solves the use case | Point users to Win+V; revisit v2+ with encryption if actually demanded |
| Window switching (alt-tab style) | Rofi's `window` mode; PowerToys Window Walker; Flow plugin | Complex Win32/COM surface; explicitly out of scope in PROJECT.md; Win+Tab exists | Defer; revisit v2+ |
| Web search + browser bookmarks | "g query" muscle memory from Launchy's Weby, Flow, Listary | Scope creep; browser profile formats break between versions; every launcher's web plugin is a maintenance sink | Defer to v1.x; not needed to validate apps+files |
| Shell command execution / run-$PATH mode | Rofi's `run` mode; Flow Shell plugin (batch/PowerShell as admin) | Arbitrary command execution = security surface + confirmation UX questions | Apps+files only in v1; a typed command mode later, deliberately designed |
| Custom directory indexing / catalog | Launchy's model; "index my folders" requests | **Launchy's own indexing was its chronic weakness** (slow rebuilds, stale catalogs); Windows Search index already exists | Use the Windows Search index; Everything backend as the v1.x upgrade path |
| Everything (Voidtools) integration | Near-instant filename search; Flow supports it as a backend | Third-party runtime dependency or IPC to user-installed Everything | Keep the search backend behind a thin interface now so Everything can slot in later without rework |
| System commands (shutdown/restart/lock) | Flow and PowerToys ship them; users request them reflexively | Trivial individually but adds UI + confirmation surface; not in v1 scope | Candidate for v1.x if users ask after release |
| AI chat / MCP integration | New-Wox ships AI with MCP; Flow has "Ask AI" plugins — the 2026 trend | API costs, key management, latency vs. the "instant" brand | Anti-feature for v1; v2+ only |
| Emoji picker | New-Wox ships it | Win+. already built into Windows; scope creep | Skip; point users to Win+. |
| Pinyin / multi-language search | Flow supports Pinyin; CJK users ask | Locale-aware matching + i18n surface | Defer until non-English demand actually appears |

## Feature Dependencies

```
Global hotkey (config) ─────────┐
App enumeration (Start Menu + UWP) ──┼─→ Search core (fuzzy matcher) ──→ Result list ──→ Launch (Enter)
File search (Windows Search index) ──┘                     │  │                        │
                                                           │  └──→ Run-as-admin (Ctrl+Shift+Enter) [apps only]
Recency store (JSON) ──→ ranking boost ──→ ordering        │                           │
        │                  (empty-query recent list)       └──→ Open containing folder (Ctrl+Enter) [files]
        └─ depends on: stable result identity               └──→ Copy path (v1.x) [files]
           (exe path / AUMID / full file path)
Animations ──→ widget show/hide ──→ dismissal paths (Esc / launch / click-away)
Settings (hotkey, accent, autostart) ──→ settings window + tray menu ──→ autostart registry write
Installer ──→ single-instance, autostart defaults, clean-install paths
```

Notes on dependencies:

- **Recency ranking requires a stable identity per result** — apps keyed by executable path or AUMID (UWP), files by full path. Design identity into the result model from day one; retrofitting it later means breaking the ranking store.
- **Run-as-admin, open-containing-folder, and copy-path all hang off one "action runner" module** built on ShellExecute variants. Do not scatter ShellExecute calls through the codebase; one module also makes the UWP-can't-elevate guard single-point.
- **File results must carry the full path** in the result model (needed by open-containing-folder, copy-path, and recency key). This is a v1 schema decision.
- **Dismissal and animation are coupled**: Enter/launch must dismiss instantly (no animation), while Esc/click-away plays the reverse animation. Decide this in the widget show/hide design; it is a feel-defining detail.
- **The search backend needs an abstraction** (Windows Search now, Everything later) — a 20-line interface now, an architecture change later.
- **Alt+number quick-select** is trivial if the result list is a single QML ListView with index access — do not prebuild it as a separate system.

## MVP Definition

**Launch With (v1):**
- Global hotkey, default Alt+Space, configurable, graceful conflict handling
- App search: Start Menu shortcuts + UWP/Store apps; fuzzy subsequence matching, case-insensitive, boundary bonuses
- File search via Windows Search index; Enter opens with default app; Ctrl+Enter opens containing folder
- Keyboard navigation (↑/↓, Enter, Esc, PageUp/PageDown, Home/End); mouse click and click-away dismiss
- Recency/frequency ranking + recent apps shown on empty query
- Run-as-admin via Ctrl+Shift+Enter (apps only; UWP guarded)
- Dark theme + accent color, scale+fade animations at 60fps, blur/transparency backdrop
- Tray icon (Open/Settings/Quit), autostart toggle, single instance
- Settings window: hotkey capture, accent color, autostart
- Installer with LGPL compliance; works on clean Windows 10/11

**Add After Validation (v1.x):**
- Alt+number quick-select with visible number hints
- Fuzzy match highlighting in accent color (matcher returns positions)
- Query history browsing (Ctrl+H-style)
- Game mode / disable hotkey in fullscreen
- Opacity + blur toggles, theme presets
- Everything backend behind the existing abstraction
- System commands (shutdown/restart/lock) — only if post-release feedback asks
- Simple calculator — only if post-release feedback asks (expression evaluator, no dependency)

**Future Consideration (v2+):**
- Plugin SDK
- Clipboard history
- Window switching
- Web search
- AI integration

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|-----------|---------------------|----------|
| Global hotkey + config | Critical | Med | P1 |
| Fuzzy app search | Critical | Med | P1 |
| Recency ranking + empty-query recent list | High | Low–Med | P1 |
| Keyboard nav + dismiss (Esc/launch/click-away) | Critical | Low | P1 |
| File search (index) + open with default app | High | Med | P1 |
| Open containing folder | High | Low (ships with file search) | P1 |
| Run-as-admin | High | Low–Med | P1 |
| Scale+fade animations | High (core value) | Med | P1 |
| Dark theme + accent color | High | Low–Med | P1 |
| Tray + autostart | Med–High | Low | P1 |
| Settings window | Med–High | Med | P1 |
| Installer | Med (release gate) | Med | P1 |
| Fuzzy match highlighting | Med | Med | P2 |
| Alt+number quick-select | Med | Low | P2 |
| Query history | Med | Low–Med | P2 |
| Game mode | Low–Med | Low | P2 |
| Everything backend | Med | Med | P2 |
| Calculator | Med | Low (simple eval) | P3 |
| System commands | Low–Med | Low | P3 |
| Clipboard history | Med | High | P3 (likely anti-feature) |
| Window switching | Med | High | P3 (defer v2+) |
| Plugin SDK | High (long-term) | Very High | P3 (defer v2+) |

## Competitor Feature Analysis

| Feature | Rofi | PowerToys Run | Flow Launcher | Our Approach |
|---------|------|---------------|---------------|--------------|
| Hotkey | WM/DE binding, no built-in | Alt+Space, configurable | Alt+Space, configurable | Alt+Space, configurable, conflict handling |
| App search | `drun` (.desktop files), `run` ($PATH) | installed apps + plugins | Program plugin | Start Menu + UWP enumeration |
| File search | file-browser mode (manual browse) | Windows Search (`?` prefix) | Explorer plugin: Windows Search **or** Everything | Windows Search index behind backend abstraction |
| Fuzzy matching | tokenized, fuzzy/regex/glob, Levenshtein or fzf-like sorting | fuzzy matching | fuzzy matching | fzf-style subsequence + boundary bonuses |
| Recency ranking | last-25 history (optional) | History plugin (`!!`) | query history, top results | count + last-used JSON boost |
| Run-as-admin | n/a (Linux) | Ctrl+Shift+Enter | Ctrl+Shift+Enter | Ctrl+Shift+Enter, UWP guarded |
| Open containing folder | n/a | Ctrl+Shift+E | Ctrl+Enter | Ctrl+Enter |
| Window switching | built-in `window` mode | Window Walker plugin | Window Walker plugin | Deferred (out of scope) |
| Calculator | n/a | built-in (`=` prefix) | built-in | Deferred candidate (P3) |
| Clipboard history | n/a | v2/Command Palette | 3rd-party plugin | Deferred (anti-feature v1) |
| Plugins | script/plugin extensions | plugin manager | plugin store + SDKs (C#, Python, JS) | Deferred to v2+ |
| Theming | advanced `.rasi` theming | few themes | themes + custom themes | Curated: dark theme + accent color |
| Animations | none (instant pop) | minimal | minimal fade | scale+fade @ 60fps (core value) |
| Tray icon | n/a | n/a (background service) | tray | tray: Open/Settings/Quit |
| Autostart | n/a (session tools) | via PowerToys settings | settings toggle | HKCU Run key + toggle |
| Languages | UTF-8, RTL support | Pinyin option | 26+ languages, Pinyin | English v1; i18n later |

## Sources

- **Rofi** — official repo README (github.com/davatorium/rofi), manpage (man.archlinux.org/man/rofi.1.en; davatorium.github.io/rofi/1.7.3/rofi.1). HIGH
- **PowerToys Run** — Microsoft Learn, "PowerToys Run utility" (learn.microsoft.com/en-us/windows/powertoys/run, ms.date 2025-08-20). HIGH
- **Flow Launcher** — official repo README incl. hotkey table (github.com/Flow-Launcher/Flow.Launcher), flowlauncher.com, plugin directory (flowlauncher.com/plugins), docs repo. HIGH
- **Launchy** — launchy.net, SourceForge project news/changelogs (sourceforge.net/p/launchy/news). HIGH for historical facts; **project discontinued after 2.5 (2010)**.
- **Listary** — official docs (help.listary.com — hotkeys page updated 2025-06-06), Wikipedia, listary.com. HIGH
- **Ulauncher** — ulauncher.io, GitHub repo (v5 features; v6 rewrite in progress). HIGH
- **Wox** — github.com/Wox-launcher/Wox: rebuilt as Go/Flutter cross-platform launcher with AI/MCP, plugin/theme stores, clipboard history, emoji (the original C#/WPF Wox, 2015–2019, is what Flow Launcher forked from). HIGH
- **Albert** — albertlauncher.github.io: C++/Qt keyboard launcher, plugin-based architecture; Linux-focused reference for interaction patterns. MEDIUM (limited doc depth reviewed)
- **fzf matching algorithm** — github.com/junegunn/fzf src/algo/algo.go (FuzzyMatchV2, modified Smith–Waterman; boundary/consecutive bonuses), DeepWiki fzf summary, NeoMutt fuzzy-search docs (concrete scoring table). HIGH
- **WindowsForum review of Flow Launcher** (windowsforum.com, 2025-09) — corroborates numbered selection (Alt+number) and Everything integration. MEDIUM (third-party)
- **MakeUseOf Listary review** (makeuseof.com, 2025-12) — corroborates Ctrl+number selection, smart ranking, Ctrl-double activation. MEDIUM (third-party)
- **Tim Andrew, "FuzzyMatchV2 visualized"** (timothya.com, 2026-05) — algorithm mechanics. MEDIUM

**Confidence flags:** Flow's Alt+number selection is third-party-cited only (not in official hotkey docs) — MEDIUM. New-Wox and Raycast-for-Windows specifics — LOW (brief exposure; not central to this analysis). Everything else above is HIGH.

---
*Feature research for: Windows application launcher (Rofi-style)*
*Researched: 2026-08-09*
