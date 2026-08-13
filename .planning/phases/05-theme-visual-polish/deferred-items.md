# Deferred Items — 05-theme-visual-polish

Out-of-scope discoveries logged during plan execution (per deviation-rule scope
boundary: NOT fixed, only tracked).

## 2026-08-10 (plan 05-05)

| Item | File | Detail | Status |
|------|------|--------|--------|
| Pre-existing hex literals | qml/HotkeyCaptureDialog.qml (lines 134, 182: `#E5484D`, `#FFFFFF`) | Phase-2 file, untouched by Phase 5. Breaks the literal-gate's strictest reading ("grep qml/*.qml excluding Theme.qml → 0 matches") — all Phase-5-touched QML (Theme/ResultsRow/MainWindow) holds zero hex literals. | Fixed file is out of Phase-5 scope; convert to Theme tokens when the settings window (Phase 6) touches this dialog |