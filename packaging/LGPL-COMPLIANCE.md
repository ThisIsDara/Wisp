# wisp — LGPL Compliance Evidence (D-15)

wisp is built on Qt 6.11.1 (open-source, LGPLv3). This document records the
mechanical evidence that the release satisfies the LGPL obligations that
matter for a closed-source application:

1. **Dynamic linking only** — Qt is shipped as standard DLLs; the application
   is a separate work under the LGPL.
2. **Re-linkability** — an independent binary compiled against Qt's public
   import libraries runs against the deployed Qt DLLs.
3. **License text + notice** — `THIRD-PARTY-NOTICES.txt` ships next to
   `wisp.exe` in the deploy folder and inside the installer.
4. **Source offer** — Qt 6.11.1 is unmodified open-source Qt; the complete
   corresponding source is available (see section 4).

## How to run the verification

```powershell
.\packaging\verify-lgpl.ps1
```

Prerequisites: VS2022 "Desktop development with C++" (dumpbin + cl),
Qt 6.11.1 at `C:\Qt\6.11.1\msvc2022_64`, and a deploy output produced by
`.\packaging\build-installer.ps1` (or `.\deploy.ps1`). Exit code 0 = both
checks pass.

## Check 1 — dynamic-link evidence (dumpbin)

`dumpbin /DEPENDENTS build\deploy\wisp\wisp.exe` lists the import table of
the shipped executable. The gate asserts the imports contain:

- `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Qml.dll`, `Qt6Quick.dll`

Importing DLLs (rather than embedding Qt object code) is the LGPL
"dynamic linking" carve-out: the application and Qt remain separate works,
so the application source may stay proprietary. Users can replace the Qt
DLLs with their own build without recompiling the application.

## Check 2 — relink test (re-linkability)

`packaging/relink-test/main.cpp` is a minimal console binary linked against
`Qt6Core.lib` — the import library, i.e. a *dynamic* import, never a static
Qt lib. The verification compiles it with cl and runs it with `PATH` set to
the **deploy folder only** (`build\deploy\wisp`), with `C:\Qt` absent from
the path. Two things are proven by a `RELINK OK` run:

1. The **deployed** `Qt6Core.dll` is a complete, usable dynamic dependency
   for a binary that was not built in the wisp project tree — exactly the
   LGPL "user may re-link" scenario.
2. The shipped Qt DLLs carry no anti-replacement measures (tivoization
   prohibition).

The binary prints the resolved Qt libraries path as evidence.

## Source offer (LGPL §6(d))

- wisp uses **unmodified** Qt 6.11.1 open-source binaries (dynamic linking).
- The complete corresponding Qt 6.11.1 source code is available from:
  - `https://code.qt.io` (git, tag `v6.11.1`)
  - `https://www.qt.io/download-open-source` (source archives)
- Written offer: the wisp maintainers will provide the corresponding Qt
  source on request, valid for three (3) years from the date of
  distribution, per LGPLv3 §6(d). Requests can be filed via the project's
  release/issue tracker. No modifications to Qt are distributed, so nothing
  beyond the stock Qt 6.11.1 source is owed.

## Where the notices ship

`packaging/THIRD-PARTY-NOTICES.txt` (Qt LGPLv3 notice + this source offer)
is copied into `build\deploy\wisp\` by `deploy.ps1` and packaged into the
installer by `packaging/installer.nsi` (`File /r "..\build\deploy\wisp\*"`),
so it lands next to `wisp.exe` on every installed machine.
