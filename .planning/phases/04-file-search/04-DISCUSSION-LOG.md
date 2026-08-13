# Phase 4: File Search - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md - this log preserves the alternatives considered.

**Date:** 2026-08-10
**Phase:** 4-File Search
**Areas discussed:** Result mix & row design, Search scope & semantics, Typing feel & debounce, Indexer trouble UX

---

## Result Mix & Row Design

| Option | Description | Selected |
|--------|-------------|----------|
| Interleaved by score | Files and apps share one ranked list; score decides order. | |
| Grouped (apps then files) | Apps always sort above files regardless of match strength. | |
| Sectioned with divider | Apps at top, files below a subtle divider, each sorted within its section. | |

**User's choice:** Interleaved by score
**Notes:** Rofi/Spotlight feel; direct score competition between apps and files accepted.

| Option | Description | Selected |
|--------|-------------|----------|
| Filename + path subtitle | Title = filename, subtitle = full path (elided). | |
| Filename + type label | Title = filename, subtitle = "File" or type label. | |
| Full path as title | Title = full path. | |

**User's choice:** Filename + path subtitle
**Notes:** Consistent with existing ResultsRow delegate.

| Option | Description | Selected |
|--------|-------------|----------|
| ~5 files | File results capped at ~5 rows; apps stay dominant. | |
| ~10 files | More depth for heavy file-searching sessions. | |
| No cap | Everything matching shows. | |

**User's choice:** ~5 files

| Option | Description | Selected |
|--------|-------------|----------|
| Show folders, Enter opens | Folders in results; Enter opens in Explorer. | |
| Files only | Folders never appear. | |
| Show folders with distinct glyph | Folders marked visually; Enter opens Explorer. | |

**User's choice:** Show folders with distinct glyph (chose the variant over the plain recommendation)

| Option | Description | Selected |
|--------|-------------|----------|
| Silently normal launch | Ctrl+Shift+Enter on a file = normal launch, no hint. | |
| Refuse with transient hint | Show the UWP-style refusal hint then launch normally. | |

**User's choice:** Silently normal launch

| Option | Description | Selected |
|--------|-------------|----------|
| Text glyph in monogram | Folder glyph as a character in the monogram circle. | |
| Distinct monogram color | Different surface token instead of a glyph. | |

**User's choice:** Text glyph in monogram
**Notes:** Swaps cleanly to real icons in Phase 5.

---

## Search Scope & Semantics

| Option | Description | Selected |
|--------|-------------|----------|
| Index default scope | Whatever SystemIndex covers (profile + libraries by default). | |
| Explicit install roots | Scope to profile + Program Files + ProgramData regardless of config. | |
| Conditional scope | Scoped only when query looks like a program/path. | |

**User's choice:** Index default scope + frequently-launched apps outside the index
**Notes:** User freeform: "Index default scope + apps that get launched very frequently by the users that might not be in the index area. for example I have my WoW.exe in my SSD."

| Option | Description | Selected |
|--------|-------------|----------|
| Filename + path | Match against filename AND full path. | |
| Filename only | Filename matches only. | |
| Include contents | Also search file bodies. | |

**User's choice:** Filename + path

| Option | Description | Selected |
|--------|-------------|----------|
| Plain keywords, AQS escapes | Raw query via GenerateSQLFromUserQuery; AQS works if typed. | |
| Force filename semantics | Wrap query to force filename/path columns. | |
| Full AQS passthrough | Expose full AQS to the user. | |

**User's choice:** Plain keywords, AQS escapes

| Option | Description | Selected |
|--------|-------------|----------|
| All types | No type filter. | |
| Documents only | Document-like extensions only. | |
| Type groups | Docs/images/audio/video/archives groups. | |

**User's choice:** ONLY .exe files (freeform)
**Notes:** "I want it to be an application launcher, so just executable apps." Clarified further: "only .exe files for now." Broader file types explicitly deferred. This narrows the roadmap's Phase-4 scope.

| Option | Description | Selected |
|--------|-------------|----------|
| Track wisp launches only | LaunchController records launches to the INI; each becomes searchable. | |
| Wisp tracking + App Paths | Plus seed from the Win+R App Paths registry. | |
| Wisp tracking + shell history | Plus parse MuiCache/UserAssist. | |

**User's choice:** Track wisp launches only, plus a manual "add .exe" option
**Notes:** User freeform: "Track wisp launches only, but if an app isnt in wisp, I want an option to add .exe files to it." Bootstrap problem explicitly surfaced: manual add is the answer for apps outside Start Menu/index (e.g. WoW.exe).

| Option | Description | Selected |
|--------|-------------|----------|
| Result-row action | Pinned "Add executable…" row in results; native file dialog. | |
| Drop onto tray | Drag-and-drop .exe onto the tray icon. | |
| Settings window (Phase 6) | Defer to the settings UI. | |

**User's choice:** Result-row action

---

## Typing Feel & Debounce

| Option | Description | Selected |
|--------|-------------|----------|
| Fixed 150ms | Fixed debounce on the file query, as the roadmap prescribes. | |
| Adaptive debounce | Pause-based adaptive debounce. | |
| No debounce | Fire on every keystroke, drop stale via generation counter. | |

**User's choice:** Fixed 150ms

| Option | Description | Selected |
|--------|-------------|----------|
| Quiet fill-in | Apps show immediately; file rows arrive ~150ms later, no indicator. | |
| Subtle indicator | "Searching files…" row while the query is in flight. | |
| Wait for full pause | Files only on a settled query. | |

**User's choice:** Quiet fill-in

| Option | Description | Selected |
|--------|-------------|----------|
| Apps only | Empty query = full alphabetical app list, first row selected (unchanged). | |
| Apps + tracked exes | Empty query also shows tracked/manual .exes. | |

**User's choice:** Apps only
**Notes:** D-01/D-02 contract preserved; file search engages only with a non-empty query.

| Option | Description | Selected |
|--------|-------------|----------|
| Generation counter | Drop results from any but the latest generation. | |
| Timestamp check | Drop results older than the newest query timestamp. | |

**User's choice:** Generation counter

---

## Indexer Trouble UX

| Option | Description | Selected |
|--------|-------------|----------|
| On-query check | Lazy worker-side status check when a file query fires. | |
| Poll on every open | Proactive status poll on each show. | |

**User's choice:** On-query check

| Option | Description | Selected |
|--------|-------------|----------|
| 3 states | disabled / building / error, each with its own message. | |
| Single generic message | One message for all trouble. | |

**User's choice:** 3 states: disabled/building/error

| Option | Description | Selected |
|--------|-------------|----------|
| Status row in list | Non-selectable Theme-token row in the list area. | |
| Transient bottom hint | Reuse the admin-refusal hint pattern. | |
| Both | Status row plus transient hint on first discovery. | |

**User's choice:** Status row in list

---

## Deferred Ideas Captured

- Broader file search (non-.exe documents/media) — user: "only .exe files for now"
- Recency ranking + recent apps (LAUN-07/08, v2) — wisp launch-tracking feeds this later
- App Paths / shell-history discovery — rejected (registry parsing + privacy)
