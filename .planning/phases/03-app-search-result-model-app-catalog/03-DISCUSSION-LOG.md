# Phase 3: App Search (Result Model + App Catalog) - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-09
**Phase:** 3-App Search (Result Model + App Catalog)
**Areas discussed:** Empty-query results, Ranking feel, Catalog refresh

---

## Empty-query results

| Option | Description | Selected |
|--------|-------------|----------|
| Full app list | Hotkey shows a ready-made list of every installed app, sorted alphabetically. Zero dead air, immediate "it works" feel — matches PowerToys Run and Spotlight. | ✓ |
| Blank until typing | Empty until you type; a hint like "Search apps…" sits in the field. | |
| Hybrid: first-show only | Show the list only on the very first open after launch, blank on later opens. | |

| Option | Description | Selected |
|--------|-------------|----------|
| Plain alphabetical | Empty query = every app, sorted alphabetically (case-insensitive, locale-aware). | ✓ |
| Same score pipeline | Sort the empty list by the same fuzzy score computed against the empty string. | |

| Option | Description | Selected |
|--------|-------------|----------|
| First row selected | Keyboard starts with the first row selected (Rofi/Spotlight feel); mouse hover also selects. | ✓ |
| Nothing selected | Nothing selected until the user presses a key or moves the mouse. | |

| Option | Description | Selected |
|--------|-------------|----------|
| Everything, filtered | All 500+ .lnk + ~100 UWP apps render; AppListEntry=none junk filtered. | ✓ |
| Cap at 50 | Cap to first ~50 rows so the list never looks overwhelming. | |

**User's choice:** Full app list, plain alphabetical, first row selected, everything filtered.
**Notes:** Mouse hover also selects rows (LAUN-05 click-to-launch). No cap — virtual scrolling handles overflow.

---

## Ranking feel

| Option | Description | Selected |
|--------|-------------|----------|
| Exact > prefix > boundary | Exact name > name prefix > word-boundary starts > any subsequence (with camelCase bonuses). `cal`→Calculator, `term`→Terminal, `note`→Notepad. | ✓ |
| Subsequence only | Just subsequence distance — simpler scorer but word starts stop mattering. | |

| Option | Description | Selected |
|--------|-------------|----------|
| Alphabetical tie-break | Ties break alphabetically (case-insensitive). Predictable, stable. | ✓ |
| Catalog-order bonus | Score bonus for enumeration order — arbitrary, changes with enumeration. | |
| Source preference on ties | Prefer .lnk on exact-name ties. | |

| Option | Description | Selected |
|--------|-------------|----------|
| No cutoff | Any subsequence match scores > 0; single-char queries show everything matching. | ✓ |
| Hard score floor | Scores below a threshold dropped. | |

**User's choice:** Exact > prefix > boundary > subsequence; alphabetical tie-break; no cutoff.
**Notes:** Matcher must return match ranges (start/length) from day one for Phase-5 highlight data.

---

## Catalog refresh

| Option | Description | Selected |
|--------|-------------|----------|
| Startup + age check | Worker thread rebuilds at startup; each show checks age, rebuilds only if older than ~10 min. | ✓ |
| Startup only | Rebuild once after launch; only a restart picks up new apps. | |
| Every show | Re-enumerate on every hotkey show unconditionally. | |

| Option | Description | Selected |
|--------|-------------|----------|
| 10 minutes | Long enough to never burn CPU on back-to-back opens, short enough to pick up new installs. | ✓ |
| Rebuild always | Same cost profile as "every show". | |
| 10 seconds | Near-fresh always, but enumeration cost shows on rapid show/hide cycles. | |

| Option | Description | Selected |
|--------|-------------|----------|
| Silent swap | Old catalog remains usable during ~100ms rebuild; next show has the new one. | ✓ |
| Show "refreshing" hint | Small indicator while rebuild runs. | |

**User's choice:** Startup + 10-minute age check, silent swap.
**Notes:** No DB — in-memory catalog per STACK (sub-100ms enumeration doesn't need persistence).

---

## OpenCode's Discretion

- **Source dedupe:** on case-insensitive display-name collision between .lnk and UWP, prefer the classic .lnk entry, suppress the UWP duplicate (exact full-name equality only).
- **Admin refusal UX (UWP):** transient in-shell status hint, never a modal.
- Enumeration/COM class layout inside `src/win/`, worker handoff mechanism, result-row placeholder visuals (no icon pipeline this phase — icons are Phase 5).

## Deferred Ideas

- Recency ranking + recent apps on empty query (LAUN-07/08) — v2, reaffirmed while choosing alphabetical empty-order.
- Icons (`IShellItemImageFactory` + UWP indirect strings) — Phase 5.
- File search — Phase 4.