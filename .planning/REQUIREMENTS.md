# Requirements: Rofi-Windows

**Defined:** 2026-08-09
**Core Value:** The launcher must open and launch in under a second with buttery-smooth animation. Speed and feel are the product.

## v1 Requirements

Requirements for initial release. Each maps to roadmap phases.

### Launch & Search

- [x] **LAUN-01**: User can search installed apps (Start Menu shortcuts + UWP/Store apps) with fuzzy subsequence matching, case-insensitive, with word-boundary/camelCase bonuses
- [x] **LAUN-02**: User can search files via the Windows Search index; Enter opens the file with its default app
- [x] **LAUN-03**: User can open the containing folder of a file result with Ctrl+Enter
- [x] **LAUN-04**: User can launch an app as administrator with Ctrl+Shift+Enter (apps only; UWP/Store apps gracefully refuse)
- [x] **LAUN-05**: User can navigate results with ↑/↓, PageUp/PageDown, Home/End, and launch with Enter; mouse click also works
- [ ] **LAUN-06**: Matched characters in results are highlighted in the accent color

### Catalog Curation (Phase 05.1 — inserted 2026-08-11, urgent)

- [ ] **CUR-01**: The default catalog shows only real apps and games — rule-hidden noise (uninstallers, dev toolchains, Windows system utils, installer cruft) is absent by default; marking runs after dedupe so a hidden .lnk never resurrects its UWP twin
- [ ] **CUR-02**: Per-row Hide persists across restarts and catalog rebuilds; an uninstalled hidden app breaks nothing (inert key); explicit user overrides beat default rules (shown wins)
- [ ] **CUR-03**: Hidden entries are excluded from search by default but recoverable via a "Show hidden (N)" mode (dimmed, unhideable); Ctrl+H hides/unhides the selected row; query is preserved on hide
- [ ] **CUR-04**: User-added executables are never silently hidden — "Add executable…" (Source::File rows) is structurally immune to curation

### Hotkey & Dismissal

- [ ] **HOTK-01**: A global hotkey toggles the launcher; default Alt+Space; user-configurable
- [ ] **HOTK-02**: Hotkey registration conflicts are surfaced to the user (tray notification), not silently ignored
- [ ] **HOTK-03**: Launcher dismisses on Escape, on launch (instantly, no animation wait), and on click-away
- [ ] **HOTK-04**: Launcher does not steal focus from exclusive fullscreen games/windows

### Look & Feel

- [ ] **VISU-01**: Launcher opens as a small widget centered on the screen with a scale+fade animation (~150-200ms) holding 60fps; reverse animation on dismiss
- [ ] **VISU-02**: Sleek dark theme (free — a deliberate contrast to light-only free launchers)
- [ ] **VISU-03**: User can pick an accent color used for selection and match highlighting

### System & Packaging

- [ ] **SYS-01**: System tray icon with Open / Settings / Quit menu; single-instance enforcement
- [ ] **SYS-02**: User can toggle "start with Windows" (autostart) in settings
- [ ] **SYS-03**: Settings window with hotkey capture, accent color picker, and autostart toggle
- [ ] **SYS-04**: Installer works on clean Windows 10/11 machines (NSIS, windeployqt `--qmldir`, VC redist) with Qt LGPL notices included

## v2 Requirements

Deferred to future release. Tracked but not in current roadmap.

### Ranking

- **LAUN-07**: Usage-history ranking (recency/frequency) boosts results
- **LAUN-08**: Recent apps shown on empty query

### Files

- **LAUN-09**: Copy full path action for file results

### Look & Feel

- **VISU-04**: Acrylic blur/transparency backdrop (Win11 22H2+ documented path, Win10 fallback)

## Out of Scope

Explicitly excluded. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| Plugin SDK / marketplace | Massive scope; dilutes v1 tightness; PROJECT.md defers plugins |
| Clipboard history | Win+V already solves it; sensitive-data storage questions |
| Window switching (alt-tab style) | Complex Win32 surface; Win+Tab exists; PROJECT.md excludes |
| Web search / bookmarks | Scope creep; browser profile formats break between versions |
| Shell command execution | Arbitrary code execution = security surface + confirmation UX |
| Custom directory indexing | Windows Search index already exists; Launchy's indexing was its chronic weakness |
| Everything (Voidtools) integration | Third-party dependency; keep backend abstraction so it can slot in as v1.x |
| System commands (shutdown/lock/restart) | Not in v1 scope; candidate if users ask post-release |
| AI / MCP integration | API costs, latency vs. "instant" brand |
| Emoji picker | Win+. is built in |
| Multi-language (Pinyin etc.) | No demand yet |
| Cross-platform (Linux/macOS) | This is a Windows launcher |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| LAUN-01 | Phase 3 | Complete |
| LAUN-02 | Phase 4 | Complete |
| LAUN-03 | Phase 4 | Complete |
| LAUN-04 | Phase 3 | Complete |
| LAUN-05 | Phase 3 | Complete |
| LAUN-06 | Phase 5 | Pending |
| CUR-01 | Phase 05.1 | Pending |
| CUR-02 | Phase 05.1 | Pending |
| CUR-03 | Phase 05.1 | Pending |
| CUR-04 | Phase 05.1 | Pending |
| HOTK-01 | Phase 2 | Pending |
| HOTK-02 | Phase 2 | Pending |
| HOTK-03 | Phase 2 | Pending |
| HOTK-04 | Phase 2 | Pending |
| VISU-01 | Phase 1 | Pending |
| VISU-02 | Phase 5 | Pending |
| VISU-03 | Phase 6 | Pending |
| SYS-01 | Phase 6 | Pending |
| SYS-02 | Phase 6 | Pending |
| SYS-03 | Phase 6 | Pending |
| SYS-04 | Phase 6 | Pending |

**Coverage:**
- v1 requirements: 21 total
- Mapped to phases: 21 ✓ (100%)
- Unmapped: 0

---
*Requirements defined: 2026-08-09*
*Last updated: 2026-08-09 after roadmap creation*
