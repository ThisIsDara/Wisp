# Phase 6: Tray, Settings, Autostart & Packaging - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-08-11
**Phase:** 6-tray-settings-autostart-packaging
**Areas discussed:** Settings window shape, Accent picker UX, Single-instance & autostart behavior, Installer & LGPL compliance

---

## Settings Window Shape

| Option | Description | Selected |
|--------|-------------|----------|
| Dedicated QML window | New MainSettings.qml built like MainWindow.qml (dark, Theme tokens, concrete widgets) + C++ SettingsController; hosts hotkey capture, accent picker, autostart. Consistent with the launcher's look | ✓ |
| Extend capture dialog | Turn the existing HotkeyCaptureDialog into a small settings sheet — less code but cramped for 3 features + picker | |
| Native Widgets window | Qt Widgets window (QSettings-style settings UI). Breaks the QML-only visual language of the app | |

**User's choice:** Dedicated QML window

| Option | Description | Selected |
|--------|-------------|----------|
| Replace with Settings | Menu becomes Open wisp / Settings / Quit — hotkey capture lives inside the window. Matches success criterion 1 verbatim | |
| Keep Change hotkey… too | Keep both — 'Change hotkey…' opens the capture dialog directly, Settings opens the window. Two paths to the same feature | ✓ |

**User's choice:** Keep Change hotkey… too

| Option | Description | Selected |
|--------|-------------|----------|
| Open fullscreen capture | Settings hotkey row opens the EXISTING fullscreen capture dialog when clicked. Reuses the validated capture flow | ✓ |
| Inline capture in settings | Inline key-listening row inside the settings window — needs a new inline capture implementation | |

**User's choice:** Open fullscreen capture

| Option | Description | Selected |
|--------|-------------|----------|
| Small themed tool window | Same dark shell aesthetic: ~480×360, Theme tokens, auto-centered on open, Esc/click-away closes. Non-modal | ✓ |
| Tabbed layout | Single-window tab layout with header navigation — more structure, more code for 3 settings | |

**User's choice:** Small themed tool window

---

## Accent Picker UX

| Option | Description | Selected |
|--------|-------------|----------|
| Swatches + custom | Row of curated swatches (8-10 handpicked colors that look good on the dark theme) + custom option opening a color dialog | ✓ |
| Swatches only | Simple, but users can't pick arbitrary colors | |
| Free picker only | Full color wheel/gradient picker — maximal freedom, heavier UI, harder to make pretty in 480px window | |

**User's choice:** Swatches + custom

| Option | Description | Selected |
|--------|-------------|----------|
| Apply live, persist on change | Click a swatch → applies instantly (SettingsStore accentChanged already wired); Close settings to keep it | ✓ |
| Apply on Save | Stage a preview, only persist on explicit Save/Apply button | |

**User's choice:** Apply live, persist on change

| Option | Description | Selected |
|--------|-------------|----------|
| Native QColorDialog | Windows color picker, zero QML work, familiar. Slight visual mismatch with the dark theme | |
| Custom QML dialog | Custom dark-themed color dialog in QML — consistent look, but significant extra UI code for hue/sat/value sliders | ✓ |

**User's choice:** Custom QML dialog

| Option | Description | Selected |
|--------|-------------|----------|
| Curated 8-10 | 8-10 handpicked colors tuned for the dark theme (blue, green, red, orange, purple, pink, teal, gold, cyan…) | ✓ |
| Win11 palette | ~50 swatches — comprehensive but visually noisy in a 480px window | |
| Minimal set | Accent only + custom — fewest swatches, picker does the rest | |

**User's choice:** Curated 8-10

---

## Single-Instance & Autostart Behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Named mutex + signal | CreateMutexW at startup; second instance signals the first to show the launcher then exits. Pure Win32 | ✓ |
| QSharedMemory | Qt canonical single-instance — heavier, 'surface existing' still needs extra plumbing | |
| Process scan | Detect via process enumeration — fragile, slow, no clean signaling channel | |

**User's choice:** Named mutex + signal

| Option | Description | Selected |
|--------|-------------|----------|
| Settings toggle only | Toggle switch/checkbox row in the settings window — SYS-02 verbatim | ✓ |
| Settings + tray item | Two places to manage the same state | |

**User's choice:** Settings toggle only

| Option | Description | Selected |
|--------|-------------|----------|
| Boot to tray, window hidden | Runs quiet with tray present, window hidden — resident-citizen feel | ✓ |
| Show window at boot | Boot and show the launcher window immediately — intrusive at every sign-in | |

**User's choice:** Boot to tray, window hidden

| Option | Description | Selected |
|--------|-------------|----------|
| Quoted path + --autostart | `"C:\...\wisp.exe" --autostart` — quoted path + arg; wisp parses it to skip the first show | ✓ |
| Bare path | Unquoted — breaks on spaces in the install path (common: Program Files) | |

**User's choice:** Quoted path + --autostart

---

## Installer & LGPL Compliance

| Option | Description | Selected |
|--------|-------------|----------|
| Per-user, no admin | Installs to %LOCALAPPDATA%\Programs\wisp, Start Menu shortcut, no elevation prompts | ✓ |
| Machine-wide + UAC | Program Files + UAC elevation — admin prompt friction for a consumer launcher | |

**User's choice:** Per-user, no admin

| Option | Description | Selected |
|--------|-------------|----------|
| Download if missing | Installer checks for the VC runtime; if missing, downloads & installs VC_redist.x64.exe quietly | ✓ |
| Bundle it | Bundle VC_redist.x64.exe inside the installer — no internet needed, larger installer (~25MB) | |
| Skip handling | Rely on the target machine already having it — risky on clean VMs | |

**User's choice:** Download if missing

| Option | Description | Selected |
|--------|-------------|----------|
| Compliance doc + relink test | docs/LGPL-COMPLIANCE.md or packaging/: NOTICES ships in installer, dynamic-linking proof (relink test), source-offer documented | ✓ |
| Notices only | Just ship THIRD-PARTY-NOTICES.txt — no relink test or source offer beyond what exists | |

**User's choice:** Compliance doc + relink test

| Option | Description | Selected |
|--------|-------------|----------|
| Manual runbook checklist | Scripted VM check documented as a runbook (VM snapshots → install → launch → hotkey → launch app → search file) with a checklist; runs on the user's VMs | ✓ |
| Fully automated | Automate the whole VM validation (packer/ansible/playwright) — heavy setup for a one-shot release gate | |

**User's choice:** Manual runbook checklist

---

## OpenCode's Discretion

- Exact tray icon appearance (accent-aware retouch vs existing generated disc)
- Settings window layout details and row styling
- Named mutex name / signaling channel specifics (WM_COPYDATA vs named event)
- Swatch color values (must be chosen for dark-theme readability)
- Custom QML color dialog internal design
- NSIS script structure and installer branding
- Relink-test build mechanics
- VM runbook file location/format

## Deferred Ideas

- None — discussion stayed within phase scope. v2 exclusions reaffirmed: backdrop blur (VISU-04), recency ranking (LAUN-07/08), copy full path (LAUN-09), code signing (not required).
