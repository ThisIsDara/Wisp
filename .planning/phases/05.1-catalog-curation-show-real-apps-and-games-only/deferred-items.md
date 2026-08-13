# Deferred Items — Phase 05.1, Plan 01 (Curation core)

Discovered during execution but **out of scope** for this plan (not caused by
its changes; no files in this plan's `files_modified` list are involved).
Tracked here so the orchestrator / a future plan can pick them up.

## 1. tst_shell pre-existing failure (windowContract + themeTokens)

- **Found during:** task 3 (full-suite regression)
- **Evidence:** at the worktree base commit `5033147` (05-05 theme polish),
  `qml/Theme.qml` already computes `windowWidth = surfaceWidth(648) + 2*shadowMargin(16) = 680`,
  while `tests/tst_shell.cpp:30,71` asserts `672` (the comment in tst_shell even
  says "surface 640"). The 05-05 commit bumped `surfaceWidth` 640 → 648 without
  updating the test. `git diff 5033147..HEAD -- qml/ tests/tst_shell.cpp` is empty —
  this plan changed neither.
- **Impact:** `ctest --test-dir build/dev` shows 16/17 pass; tst_shell fails on
  2 QML assertions. All 15 other pre-existing suites + new tst_curation pass.
- **Suggestion for the owner:** either restore `surfaceWidth: 640` in Theme.qml
  or update tst_shell.cpp expectations to 680 — a one-line fix in whichever file
  carries the intended geometry. This belongs to the 05-05 theme-polish follow-up
  or a dedicated fix plan, NOT 05.1 curation.

## 2. Environment: wisp.exe lock during build (LNK1168)

- **Found during:** task 1 verification
- **Issue:** a running dev instance of `build/dev/wisp.exe` (left over from an
  earlier session) locks the output file and makes `.\build.ps1` fail with
  `LNK1168: cannot open wisp.exe for writing`. Killed the stale dev process
  (PID 77412) and the build succeeded.
- **Impact:** none — one-off environment condition. If it recurs, kill the
  stale dev process before building.
