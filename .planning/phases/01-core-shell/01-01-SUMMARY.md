---
phase: 01-core-shell
plan: 01
subsystem: toolchain, build, ui-shell
tags: [qt6, qml, cmake, ninja, aqt, windeployqt, windows]

requires:
  - phase: null
    provides: null
provides:
  - Buildable Qt 6.11.1 + QML skeleton (target wisp.exe, URI wisp module, VERSIONINFO 0.1.0)
  - CLI-first build workflow: build.ps1 + CMakePresets (Ninja, dev/release)
  - Placeholder window honoring the Phase-1 window contract (Qt.Tool | FramelessWindowHint, 672x432 canvas, transparent)
affects: [01-core-shell (plans 02/03), 02, 03, 05, 06]

tech-stack:
  added: [Qt 6.11.1 win64_msvc2022_64 (manual installer), CMakePresets v6, Ninja]
  patterns: [CLI-first build driver, dynamic Qt linking (LGPL lock), Qt-owns-PMv2 DPI]

key-files:
  created: [CMakeLists.txt, CMakePresets.json, build.ps1, run.ps1, .gitignore, src/app/main.cpp, src/app/wisp.rc, qml/MainWindow.qml]
  modified: []

key-decisions:
  - "Qt 6.11.1 installed via official installer (aqtinstall mirror failed to resolve 6.11.1 archives) into C:/Qt/6.11.1/msvc2022_64"
  - "qt_standard_project_setup() placed after find_package(Qt6) (CMake command resolution order)"
  - "Qt::WA_TranslucentBackground setFlag dropped — it is a QWidget API; Qt6 Quick translucency = QML color:'transparent' + setDefaultAlphaBuffer(true)"

patterns-established:
  - "build.ps1 wraps vcvars64.bat + cmake --preset inside cmd /c for the Ninja generator"
  - "PowerShell scripts must be ASCII-only — UTF-8 non-ASCII bytes (em-dash) break Windows PowerShell 5.1 parsing (0x94 = smart quote)"

requirements-completed: []

# Metrics
duration: 38min
completed: 2026-08-09
---

# Phase 01 Plan 01: Toolchain + Scaffold Summary

Qt 6.11.1 toolchain installed and verified; wisp.exe builds from the CLI with embedded VERSIONINFO (ProductName wisp, FileVersion 0.1.0, CompanyName TID) and runs showing a frameless transparent placeholder window centered on the primary screen.

## Performance

- **Duration:** 38 min
- **Started:** 2026-08-09T07:40:00Z
- **Completed:** 2026-08-09T08:18:00Z
- **Tasks:** 3
- **Files modified:** 7

## Accomplishments

1. **Task 1 — Qt 6.11.1 toolchain installed and verified.** aqtinstall 3.3.0 failed to resolve 6.11.1 archives ("Failed to locate XML data" — stale mirror index despite `list-qt` showing 6.11.1); user installed manually via the Qt online installer (MSVC 2022 64-bit + Additional libraries). Verified: `qmake -query QT_VERSION` → `6.11.1`, `windeployqt.exe` present, `qml/QtQuick` + `qml/QtQml` + `lib/cmake/Qt6Widgets` present.
2. **Task 2 — CMake scaffold.** CMakeLists.txt (`qt_add_executable(wisp WIN32 ...)`, `qt_add_qml_module(wisp URI wisp QML_FILES qml/MainWindow.qml)`, C++20, dynamic linking only), CMakePresets.json (Ninja dev/release, CMAKE_PREFIX_PATH C:/Qt/6.11.1/msvc2022_64), build.ps1 (vcvars64 + `cmake --preset`/`--build`), .gitignore (build/).
3. **Task 3 — Entry point + placeholder window.** main.cpp (org TID/app wisp/version 0.1.0, `setDefaultAlphaBuffer(true)`, `loadFromModule("wisp","MainWindow")`), wisp.rc VERSIONINFO, placeholder MainWindow.qml (Qt.Tool|FramelessWindowHint, 672x432, transparent, centered on primary screen availableGeometry). Build succeeded; smoke-run alive 2s; VersionInfo verified.

## Deviations from Plan

- **[Rule 1 - Bug] aqtinstall could not install Qt 6.11.1** — Found during: task 1 | aqt 3.3.0 repeatedly failed "Failed to locate XML data for Qt version 6.11.1" from the default mirror (transient/stale index; `list-qt` still listed it) | Fix: user installed manually via Qt online installer — same target layout C:\Qt\6.11.1\msvc2022_64, same version, same architecture | Files modified: none | Verification: qmake -query QT_VERSION = 6.11.1, windeployqt present | Commit: none (machine-level)
- **[Rule 1 - Bug] qt_standard_project_setup() before find_package fails** — Found during: task 2 build | CMake error "Unknown CMake command qt_standard_project_setup" | Fix: reordered find_package(Qt6...) before it | Files modified: CMakeLists.txt | Verification: configure succeeds | Commit: 52fc632
- **[Rule 1 - Bug] Qt::WA_TranslucentBackground setFlag invalid on QQuickWindow** — Found during: task 3 build | C2664: cannot convert Qt::WidgetAttribute to Qt::WindowType | Fix: removed the setFlag call — Qt6 Quick translucency is QML `color: "transparent"` + `setDefaultAlphaBuffer(true)` (both already in place) | Files modified: src/app/main.cpp | Verification: build passes, smoke-run alive | Commit: 52fc632
- **[Rule 1 - Bug] build.ps1 parse failure under Windows PowerShell 5.1** — Found during: task 3 | UTF-8 em-dash in comment read as ANSI 0x94 (smart quote) terminated string early | Fix: ASCII-only script | Files modified: build.ps1 | Verification: script parses + runs | Commit: 52fc632
- **[Rule 1 - Bug] Debug Qt DLLs missing on launch (Qt6Qmld/QtQuickd/Qt6Guid/Qt6Cored)** — Found during: post-task-3 launch | Initial smoke test false-passed because Windows shows a DLL-error DIALOG (process alive but no window) | Fix: build.ps1 + new run.ps1 prepend C:\Qt\6.11.1\msvc2022_64\bin to PATH; re-verified with MainWindowTitle check ("wisp", responding) proving a real window | Files modified: build.ps1, run.ps1 | Verification: window title "wisp", Responding=True | Commit: 520c27e

**Total deviations:** 5 auto-fixed. **Impact:** all resolved; toolchain install path differs from plan (manual installer vs aqtinstall) but lands the identical locked layout.

## Verification Results

1. `powershell -ExecutionPolicy Bypass -File build.ps1` exits 0 (dev preset) — PASS
2. `build/dev/wisp.exe` exists; VersionInfo ProductName=wisp, FileVersion=0.1.0, CompanyName=TID — PASS
3. `powershell -ExecutionPolicy Bypass -File run.ps1` shows a real window (MainWindowTitle "wisp", Responding=True) — PASS (corrected after PATH fix; DLL-error dialog false-positive eliminated)
4. Repo contains no reference to Qt 6.5.3 / msvc2019_64 / 5.15.2 and no SetProcessDpiAwareness call — PASS (repo-wide scan)

## Success Criteria

- [x] Qt 6.11.1 win64_msvc2022_64 installed at C:\Qt and verified (qmake -query QT_VERSION == 6.11.1)
- [x] CMake project builds wisp.exe (Ninja preset, C++20, Qt6 Quick/Qml/Gui/Core, dynamic linking)
- [x] VERSIONINFO embedded: ProductName "wisp", CompanyName "TID", FileVersion 0.1.0
- [x] build.ps1 is the single CLI entry point producing build/dev/wisp.exe
- [x] Placeholder window honors the locked contract: Qt.Tool | FramelessWindowHint, 640x400 surface, transparent, centered, title "wisp"
- [x] Zero DPI-manual-API calls; Qt owns PMv2

## Issues Encountered

None

## Next Phase Readiness

Ready for Plan 01-02 (Theme.qml tokens + full shell + open/close animation). Scaffold builds cleanly; window contract verified on disk.
