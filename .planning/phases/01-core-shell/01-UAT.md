---
status: complete
phase: 01-core-shell
source: [01-01-SUMMARY.md, 01-02-SUMMARY.md, 01-03-SUMMARY.md]
started: 2026-08-09T10:05:00.000Z
updated: 2026-08-09T10:15:00.000Z
---

## Current Test

[testing complete]

## Tests

### 1. Launch — frameless transparent centered widget
expected: `run.ps1` shows a frameless, transparent-backed, centered 672×432 window titled "wisp"
result: pass

### 2. Surface & theme — dark rounded card with shadow
expected: The widget shows a dark (#1E1E1E) surface with softly rounded corners (12px), a subtle border, and a soft shadow halo around the card against whatever is behind the window
result: pass

### 3. Open animation — quick fade+scale-in
expected: On show, the widget animates in over ~150ms: fade from 0 to full opacity while scaling from 0.96 to 1.0, smooth (60fps, no stutter)
result: pass

### 4. Escape dismiss — reverse animation then exit
expected: Pressing Escape plays the reverse animation (~140ms fade-out + scale down), then the window closes and the process exits
result: pass

### 5. Alt+F4 dismiss — same exit path
expected: Pressing Alt+F4 behaves like Escape: reverse animation plays, then clean exit
result: pass

### 6. Deploy — standalone folder runs without Qt on PATH
expected: `build/deploy/wisp/wisp.exe` launches from an environment without Qt on PATH (e.g., double-click from Explorer, or PATH=System32): window appears with the same appearance, ESC exits cleanly
result: pass

### 7. LGPL notices — compliance scaffold present
expected: `build/deploy/wisp/THIRD-PARTY-NOTICES.txt` (and repo `packaging/THIRD-PARTY-NOTICES.txt`) exists and names Qt 6.11.1, LGPLv3, dynamic linking, and the modules in use
result: pass

## Summary

total: 7
passed: 7
issues: 0
pending: 0
skipped: 0

## Gaps

[none yet]
