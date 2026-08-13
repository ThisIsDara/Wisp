---
phase: 01
slug: 01-core-shell
status: verified
threats_open: 0
asvs_level: 1
created: 2026-08-09
---

# Phase 01 — Security

> Per-phase security contract: threat register, accepted risks, and audit trail.

---

## Trust Boundaries

| Boundary | Description | Data Crossing |
|----------|-------------|---------------|
| Build/deploy environment | build.ps1 + deploy.ps1 invoke local compiler/windeployqt, write only to build/ and build/deploy/ | None (no network, no user data, no secrets) |
| Executable | wisp.exe renders a static QML shell; runs asInvoker (no elevation) | None — no user text, paths, or launch targets yet |
| Test environment | tst_shell links the wisp QML module; reads Theme/MainWindow only | None (no external I/O) |
| Compliance output | packaging/THIRD-PARTY-NOTICES.txt stub, copied to deploy folder | License text only; no fabricated URLs |

---

## Threat Register

| Threat ID | Category | Component | Disposition | Mitigation | Status |
|-----------|----------|-----------|-------------|------------|--------|
| T-01-01 | Tampering | aqtinstall / Qt toolchain | mitigate | Exact version pinned (6.11.1 win64_msvc2022_64, official installer, manually verified); qmake -query QT_VERSION = 6.11.1 + windeployqt presence verified before proceeding (01-01-SUMMARY) | closed |
| T-01-02 | Spoofing | build.ps1 | mitigate | `$ErrorActionPreference = "Stop"`; `Test-Path` guards on qmake.exe, vcvars64.bat with loud `throw` messages; rejects unknown config before building (verified in build.ps1) | closed |
| T-01-03 | Elevation | build artifacts | accept | No secrets, network I/O, or user data in Phase 1; wisp.exe static local UI shell, asInvoker default | closed |
| T-02-01 | Info disclosure | MainWindow.qml | accept | Static shell; no data crosses the boundary in Phase 1 (no user text, file paths, or launch targets until Phase 3) | closed |
| T-02-02 | Spoofing | animation/close path | mitigate | Idempotent dismiss(): `if (closing) return` guard + closing flag; hide() only in closeAnim.onFinished, never mid-animation; onClosing rejects only the first close (verified in qml/MainWindow.qml) | closed |
| T-02-03 | Elevation | window flags | mitigate | `Qt.Tool \| Qt.FramelessWindowHint` only — never Qt.Popup (input swallowing), no WS_EX_NOACTIVATE (would break Phase 2 focus contract); launcher runs asInvoker (verified flags in MainWindow.qml) | closed |
| T-02-04 | DoS | shadow asset | accept | Static repo-committed PNG; generation script is documentation-only, never executed at runtime | closed |
| T-03-01 | Tampering | deploy.ps1 | mitigate | Deploys only from verified local build (build/$Config/wisp.exe) with Test-Path + throw; windeployqt exit code checked; writes only under build/deploy/wisp (verified in deploy.ps1) | closed |
| T-03-02 | Info disclosure | THIRD-PARTY-NOTICES.txt | mitigate | No fabricated URLs/license text — source-offer placeholder explicitly marked TBD Phase 6; verified via UAT (LGPL/6.11.1/dynamic-linking strings, TBD placeholder present) | closed |
| T-03-03 | Elevation | deploy output | accept | Local artifacts only; no admin rights used; VC runtime explicitly delegated to official VC_redist.x64.exe (no unlicensed MSVC DLL copying) | closed |
| T-03-04 | Spoofing | tst_shell | accept | Test-only binary; loads Theme/MainWindow from the wisp module; no external I/O | closed |

*Status: open · closed*
*Disposition: mitigate (implementation required) · accept (documented risk) · transfer (third-party)*

---

## Accepted Risks Log

| Risk ID | Threat Ref | Rationale | Accepted By | Date |
|---------|------------|-----------|-------------|------|
| AR-01-01 | T-01-03 | Phase 1 builds local UI artifacts only; no attack-relevant data exists yet | auditor | 2026-08-09 |
| AR-01-02 | T-02-01 | Static shell with no user input or rendered targets; boundary meaningful from Phase 3 | auditor | 2026-08-09 |
| AR-01-03 | T-02-04 | Shadow PNG is a committed asset; generator script is dev-doc only | auditor | 2026-08-09 |
| AR-01-04 | T-03-03 | Deploy copies Qt DLLs only; MSVC runtime deferred to official redist (Phase 6) | auditor | 2026-08-09 |
| AR-01-05 | T-03-04 | tst_shell is a dev-time test binary with no external I/O | auditor | 2026-08-09 |

*Accepted risks do not resurface in future audit runs.*

---

## Security Audit Trail

| Audit Date | Threats Total | Closed | Open | Run By |
|------------|---------------|--------|------|--------|
| 2026-08-09 | 11 | 11 | 0 | gsd-security-auditor |

---

## Sign-Off

- [x] All threats have a disposition (mitigate / accept / transfer)
- [x] Accepted risks documented in Accepted Risks Log
- [x] `threats_open: 0` confirmed
- [x] `status: verified` set in frontmatter

**Approval:** verified 2026-08-09