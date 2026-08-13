# Release Notes Theme Research

Research-only companion doc for wisp v0.1.0 (first GitHub release, GPLv3, Qt6/QML/C++, single maintainer, GitHub Actions pipeline). Body ~850 words. Every finding cites its source.

## 1. Universal patterns (what the best release notes share)

- **Version + ISO date as the title line.** Node: `2026-08-05, Version 26.7.0 (Current), @aduh95`; starship: `## [1.26.0](…) (2026-06-28)`; Angular: `# 21.2.20 (2026-08-12)`. Dates are ISO for a reason — keepachangelog: "the recommended date format for changelog entries" (source 4).
- **Same-type changes grouped under flat category headings** ("Features", "Fixes", "Other Changes"). keepachangelog's guiding principle #3: "The same types of changes should be grouped" (4). Electron uses exactly Features/Fixes/Other Changes (2); starship Features/Bug Fixes/Performance Improvements/Reverts (5).
- **Human-first summary precedes the ledger.** Node's "Notable Changes" sits above "Commits" (6); Zed opens with "This week's release includes…" naming 2–3 headlines (7); VSCode opens with a bullet list of its marquee features (1). Readers get the story, then the exhaustive list.
- **Every line links a PR.** Starship: `- **scope:** describe change ([#7500])([commit])`. Electron: trailing `[#52627]` plus "(Also in 42, 44)" backport tags. Angular: full commit tables. PR links are the currency of trust (2, 5, 6).
- **Imperative, sentence-fragment verbs in bullets**: "Added `webFrameMain.printToPDF()`", "show git am progress", "Fixed a crash when…" (2, 5).
- **Breaking changes/deprecations are loud and near the top.** Angular places "Breaking Changes" immediately under the version header; keepachangelog: "If you do nothing else, list deprecations, removals, and any breaking changes in your changelog" (3, 4).
- **Contributor/thanks section closes the notes.** VSCode ends with "## Thank you" (1); GitHub auto-generation ends with "New Contributors" + a thanks line (8); Zed has "Shipped by the Zed Guild 🛡️" and inline "thanks @user" (7).
- **Relentless consistency release-to-release.** VSCode, Electron, and starship use identical skeletons every cycle; readers learn the rhythm (1, 2, 5).
- **A closing sign-off gives identity**: VSCode's "Happy Coding!" (1); zed's monthly cadence text (7).

## 2. Three theme archetypes

**A. VSCode showcase** — announcement, media-rich. Structure: hero title → warm intro ("Welcome to the 1.131 release. This release brings…") → 3 marquee bullets with anchors → `##` feature areas (each with `###` sub-features + inline settings + screenshots/GIFs) → "Deprecated features" → "Thank you". Tone: excited-but-factual. Theming comes from heading hierarchy + embedded visuals, not emoji. **When:** product whose story is visual/UX (an app launcher qualifies); monthly cadence; effort: high (1).

**B. Angular/electron changelog** — commit ledger. Structure: version+date → Breaking Changes/Deprecations (narrative) → category sections (conventional-commit types: feat/fix/perf, or Features/Fixes/Other Changes) → PR links on every bullet. Tone: dry, clinical, exhaustive. **When:** developer-tool audiences, frequent releases, auto-generation via release-please/conventional commits; effort: near-zero after setup (3, 5, 9).

**C. keepachangelog minimal** — hand-maintained CHANGELOG.md. Six fixed types — `Added / Changed / Deprecated / Removed / Fixed / Security` — an `[Unreleased]` section at top, yanked-version tags, no prose, no emoji. "Changelogs are *for humans*, not machines." **When:** libraries needing a portable plaintext file, or when you want the *content discipline* underneath a flashier GitHub release (4).

## 3. First-release conventions

- **Flow-Launcher v1.0.0** (closest cousin — a Windows launcher): `# Release Notes`, `## Features` (1 bullet: "Official Flow Launcher logo"), `## Bug fixes` (2 bullets). No intro, no thanks — 3 bullets total. Proof that even minimal category scaffolding reads deliberate (10).
- **PowerToys v0.11.0** (first release): the *title* carries the theme — "FancyZones and Shortcut Guide initial commit" — body empty, marked Pre-release. Lesson: a themed title sets the tone even with no body (10b).
- **VSCode 1.131**: "Welcome to the… release. This release brings…" + "Happy Coding!" — the first-release intro formula is a welcome line, a what's-in-it line, a sign-off (1).
- **Zed 1.15.0**: "This week's release includes…" single paragraph covering all headlines (7).
- **Conventional Commits FAQ**, on initial releases: "proceed as if you've already released" — v0.1.0 notes should read like real notes, never "initial commit…" (9).
- **Known limits belong in v0.1**: keepachangelog's honesty principle ("what will break… must be painfully clear") maps to a first release's "Known limitations/not here yet" section (4).

## 4. Recommended theme for wisp

**Archetype: VSCode showcase structure + GitHub-release emoji categories + Zed summary paragraph**, scaled to ~300 words. Release title: **"wisp v0.1.0 — First light"** (themed, PowerToys-style title). Exact order:

1. `# wisp v0.1.0` + one-line subtitle fitting the brand tagline ("neat, sweet, and smooth").
2. **Intro paragraph (VSCode formula):** "Welcome to the first release of wisp — a Rofi-style launcher for Windows… This release brings…" 2 sentences max, plus 2–3 bolded hero bullets with anchors (Zed-style summary).
3. `## 🚀 Features` — 4–7 bullets, PR-linked, imperative ("Launch anything with `Alt+Space`…").
4. `## 🛠️ Fixes` — only real fixes; drop the section if empty (keepachangelog: removed empty sections "occupy too much space").
5. `## 🗺️ Roadmap / Known limitations` — honest v0.1 list (no file search yet, Win10 blur fallback, etc.).
6. `## 📦 Install` — 2 lines: installer download link + first-run hint (global hotkey, tray).
7. `## 🙏 Thanks` — LGPL/Qt notice courtesy line + "Full changelog" compare link (GitHub auto-gen pattern).
8. Sign-off: one line ("Happy launching!"-style, wisp-flavored).

Emoji set (fixed, reused forever): `🚀 Features`, `🛠️ Fixes`, `🗺️ Roadmap`, `📦 Install`, `🙏 Thanks` — mirroring GitHub's auto-gen category emoji trick (8) and Zed's `🛡️` section header (7). **Header style:** `## <emoji> <TitleCase>`. **Tone:** warm, first-person ("I built…"), one excitement word max per section. **Length target:** 250–350 words, ≤ 15 lines total. **Do not** use Angular-style commit tables or a CHANGELOG.md at v0.1 — the GitHub release body *is* the changelog (keepachangelog: Releases pages can stand in for the file) (4).

## 5. Do's and Don'ts checklist

- DO lead with the story, not the commit list. — Zed/VSCode (1, 7)
- DO link every PR/issue. — Electron/starship (2, 5)
- DO prove the product: one screenshot/GIF of the launcher popup if humanly possible. — VSCode (1)
- DO write the body as a real changelog, not "initial commit". — conventional commits FAQ (9)
- DO repeat the skeleton forever. — Electron (2)
- DON'T dump raw git log ("full of noise" — commit-log diffs is a named keepachangelog bad practice) (4).
- DON'T use fake PR links or version numbers that don't exist.
- DON'T add emoji per bullet — only per section header (scannability) (4).
- DON'T overpromise: v0.1 known-limits honesty beats hype (4).
- DON'T say "thank you" to empty names — thank real contributors or Qt/LGPL notice.

## 6. Template sketch

```markdown
# wisp v0.1.0 — First light

*Neat, sweet, and smooth — a Rofi-style launcher for Windows.*

Welcome to the first release of wisp. Press **`Alt+Space`** anywhere and this release brings…

- **Instant launch** — opens and fires an app in under a second.
- **Rofi-style matching** — fuzzy-scored results across Start Menu apps (+ UWP).

## 🚀 Features
- Global `Alt+Space` hotkey via `RegisterHotKey` (#NNN)
- Fuzzy app search across Start Menu shortcuts and installed UWP apps (#NNN)
- Acrylic backdrop on Windows 11, graceful fallback on Windows 10 (#NNN)
- Tray icon + HKCU autostart toggle (#NNN)

## 🛠️ Fixes
- *(omit section if nothing to fix)*

## 🗺️ Roadmap
- File-content search (Windows Search index), configurable hotkey UI, plugins

## 📦 Install
Download `wisp-setup-0.1.0.exe` below; first run registers the hotkey and tray icon.

## 🙏 Thanks
Qt (LGPL) makes the 60fps animation possible. [Full changelog](compare link)
Happy launching!
```

## Sources

1. https://code.visualstudio.com/updates/v1_131 (VSCode 1.131 release notes; template: welcome line, hero bullets, feature areas, Thank you)
2. https://github.com/electron/electron/releases (Features / Fixes / Other Changes, PR links, backport tags)
3. https://raw.githubusercontent.com/angular/angular/main/CHANGELOG.md (Breaking Changes / Deprecations front-loading, commit tables by Type)
4. https://keepachangelog.com/en/1.1.0/ (six change types, Unreleased, ISO dates, bad practices, yanked tags, GitHub Releases stance)
5. https://github.com/starship/starship/releases (release-please output: Features / Bug Fixes / Performance Improvements, scoped bullets)
6. https://raw.githubusercontent.com/nodejs/node/main/doc/changelogs/CHANGELOG_V26.md (Notable Changes above Commits, SEMVER-MINOR labels, release-manager attribution)
7. https://zed.dev/releases (monthly theme: "This week's release includes…", Features by area, Bug Fixes, Breaking Changes and Notices, Zed Guild 🛡️)
8. https://docs.github.com/en/repositories/releasing-projects-on-github/automatically-generated-release-notes (What's Changed categories, emoji in category titles, New Contributors, Full Changelog link)
9. https://www.conventionalcommits.org/en/v1.0.0/ (feat/fix/BREAKING CHANGE, scope, "proceed as if you've already released")
10. https://github.com/Flow-Launcher/Flow.Launcher/releases/tag/v1.0.0 (indie first release: Features / Bug fixes, 3 bullets)
10b. https://github.com/microsoft/PowerToys/releases/tag/v0.11.0 (first release: themed title, no body)