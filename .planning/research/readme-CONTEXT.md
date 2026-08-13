# README Research Context

## 1. What the best READMEs share

- **README = landing page, not manual.** All 10 push detail to docs sites (react.dev, starship.rs, flowlauncher.com, electronjs.org) and keep the README to pitch + install (React, Electron, VSCode, OBS).
- **One-sentence tagline in the first screenful** ("React is a JavaScript library for building user interfaces."; same shape in PowerToys, Flow, Starship, Electron).
- **Visual media inside the first ~15 lines** — hero image (PowerToys), demo GIF (Flow, Starship), screenshot (VSCode, ripgrep).
- **Install reachable from the first screenful** — PowerToys and Starship's first navigable link row includes Installation; Flow's section 2 is Getting Started with a direct `releases/latest/download/...exe` link.
- **Feature bullets with bold lead-ins** (React's 3, Starship's 6, Syncthing's numbered Goals, Neovim's mission bullets).
- **End-user content on top, contributor content at bottom** — Contributing/CoC/License are the final sections in all 10; Flow buries Development after Sponsors.
- **Badges clustered right after logo/tagline** — everywhere except PowerToys (which skips them).
- **Separate CONTRIBUTING.md**, never inlined (all 10).
- **Trust signals as content**: SmartScreen/unsigned warning + "only trust official channels" (Flow), signed releases (Syncthing), code-signing policy (Starship), CLA (PowerToys, React).
- **Honesty packs punch**: ripgrep's "Why shouldn't I use ripgrep?" is its most-cited section.
- **Changelog linked, not embedded** (ripgrep CHANGELOG.md; PowerToys "What's new" banner → Releases).

## 2. Structural template (the common strongest shape)

1. Name/logo + one-line tagline (centered in consumer apps, plain heading in dev tools)
2. Badge row (2-5)
3. Demo media (GIF/screenshot)
4. Value-pitch bullets ("why this tool")
5. Quick nav rows (PowerToys/Flow/Starship: Install · Docs · Changelog)
6. Installation (primary method first, direct links, collapsibles for alternates)
7. Features with per-feature media (Flow model)
8. Usage — hotkey/command tables (Flow's `<kbd>` tables)
9. Docs/community links (OBS is only this after a one-liner)
10. Contributing → CoC → License (always last; React also puts a license badge in the heading)

## 3. Tone & audience matrix

- **End-user-first (Flow, OBS):** GIF at top, install in section 1-2, every feature gets a screenshot, dev content at the tail. Implies: assume no terminal comfort, preempt SmartScreen panic, pictures over prose.
- **Dev-first (React, Electron, ripgrep):** package-manager install first, docs-forward, no app screenshots — works only because the audience IS developers.
- **Bifurcated (PowerToys, Starship, VSCode, Neovim):** product pitch + install for users, clean break, then source/build/contribute. VSCode makes the split explicit ("The Repository" vs "Visual Studio Code"). Never interleave audiences.
- **wisp:** bifurcated, end-user majority — installer and GIF dominate the top; Qt/MSVC build notes near the bottom.

## 4. Badges & media best practices

- **Earn their keep** (recurring across projects): CI/build status (Electron, React, Starship, ripgrep), GitHub downloads (Flow, Neovim), latest release (Flow, React), license (React, Syncthing), Discord (Electron, Flow, OBS). Skip Coverity/repology-style clutter — Neovim shows over-badging, Flow's 8 are near the ceiling.
- **One hero GIF, ≤50% width** — Starship floats it right of the pitch; Flow centers it 500px looping open→search→launch.
- **Per-feature screenshots** for consumer UX (Flow); terminal shots for CLI (ripgrep). Keep ≤600px wide.
- **Dark/light-aware hero via `<picture>`** (PowerToys) — a dark-only launcher screenshot looks broken in light GitHub chrome.

## 5. First 15 lines matter most

Three openings recur, all answering *what/why/where-get-it* in one screen: (a) hero image → name → tagline → 4-link nav including Installation (PowerToys); (b) badge-decorated title → one sentence → 3 bullets (React); (c) demo GIF → badges → 2-sentence pitch (Flow). Only well-known projects open with mission prose (Neovim, Syncthing) — not a model for a new project.

## 6. Domain-specific guidance (Windows launcher)

- **Direct installer link, above the fold:** Flow links `releases/latest/download/Flow-Launcher-Setup.exe` + portable zip; PowerToys annotates scope ("x64 per-user for most devices").
- **Preempt the unsigned/SmartScreen warning** (Flow's blockquote) — near-certain for a fresh GPLv3 project.
- **Say "no admin needed / per-user install"** (PowerToys scope note, Flow portable paths).
- **Feature GIFs over bullets** — a launcher's product *is* motion (open animation, fuzzy filter); Flow demos exactly that.
- **Hotkey table with `<kbd>`** (Flow) — users need defaults at a glance.
- **Trust section**: "only trust official releases", optionally checksums (Flow, Syncthing, Starship).
- **Changelog → Releases link** (PowerToys banner, ripgrep). Roadmap: PowerToys keeps it vague-positive; a single maintainer should link the issues board or omit it.
- **State "Windows 10/11" plainly** (Flow: "Windows 10+").

## 7. Do's and Don'ts checklist

**Do:** lead with a launcher-action GIF; one-sentence "what it is"; direct installer link in screen one; SmartScreen caveat; `<kbd>` hotkey table; link docs instead of inlining; contributing/license last; state min-OS; link Releases; loop GIF ≤5s starting at open.
**Don't:** open with history/mission prose; exceed ~6 badges; copy ripgrep's 200-line per-distro install (outlier); use emoji headers with a dev-tool tone (PowerToys/Flow do, ripgrep/Neovim/VSCode/Electron don't — pick one); ship a dark-only hero; put build-from-source before user install (right only for Electron); skip an honest "limits" line vs PowerToys Run/Flow.

## 8. Recommended section order for wisp

1. **Logo + name + tagline** ("Rofi-style launcher for Windows, instant and smooth") — first impression (React, PowerToys)
2. **Demo GIF** (hotkey → open → fuzzy type → Enter-launch) — the product is the feel (Flow, Starship)
3. **Badges** (downloads, latest release, CI, GPLv3) — trust at a glance (Flow, React)
4. **Features, bold bullets** — fuzzy match, global hotkey, UWP/app/file search, acrylic, tray-resident (Starship, React)
5. **Installation** — Releases exe + per-user note + SmartScreen caveat (Flow, PowerToys)
6. **Usage** — default hotkey + `<kbd>` behavior table (Flow)
7. **Limits/honesty** — one line vs PowerToys Run/Flow + roadmap link (ripgrep, PowerToys)
8. **Changelog** → Releases link (PowerToys, ripgrep)
9. **Building from source** — Qt 6.11/MSVC quick path, link BUILD.md (Neovim)
10. **Contributing** — good-first-issues, small-project etiquette (React, Starship)
11. **License** — GPLv3 explicit (all; React badge style)

## Sources

- https://raw.githubusercontent.com/microsoft/PowerToys/main/README.md
- https://raw.githubusercontent.com/Flow-Launcher/Flow.Launcher/master/README.md
- https://raw.githubusercontent.com/obsproject/obs-studio/master/README.rst
- https://raw.githubusercontent.com/electron/electron/main/README.md
- https://raw.githubusercontent.com/facebook/react/main/README.md
- https://raw.githubusercontent.com/neovim/neovim/master/README.md
- https://raw.githubusercontent.com/syncthing/syncthing/master/README.md
- https://raw.githubusercontent.com/BurntSushi/ripgrep/master/README.md
- https://raw.githubusercontent.com/starship/starship/main/README.md
- https://raw.githubusercontent.com/microsoft/vscode/main/README.md