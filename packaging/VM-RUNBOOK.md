# wisp — Clean-VM Validation Runbook (D-16)

**Purpose:** verify the wisp release installer works on pristine Windows
machines with no dev tools, no Qt, and no VC runtime installed — exactly the
conditions the public release targets.

**Scope:** two VMs — **Windows 10 22H2** and **Windows 11 24H2**.

## Preconditions

- A Win10 22H2 VM snapshot and a Win11 24H2 VM snapshot, both **clean**:
  no Visual Studio, no Qt, no NSIS, no dev tools of any kind.
- No VC++ runtime preinstalled on either VM (the installer must install it
  via its registry-gated VC_redist step — D-14). If a VM already has the
  runtime, note it in the results and still verify install completes.
- `build\deploy\wisp-setup.exe` copied to each VM (share folder or USB).
- The VM has internet access **only if you want to re-test the VC_redist
  download path** — it is not required: the installer carries VC_redist.x64.exe.
- A known installed app on each VM (e.g., Calculator) and a known file in an
  indexed location (e.g., a document in Documents) for steps 5–6.

## Steps (run per VM, record results)

> For each step: mark **PASS** / **FAIL** in the results table below and
> record any deviation from the expected outcome.

### 1. Copy the installer

Copy `wisp-setup.exe` to the VM (e.g., `C:\wisp-setup.exe`).

**Expected:** file present; size ≥ 60 MB.

### 2. Run the installer

Double-click `wisp-setup.exe` (or run from a command prompt).

**Expected:**
- Installer completes **WITHOUT a UAC prompt** (per-user install — D-13).
- Welcome page reads "This will install wisp — the app launcher for Windows."
- Installs to `%LOCALAPPDATA%\Programs\wisp`.
- Start Menu shortcut **"wisp"** appears (Start → All apps).
- If the VC runtime was missing, VC_redist installs silently (no visible
  prompt; machine may need a reboot if the runtime was freshly installed).

### 3. Launch wisp

Launch wisp from the Start Menu shortcut.

**Expected:** no window opens (boots resident-hidden); a **tray icon**
(appears as an accent-colored disc in the notification area) is present.
If the tray icon is hidden, click the notification-area chevron to check.

### 4. Hotkey opens the launcher

Press **Alt+Space** (default hotkey).

**Expected:** the launcher window pops open centered, with a search field
focused, in under a second. Press Escape to dismiss.

### 5. Launch an app

Type the name of a known installed app (e.g., "calc"), select the top result,
press **Enter**.

**Expected:** the app launches and the launcher dismisses.

### 6. Search a file

Type the name of a known file (e.g., a document in Documents), select it,
press **Enter**.

**Expected:** the file result appears in the list and opens with its default
app.

### 7. Hotkey-conflict re-verification

Register **Alt+Space** in another launcher on the VM (e.g., PowerToys Run —
enable its default Alt+Space hotkey). Then, in wisp, open Settings from the
tray menu → Hotkey and try to capture Alt+Space.

**Expected:** wisp detects the conflict — a tray balloon
**"wisp - hotkey in use"** and red conflict labels in the capture dialog
(no crash, no silent takeover). Restore the original wisp hotkey afterward.

### 8. Verify license notices

Open `%LOCALAPPDATA%\Programs\wisp` in Explorer.

**Expected:** `THIRD-PARTY-NOTICES.txt` exists **next to** `wisp.exe`, and
`Qt6Core.dll` is present (dynamic Qt linkage).

## Results

**Verification date:** 2026-08-11 — user-verified on live VMs, status:
**user-approved** (all 8 steps pass on both VMs, no failures, no deviations).

| VM | Step 1 | Step 2 | Step 3 | Step 4 | Step 5 | Step 6 | Step 7 | Step 8 |
|----|--------|--------|--------|--------|--------|--------|--------|--------|
| Win10 22H2 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| Win11 24H2 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |

**Overall verdict (per VM):** APPROVED / FAILED — see details below.

- **Win10 22H2:** APPROVED — installer ran without UAC prompt, installed to
  `%LOCALAPPDATA%\Programs\wisp` with Start Menu shortcut; tray icon present;
  Alt+Space opened the launcher; app launch and file search worked; hotkey
  conflict produced the expected "wisp - hotkey in use" balloon + red labels;
  `THIRD-PARTY-NOTICES.txt` present next to `wisp.exe`.
- **Win11 24H2:** APPROVED — identical results on all 8 steps.

## Failure notes

If any step fails, record for the fix plan:

- **Which VM + step** (e.g., "Win10 22H2, step 4: launcher did not open").
- **Screenshot** of the failure state (save alongside this runbook).
- **Exact error text** (dialog text, console output, event-log entry).
- **What was tried** to work around it.

Do not "fix" the VM to make the step pass — the point is to document the
clean-machine behavior for the release gate.
