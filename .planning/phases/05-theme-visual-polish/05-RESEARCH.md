# Phase 5: Theme & Visual Polish - Research

**Researched:** 2026-08-10
**Domain:** Qt Quick QML theming, async icon pipeline (IShellItemImageFactory / UWP GetLogo), QQuickImageProvider, match highlighting
**Confidence:** HIGH (stack + rendering primitives verified); MEDIUM on the two open research questions (mixed-DPI, UWP indirect strings)

## Summary

Phase 5 has three independent workstreams with two shared rules: **never block the UI thread** and **never introduce literal values into QML** (01-UI-SPEC token-only rule).

**(1) Icon pipeline (criterion 4, D-01..D-04):** The engine calls plain `QQuickImageProvider` methods on a single dedicated provider thread per engine (verified, Qt docs) — so a blocking `requestImage` that extracts icons via COM is architecturally safe: the UI thread never waits, and we need no QtConcurrent machinery inside the provider. Recommended shape: `IconProvider` (QQuickImageProvider, thin adapter) + `IconCache` (bounded LRU ~500 under QMutex, storing 64px QImages ≈ 8 MB) + `WinIconExtractor`/`WinUwpLogo` (src/win firewall seams, unit-testable pure parts). Classic icons: `IShellItemImageFactory::GetImage(64, SIIGBF_ICONONLY|SIIGBF_RESIZETOFIT|SIIGBF_SCALEUP)` (the UI-thread warning from STACK.md is satisfied — we're on the provider thread; `SIIGBF_INCACHEONLY` not needed). UWP icons: manifest-based resolution (AppxManifest.xml `Square44x44Logo` + `SHLoadIndirectString` for `ms-resource:` indirect strings + explicit scale-variant file pick — PowerToys Run's exact strategy), with `DisplayInfo.GetLogo()` stream as fallback. The delegate shows the Phase-3 monogram and crossfades to the icon on `Image.Ready`.

**(2) Match highlighting (LAUN-06, D-05..D-08):** `MatchRangesRole` is already shipped (`[{start,length}]` into the original displayName — verified in ResultsModel.cpp:238-253); this phase is pure rendering. **Critical verified fact:** Qt's rich text HTML subset supports `background-color` spans but NOT `border-radius` (official Qt 6.11.1 doc) — so D-07's "rounded background chip" cannot come from rich text alone. Recommended pattern: rich text spans for per-run COLOR + metric-positioned rounded `Rectangle` chips underneath, using QML `FontMetrics.elidedText()`/`advanceWidth()` (verified available) — which we need anyway because rich text disables `Text.elide`. Elide manually, clamp runs at the elide boundary, rebuild on width/selection change. ~8 visible rows × few runs is trivially fast.

**(3) Theme depth (VISU-02, D-09..D-16):** Extend Theme.qml tokens (hover/pressed/placeholder/scrollbar/empty state), make `accent` mutable and derive `accentLight`/`accentDark` from it (`Qt.lighter`/`Qt.darker`, verified long-standing QtQuick API). Accent flows from a new `SettingsStore` (copy the LaunchHistory `makeSettings` factory verbatim — same `%APPDATA%\TID\wisp\wisp.ini`, one file, non-colliding keys) exposed as a context property (the codebase's established injection pattern — `resultsModel`/`launchController`/`fileSearch` precedent), wired into Theme via MainWindow's `Connections` (avoids the fragile "singleton reads context property" question entirely). Auto-hide overlay scrollbar: `ScrollBar.vertical` attached to the ListView with `policy: AsNeeded` + `visible: active || hovered` + opacity fade (`active` verified in Qt 6.11 docs).

**Primary recommendation:** ship the pipeline as IconProvider + IconCache + WinIconExtractor + WinUwpLogo (testable pure parts in wisp_core, COM/WinRT behind the src/win firewall, following the AppCatalog/WinSearchQuery precedent); render highlight chips with the metrics-chip pattern; add SettingsStore with the makeSettings factory; wire accent through MainWindow → Theme bindings.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Icons render at **32px** — matches the existing 32px monogram placeholder and 44px row; no layout change.
- **D-02:** Extracted icons reach QML via a **QQuickImageProvider** (`image://wisp/icons/{id}`) — off-thread decode, Image.source-based async loading; the STACK-recommended path. Not a model role, not direct file paths.
- **D-03:** Cache is **bounded in-memory LRU** (~500 32px icons ≈ 5-10 MB), no disk persistence. Extraction is sub-10ms via `IShellItemImageFactory`, so re-extract on eviction is fine. Satisfies "cache without unbounded growth".
- **D-04:** While an icon is loading, the row shows the **existing monogram placeholder, crossfading to the real icon** when extraction lands. Reuses the Phase 3 placeholder.
- **D-05:** Highlight color is **accentLight (#58A6FF)** — vibrant on the dark surface, distinct from the accent selection layer.
- **D-06:** **Selected-row remap**: on the accent selection background, matched runs render **white text on a darker accent chip** so highlights stay readable.
- **D-07:** Emphasis is **color + rounded background chip** behind each matched run (strong, Rofi-style) — not just color or weight.
- **D-08:** Highlighting applies to the **title line only**; the secondary metadata line (folder path / package name) is never highlighted.
- **D-09:** The Phase 4 selection treatment (**accent background + vibrant left-edge bar**) is the final form — this phase tunes tokens only (radius, bar width, hover complement). No rework, no entrance animation.
- **D-10:** Full token set added to Theme.qml: **hover bg, pressed bg, search-placeholder color, styled scrollbar, designed empty state**. No decorative extras (glow, gradients) — 60fps bar wins.
- **D-11:** Empty state = **centered short message in textSecondary with a small glyph** (e.g., "No results for 'xyz'"). No suggestions logic.
- **D-12:** Scrollbar = **auto-hide overlay** styled with theme tokens (border/thumb); visible while scrolling / hovering the list.
- **D-13:** Accent is read from the **settings store at startup** (default `#0078D4`); all accent usages (selection bg, left bar, highlight, chips) bind to it. Phase 6's picker writes one value and notifies.
- **D-14:** Introduce a **SettingsStore class** (C++/QSettings over `%APPDATA%\TID\wisp\wisp.ini`) owned by the controller, with `readAccent()`/`setAccent()` — unit-testable, reused by Phase 6. No inline QSettings in main, no QML-side config.
- **D-15:** Accent variants are **derived at runtime**: Theme.qml computes accentDark (selected-remap chip) and accentLight (highlight) from the stored accent via QColor adjustments — one source of truth, so any Phase 6 picked color yields consistent shades.
- **D-16:** Missing/corrupt accent setting → **silent fallback to #0078D4**. No toasts, no tray notifications.

### OpenCode's Discretion
- Worker-thread extraction design (QtConcurrent/QThread per ARCHITECTURE), exact LRU implementation, QQuickImageProvider registration, UWP logo resolution details (indirect strings + scale variants — mandated by criterion 4 but implementation is planner's call), icon extraction failure fallback (e.g., re-extract on demand), HBITMAP → QImage conversion path, empty-state copy text, chip radius/padding values, Theme.qml token names and exact derived-shade algorithms — planner's call within architectural conventions (ARCHITECTURE.md, STACK.md).

### Deferred Ideas (OUT OF SCOPE)
- None from this phase's discussion. v2 exclusions reaffirmed: backdrop blur (VISU-04), accent picker (VISU-03) stay in Phase 6; empty-state suggestions ("Did you mean…") explicitly rejected — would require matcher-side logic.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| VISU-02 | Sleek dark theme — dark surfaces, readable contrast, rounded corners, no light-mode leftovers | Theme.qml token expansion (Focus 4 / Patterns 4-5); accent system wiring D-13..D-16; auto-hide scrollbar + centered empty state (D-10..D-12); literal-gate grep validation |
| LAUN-06 | Matched characters in results highlighted in accent color | MatchRangesRole already shipped (Phase 3, `[{start,length}]` into original displayName, verified ResultsModel.cpp:238-253) — pure rendering via rich-text color spans + metric-positioned rounded chips (Pattern 3); selected-row remap per D-06 |
| (criterion 4) | Real icons async, no UI freeze; UWP icons resolve properly (indirect strings + scale variants); bounded cache | Icon pipeline (Patterns 1-2): provider-thread extraction (verified Qt behavior), IShellItemImageFactory 64px, UWP manifest + SHLoadIndirectString + scale-variant pick (PowerToys precedent), GetLogo fallback, ~500-entry LRU (D-01..D-04) |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Icon extraction (classic .lnk / files) | Backend (src/win firewall) | Provider thread | COM/IShellItemImageFactory must never run on the UI thread (MS rule); established src/win firewall pattern (WinSearchQuery precedent) |
| Icon extraction (UWP logos) | Backend (src/win firewall) | Provider thread | WinRT confined to WinUwp*-style seam; needs the multi-threaded apartment discipline from WinUwpEnumerator.cpp |
| Icon caching (bounded LRU) | Backend (src/core) | — | Pure C++ testable; QMutex-guarded (LaunchHistory WR-01 pattern); only touched from the provider thread |
| Icon delivery to QML | Frontend (QQuickImageProvider) | — | D-02 locked: `image://wispicons/{key}`; provider is a thin adapter registered in main.cpp |
| Match highlighting rendering | Frontend (QML delegate) | — | MatchRangesRole consumed in ResultsRow.qml; data contract already unit-tested at the source |
| Theme tokens + accent derivation | Frontend (QML Theme singleton) | Backend (SettingsStore) | D-13..D-15: accent value flows C++ store → MainWindow wiring → Theme bindings; variants derived in QML via Qt.lighter/Qt.darker |
| Accent persistence | Backend (SettingsStore) | — | D-14: QSettings INI (LaunchHistory makeSettings factory verbatim), controller-owned, unit-testable |
| Scrollbar overlay + empty state | Frontend (QML MainWindow) | — | D-11/D-12: pure presentation in the results area; ScrollBar attached property |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Qt Quick QQuickImageProvider | 6.11.1 (in-tree) | Async icon delivery `image://wispicons/{id}` | D-02 locked; provider methods run on a dedicated provider thread per engine — blocking extraction there never touches the UI thread [VERIFIED: doc.qt.io QQuickImageProvider] |
| QQuickAsyncImageProvider | 6.11.1 (in-tree) | Alternative for per-request thread-pool jobs | Not needed here: extraction is <10ms and the plain provider thread serializes requests fine; noted for completeness only |
| IShellItemImageFactory (shobjidl_core.h) | Windows SDK (in-tree) | Classic app/file/folder icon extraction | STACK.md HIGH-confidence lock; GetImage(size, SIIGBF_ICONONLY\|RESIZETOFIT\|SCALEUP) returns HBITMAP at requested size |
| Windows.ApplicationModel (C++/WinRT) | Windows SDK (in-tree) | UWP logo resolution (manifest, InstalledLocation, DisplayInfo.GetLogo fallback) | Same projection as WinUwpEnumerator (Phase 3); `windowsapp` lib already linked |
| QSettings (Qt Core) | 6.11.1 (in-tree) | SettingsStore persistence | D-14; LaunchHistory makeSettings factory is the verbatim precedent |
| QML FontMetrics / TextMetrics | 6.11.1 (in-tree) | Highlight chip geometry + manual elision | Verified: `elidedText()`, `advanceWidth()` on FontMetrics [VERIFIED: doc.qt.io FontMetrics] |
| ScrollBar (Qt Quick Controls) | 6.11.1 (in-tree) | Auto-hide overlay scrollbar | Verified: `active`, `hovered`, `policy: AsNeeded` [VERIFIED: doc.qt.io ScrollBar] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|--------------|
| SHLoadIndirectString (shlwapi.h) | Windows SDK | Resolve `ms-resource:` manifest values to concrete paths | UWP manifest logo/display-name indirect strings (PowerToys-proven `@{PackageFullName?ms-resource:...}` form) |
| ExtractIconExW (shell32) | Windows SDK | Icon at explicit index (`.lnk` GetIconLocation index > 0) | IShellItemImageFactory has no index concept; rare but real case |
| DisplayInfo.GetLogo() (C++/WinRT) | Windows SDK | UWP icon stream fallback | When manifest parsing fails; read stream → QImage::fromData |
| QQuickTextDocument + QSyntaxHighlighter | 6.11.1 | Per-character background formatting | NOT recommended — rectangular backgrounds only, C++ plumbing per delegate; only if chip metrics misalign |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Metric-positioned chip Rectangles | Rich text `background-color` spans | Spans are rectangular (no `border-radius` in Qt's HTML subset — verified); chips are rounded per D-07 |
| Plain (non-async) QQuickImageProvider on provider thread | QtConcurrent inside QQuickAsyncImageProvider | Extraction <10ms; provider thread serialization is fine at 500 icons; async provider adds job plumbing for zero gain |
| Manifest-first UWP resolution | GetLogo stream only | Manifest is deterministic + picks scale explicitly (PowerToys-proven); GetLogo scale behavior is not documented; keep stream as fallback |
| 64px fixed extraction, downscaled | Request exact requestedSize (32×dpr) | 64px = one cache entry per icon, crisp at 200%, 8MB total; per-size caching doubles entries for no visible gain |
| Context property + MainWindow wiring for accent | QML singleton reading a context property | Singleton↔context-property access is not reliably documented; MainWindow wiring works in all Qt versions and matches the codebase precedent |

**Installation:** No new dependencies. All libraries are in-tree (Qt 6.11.1, Windows SDK, existing ole32/shell32/windowsapp links). The icon work needs no NuGet, no vcpkg, no extra Qt modules.

## Architecture Patterns

### System Architecture Diagram

```
┌──────────────────────────── QML (UI thread) ────────────────────────────┐
│  ResultsRow.qml (delegate)                                              │
│   ├─ Image { source: "image://wispicons/" + model.iconKey               │
│   │          sourceSize: 32×32, cache:false, asynchronous:true }        │
│   │    │  monogram Rectangle underneath → crossfade on Image.Ready      │
│   └─ Text (rich HTML, per-run color spans)                              │
│        └─ Repeater of chip Rectangles (rounded, metric-positioned)      │
│  MainWindow.qml                                                         │
│   ├─ ScrollBar.vertical (AsNeeded, visible: active‖hovered)             │
│   └─ Connections on settingsStore.accentChanged → Theme.accent = …      │
└──────┬──────────────────────────────────────────────┬───────────────────┘
       │ image://wispicons/{key}                      │ Theme.accent
       ▼ (provider thread, per engine)                │ bindings
┌────────────────────────── C++ (wisp_core) ──────────┴───────────────────┐
│  IconProvider::requestImage(key)      SettingsStore (QSettings INI)     │
│   │                                    accent: QColor, NOTIFY           │
│   ├─→ IconCache::get(key)  ──hit──→ QImage (implicit-shared copy)       │
│   │     (QHash+QList LRU, QMutex)      │ miss                           │
│   │                                    ▼                               │
│   │  WinIconExtractor::extract(key)   WinUwpLogo::loadLogo(...)         │
│   │   │  "path;index" → IShellItemImageFactory (64px)   │ manifest+     │
│   │   │  index>0 → ExtractIconExW     │ SHLoadIndirectString+scale pick │
│   │   │  plain path → shell item      │ GetLogo stream fallback         │
│   └─→ insert cache ─→ QImage return   │ (WinRT, MTA apartment)          │
└─────────────────────────────────────────────────────────────────────────┘
   Extraction failure → null QImage → Image.Error → monogram stays (silent)
```

Entry point (user): typing filters the model → delegate recreated per row → Image requests icon → provider thread extracts (cached: instant) → texture upload → crossfade. The accent path is a separate, one-directional data flow: INI → SettingsStore → context property → MainWindow Connections → Theme singleton → all accent-consuming bindings.

### Recommended Project Structure (additions only — existing layout untouched)

```
src/
├── core/
│   ├── IconCache.{h,cpp}        # bounded LRU (QHash + QList order, QMutex) — pure C++, tst
│   ├── IconProvider.{h,cpp}     # QQuickImageProvider (requestImage) — thin adapter
│   └── SettingsStore.{h,cpp}    # QSettings INI accent store — pure C++, tst
├── win/
│   ├── WinIconExtractor.{h,cpp} # key parse + classic extraction seam (COM) — pure parse parts tst
│   └── WinUwpLogo.{h,cpp}       # UWP manifest/indirect-string/scale-variant + GetLogo (WinRT)
├── app/main.cpp                 # SettingsStore + IconProvider registration (BEFORE loadFromModule)
qml/
├── Theme.qml                    # accent mutable + derived variants + new tokens
├── MainWindow.qml               # scrollbar, empty state, accent wiring
└── ResultsRow.qml               # icon crossfade + highlight chips
tests/tst_iconcache, tst_settings, (tst_icons: key parsing)   # QtTest, wisp_core link
```

### Pattern 1: Async icon provider + bounded LRU (D-01..D-04)
**What:** A plain `QQuickImageProvider` whose `requestImage` does cache-lookup → on miss, extraction on the provider thread → cache insert → return `QImage`. Verified engine behavior: "For image providers other than ImageResponse, asynchronous loading is executed on a single thread per engine" — the UI thread never blocks, and no QtConcurrent plumbing is needed for sub-10ms extractions. `requestImage` (QImage) over `requestPixmap` (QPixmap): the engine uploads the texture on the render thread; QImage is a plain data class with implicit sharing — cheap thread-safe copies.
**When to use:** Any phase needing async icon delivery with bounded memory.
**Key contract details:**
- `Image { cache: false }` — WITHOUT this, QML caches provider results in the global QPixmapCache (unbounded), silently defeating D-03's bounded-memory goal. Our LRU is the only cache.
- Register via `engine.addImageProvider("wispicons", new IconProvider(...))` BEFORE `loadFromModule`; engine takes ownership.
- Provider must be reentrant (engine may call from the provider thread): LRU under QMutex; extractor initializes its own COM apartment per call (CoInitializeEx MTA reuse discipline — WinStartMenuEnumerator.cpp:110 precedent).
- Cache key = the full provider id (e.g. `C:\...\chrome.exe;0`); failure → return null QImage (delegate keeps monogram), do NOT cache failures (re-extract is <10ms).
- Cache stores 64px QImages: 64×64×4 = 16KB × 500 = 8 MB — inside D-03's 5-10 MB budget, and crisp on 200% screens (engine asks for sourceSize×dpr = 64 at 200%).
**Example (sketch):**
```cpp
// IconProvider.h — in src/core (links QtQml)
class IconProvider : public QQuickImageProvider {
public:
    IconProvider(IconCache *cache) : QQuickImageProvider(QQuickImageProvider::Image),
                                     m_cache(cache) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override {
        QImage img = m_cache->get(id);
        if (img.isNull()) {                     // miss → extract on THIS (provider) thread
            img = WinIconExtractor::extract(id); // nullptr QImage on failure
            if (!img.isNull()) m_cache->insert(id, img);
        }
        *size = img.size();
        // Engine asked for 32/48/64 (sourceSize × dpr) — downscale the fixed 64px cache
        if (requestedSize.isValid() && requestedSize != img.size())
            img = img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return img;
    }
private:
    IconCache *m_cache; // owned by main, lives for the engine's lifetime
};
```
```qml
// ResultsRow.qml — icon layer over the existing monogram (D-04 crossfade)
Image {
    id: iconImg
    anchors.fill: monogram             // 32px slot (D-01)
    source: "image://wispicons/" + encodeURIComponent(model.iconKey)
    sourceSize: Qt.size(32, 32)
    asynchronous: true
    cache: false                        // D-03: our LRU is the only cache
    opacity: 0
    fillMode: Image.PreserveAspectFit
    Behavior on opacity { NumberAnimation { duration: Theme.animCrossfade } }
    onStatusChanged: if (status === Image.Ready) opacity = 1
    Component.onCompleted: if (status === Image.Ready) opacity = 1 // cached: no flash
}
```
(Style note: `onStatusChanged` is the older but still-valid Qt Quick signal syntax — Qt 6.11 supports both; planner picks.)

### Pattern 2: Classic + UWP extraction seams (src/win firewall)
**What:** Two pure-function seams behind the firewall, mirroring WinSearchQuery/WinUwpEnumerator. The `iconKey` role (new) in ResultsModel derives the provider id per source — the only model change this phase:
- `Lnk`: `iconRef` = `iconPath;index` (already produced; split on the LAST ';' per the locked contract) — fallback to `path:` + targetPath when GetIconLocation failed.
- `Uwp`: `iconRef` filled this phase by WinUwpEnumerator as `uwp:<PackageFullName>|<appId>` (aumid split after the last `!`) — carries the manifest identity without per-request enumeration.
- `File`/folders: `path:` + targetPath (IShellItemImageFactory works on any shell path incl. folders).

**Classic extraction (WinIconExtractor::extract):**
1. Parse key: `uwp:` prefix → delegate to WinUwpLogo; `;` in remainder → split last `;` (path, index); else treat whole key as a file/folder path.
2. `SHCreateItemFromParsingName(path)` → `IShellItem` → QI `IShellItemImageFactory` → `GetImage({64,64}, SIIGBF_ICONONLY | SIIGBF_RESIZETOFIT | SIIGBF_SCALEUP, &hbm)`. Flags verified: RESIZETOFIT=0x0, ICONONLY=0x4, SCALEUP=0x100 [VERIFIED: MS GetImage doc; SCALEUP cross-checked via mirror — MEDIUM]. SCALEUP stretches small icons up; RESIZETOFIT ensures exact size. The MS "never call on the UI thread without INCACHEONLY" rule is satisfied by construction (provider thread).
3. Index > 0 (multi-icon .exe/.dll — IShellItemImageFactory has no index): `ExtractIconExW(path, index, &large, &small, 1)` → `QPixmap::fromWinHICON(hicon)` → QImage → resize 64. Legacy but correct; rare case (most .lnk icons are index 0).
4. `QImage::fromHBITMAP(hbm)` (verified, Qt 6.0+) → `.copy()` (detach) → `DeleteObject(hbm)`. Ownership: `fromHBITMAP`'s borrowed-bits semantics are not stated in the fetched doc excerpt — the copy-before-delete pattern is safe under both interpretations [MEDIUM].
5. Failure anywhere → null QImage (silent; monogram stays — D-16 discipline).

**UWP extraction (WinUwpLogo::loadLogo, WinRT MTA apartment — WinUwpEnumerator.cpp:39-88 discipline):**
1. `PackageManager::GetPackageByFullName(packageFullName)` → `package.InstalledLocation().Path()` (readable non-elevated for current-user packages — PowerToys reads it unprivileged) → read `AppxManifest.xml` (QXmlStreamReader, namespace-tolerant).
2. Find `<Application Id="{appId}">` → `<uap:VisualElements Square44x44Logo="...">`.
3. Logo value handling (the "indirect strings" requirement):
   - Plain relative path (`Assets\X.png`) → base = installDir + relative.
   - `ms-resource:` URI → `SHLoadIndirectString(L"@{PackageFullName?ms-resource:...}", ...)` → concrete path (the documented Win32 indirect-string mechanism; Wox/PowerToys-proven form).
4. Scale-variant pick (the "scale variants" requirement): probe `base.scale-{400,300,200,150,125,100}.ext` (and bare base) in the install dir; pick the variant closest to the current `devicePixelRatio` (prefer exact, then ≥, then largest ≤; contrast-none files preferred over `contrast-white`). Load with `QImage::load`.
5. Fallback: re-enumerate the package's `GetAppListEntries()`, match `AppUserModelId()`, `DisplayInfo().GetLogo({64,64})` → `OpenReadAsync()` → read stream bytes → `QImage::fromData` (handles indirect strings OS-side).
6. All-fail → null QImage.

### Pattern 3: Match-highlight chips (LAUN-06, D-05..D-08)
**What:** The title line renders as rich HTML with per-run color spans + a chip layer of rounded Rectangles positioned with FontMetrics. Rationale (verified): Qt rich text supports `background-color` (rectangular) but NOT `border-radius` (official richtext-html-subset.html, Qt 6.11.1) — rounded chips require metric positioning. Rich text also disables `Text.elide`, so manual elision is mandatory anyway (FontMetrics.elidedText — verified), which makes the chip geometry consistent by construction.
**Steps in the delegate:**
1. `elided = fm.elidedText(model.displayName, Qt.ElideRight, titleWidth)` — `fm` = a `FontMetrics { font: titleText.font }` instance.
2. Clamp each `[start,length]` run from `model.matchRanges` to `elided.length()` (drop runs entirely past the boundary, trim crossing runs) — positions are into the ORIGINAL displayName (locked contract).
3. Build HTML: split elided into matched/unmatched segments → `<span style="color:{accentLight}">` for runs; on `ListView.isCurrentItem` the whole title becomes white (D-06) so spans are skipped/whitened.
4. Chip geometry: `x = fm.advanceWidth(elided.left(start))`, `width = fm.advanceWidth(runText) + 2*chipPadX`; chips as a `Repeater` of `Rectangle { radius: Theme.chipRadius; color: chipColor; y: centered on the line box }` behind the Text. Chips clip to the row via the parent row's clip (rows already clip).
5. Rebuild on: width change (elide boundary moves — `onWidthChanged`), model row data change (query typing recreates delegates anyway), `isCurrentItem` (chip color accentDark vs transparent/accent-tinted; text white vs accentLight — D-06).
6. Color logic: unselected → text accentLight + chip `accent` at low alpha (e.g. accent 20%) or accentDark outline — planner's call; selected → white text + `Theme.accentDark` chip (D-06). **RESOLVED by planner (05-05 t1):** unselected chip = accent at ~20% alpha blended over surface → opaque, per UI-SPEC line 109.
**Performance:** ≤8 visible rows × ≤3 runs, string concat + advanceWidth — sub-0.1ms per row; no per-frame work (rebuild only on change). Well within the 60fps bar.
**Fallback (documented):** if metric misalignment ever shows (subpixel gaps at exotic font features), degrade to rich-text `background-color` spans (rectangular) — violates the "rounded" half of D-07; only as a last resort.

### Pattern 4: Accent system wiring (D-13..D-16)
**What:** SettingsStore (QSettings, LaunchHistory makeSettings factory verbatim — same INI file, non-colliding key like `theme/accent`) → context property `settingsStore` → MainWindow `Connections` sets `Theme.accent` → derived variants update by binding. Avoids the unverified "pragma Singleton reads context properties" path entirely; matches the codebase's context-property precedent (resultsModel/launchController/fileSearch).
```qml
// Theme.qml — accent becomes the single mutable source; variants derived (D-15)
QtObject {
    property color accent: "#0078D4"                       // written once at startup (D-13/D-16)
    readonly property color accentLight: Qt.lighter(accent, 1.45)  // highlight on dark (D-05)
    readonly property color accentDark: Qt.darker(accent, 1.4)     // chip on accent bg (D-06)
}
```
```qml
// MainWindow.qml — one wiring point, runs before first paint (onCompleted fires during load)
Component.onCompleted: Theme.accent = settingsStore.accent
Connections {
    target: settingsStore
    function onAccentChanged() { Theme.accent = settingsStore.accent } // Phase-6 picker path
}
```
```cpp
// SettingsStore.h (sketch) — QSettings value member via makeSettings factory
class SettingsStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor accent READ accent NOTIFY accentChanged)
public:
    explicit SettingsStore(const QString &settingsPath = {}, QObject *parent = nullptr);
    QColor accent() const;
    Q_INVOKABLE void setAccent(const QColor &c);   // persist + sync + emit (Phase 6)
private:
    QSettings m_settings;   // makeSettings(settingsPath) — LaunchHistory.cpp:24 verbatim
};
```
- `readAccent()` semantics: missing or unparseable value → `#0078D4`, silent (D-16). Corrupt = `QColor::fromString` invalid → default. Tests via the `settingsPath` QTemporaryDir seam (LaunchHistory precedent).
- Derived-shade factors: exact values are planner's call (D-15); verify visually that default `#0078D4` yields an accentLight near the old hand-picked `#58A6FF` — `Qt.lighter(1.45)` lands close (MEDIUM, needs visual check). If a picked accent is too dark for a light accentLight (contrast guard), clamp in Theme.qml (planner's call).
- **Rename ripple:** Theme.accent becomes non-readonly — the ONLY Theme change; all consumers already bind.

### Pattern 5: Scrollbar overlay + empty state (D-10..D-12)
**What:** Auto-hide overlay scrollbar via the ScrollBar attached property on the existing ListView; styled entirely from new Theme tokens.
```qml
// MainWindow.qml — on resultsView (ListView)
ScrollBar.vertical: ScrollBar {
    id: vbar
    policy: ScrollBar.AsNeeded
    visible: vbar.active || vbar.hovered          // verified props (Qt 6.11)
    opacity: visible ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: 120 } }
    contentItem: Rectangle { radius: Theme.scrollbarRadius; color: Theme.scrollbarThumb }
    background: Rectangle { color: "transparent" } // overlay — no track
}
```
Empty state (D-11): replace/extend the existing emptyState Column — centered glyph (Segoe MDL2 Assets U+E721 "Search" is the natural fit, present on Win10/11 — planner's call) + message in textSecondary. Keep the existing copy gating (`fileSearch.indexerOk`, query interpolation). New tokens: hover/pressed bgs, placeholderColor, scrollbar* , chip* , emptyStateGlyph font, animCrossfade.

### Anti-Patterns to Avoid
- **Blocking the provider thread with disk/network I/O:** the provider thread serializes ALL image requests — extraction only, no thumbnails, no SQL.
- **QPixmapCache pollution:** any `Image` without `cache:false` double-caches unboundedly (violates D-03).
- **Building HTML on every frame** (e.g., in a `onTextChanged`-style hot path or in `paint`): rebuild only on width/data/selection change.
- **Derived shades as hand-picked constants:** D-15 requires derivation — a Phase-6 picked accent must automatically produce readable accentLight/accentDark.
- **qmlRegisterSingletonInstance("wisp", ..., "Settings", ...)**: runtime-registered types are invisible to qmlcachegen/qmllint (the wisp module is compiled) — Theme.qml referencing `Settings` would fail the module build. Use the MainWindow-wiring pattern instead.
- **`Image` without `sourceSize`:** the provider would receive an invalid requestedSize and return 64px unscaled — layout blowups on the 32px slot.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Icon delivery to QML | Model roles / file paths / QPixmap via context | QQuickImageProvider (D-02 locked) | Off-thread decode + engine texture upload; STACK HIGH-confidence |
| Icon caching | Unbounded QHash / QPixmapCache | Bounded LRU (~500, QMutex) | D-03 "bounded" is a hard criterion; QPixmapCache is global and unbounded |
| UWP indirect-string resolution | Manual pri/`@{}` parsing | SHLoadIndirectString | Documented Win32 API; Wox/PowerToys-proven form |
| Scale-variant selection | Blindly load scale-100 | Explicit variant probe (100..400, nearest to dpr) | Criterion-4 requirement; scale-100 is blurry at 150-200% |
| Rounded highlight chips | Rich text `border-radius` | FontMetrics-positioned Rectangles | `border-radius` is NOT in Qt's HTML subset (verified) |
| Settings persistence | QML-side config / inline QSettings in main | SettingsStore (D-14) | Unit-testable; one settings file; Phase-6 reuse |
| HBITMAP → QImage | Manual DIB bit copy | QImage::fromHBITMAP + copy() | In-tree, Qt 6.0+, handles format conversion |
| Icon-at-index extraction | Patching IShellItemImageFactory | ExtractIconExW for index>0 | Shell factory has no index parameter |

**Key insight:** every "custom" thing here has a native Qt or Windows API that is either in-tree or already linked. The only genuinely new code is the ~150-line LRU + the ~200-line WinUwpLogo resolver — both pure and testable.

## Common Pitfalls

### Pitfall 1: Singleton ↔ context property blindness
**What goes wrong:** `Theme.accent = settingsStore.accent` directly inside Theme.qml (pragma Singleton) resolves undefined on some Qt versions/contexts; `qmlRegisterSingletonInstance` into the compiled "wisp" module breaks qmlcachegen.
**Why it happens:** singleton instantiation context semantics are not reliably documented; runtime type registration is invisible to the module compiler.
**How to avoid:** wire via MainWindow (a normal QML file with full context access) — Component.onCompleted + Connections. Works in every Qt 6 version.
**Warning signs:** undefined Theme properties silently (Phase-1 lesson: unregistered singletons yield undefined, not errors).

### Pitfall 2: Unbounded icon caching via QPixmapCache
**What goes wrong:** D-03's "bounded" silently violated; memory grows with every unique icon ever shown.
**Why it happens:** `Image` caches provider results in the global pixmap cache by default.
**How to avoid:** `cache: false` on every icon Image; LRU is the single cache.
**Warning signs:** memory footprint grows monotonically in profiling.

### Pitfall 3: UI-thread icon extraction (regression risk)
**What goes wrong:** someone "optimizes" by calling the extractor from a model data() or a signal handler.
**Why it happens:** IShellItemImageFactory's UI-thread warning is documented for thumbnails; icons seem "fast enough".
**How to avoid:** keep extraction callable only from the provider thread; unit-test the extractor's thread contract by comment + code review; FrameTimeProbe catches >17ms frames in debug.
**Warning signs:** FrameTimeProbe logs frame overruns during scrolling.

### Pitfall 4: id encoding round-trips
**What goes wrong:** spaces/backslashes/`;` in provider ids (Windows paths!) produce broken or mis-split sources.
**Why it happens:** QML source strings become QUrls; QUrl normalizes backslashes → forward slashes; percent-encoding rules differ between QML and the engine's id extraction.
**How to avoid:** encodeURIComponent in QML; in the provider, parse defensively (split last ';', accept both slash directions — the shell normalizes); 1-minute spike at implementation start to lock the exact round-trip.
**Warning signs:** icons fail only for paths with spaces (very common on Windows).

### Pitfall 5: Rich text disables elide — overflow
**What goes wrong:** switching the title Text to RichText with long app names blows the 44px row width.
**Why it happens:** `Text.elide` is not supported for rich text (verified in Text docs).
**How to avoid:** manual elision via FontMetrics.elidedText — required anyway for chip clamping.
**Warning signs:** titles visibly overflow the row in the UI smoke pass.

### Pitfall 6: Chip/text misalignment
**What goes wrong:** chips drift from glyphs (±1-2px) — looks broken.
**Why it happens:** metrics from a different font/weight than the rendered Text, or elide mismatch.
**How to avoid:** one FontMetrics bound to the SAME font object as the title Text; clamp runs against the elided string; generous chip padding (≥2px) hides subpixel drift.
**Warning signs:** visual check with a word-boundary query on a long title + a narrow row.

### Pitfall 7: QSettings concurrency
**What goes wrong:** SettingsStore + LaunchHistory share one INI; QSettings is not thread-safe (WR-01 lesson).
**Why it happens:** Phase 6 writes from the UI thread; nothing else reads — but the pattern invites cross-thread use.
**How to avoid:** SettingsStore is UI-thread-only (accent read at startup, set from picker); if a worker ever needs it, add the LaunchHistory QMutex discipline.
**Warning signs:** inconsistent accent after Phase 6 lands (test in Phase 6).

### Pitfall 8: WindowsApps ACL surprises
**What goes wrong:** UWP manifest/asset reads fail with access-denied for some packages.
**Why it happens:** package folders carry per-user ACLs; some system packages are restricted even for the current user.
**How to avoid:** try/catch everything in WinUwpLogo (WinUwpEnumerator.cpp:69-85 batch discipline); GetLogo fallback + monogram last resort; never elevate.
**Warning signs:** log warnings (not spam) for the failing packages only.

## Code Examples

Verified patterns from official sources:

### QQuickImageProvider threading contract
```cpp
// Source: doc.qt.io/qt-6/qquickimageprovider.html (Qt 6.11)
// "For image providers other than ImageResponse, asynchronous loading is
//  executed on a single thread per engine."
// requestImage/requestPixmap: "This method may be called by multiple threads,
//  so ensure the implementation of this method is reentrant."
```

### Rich text: what works and what doesn't (D-07 evidence)
```html
<!-- Source: doc.qt.io/qt-6/richtext-html-subset.html (Qt 6.11.1) -->
<span style="color:#58A6FF; background-color:#123;">  <!-- supported (char format) -->
<!-- NOT supported: border-radius (absent from the CSS table), padding on spans
     (table cells only). Consequence: rounded chips need metric positioning. -->
```

### FontMetrics elision + advances (chip geometry)
```qml
// Source: doc.qt.io/qt-6/qml-qtquick-fontmetrics.html (verified methods)
FontMetrics {
    id: fm
    font: titleText.font            // MUST mirror the rendered Text's font
    property string elided: elidedText(model.displayName, Qt.ElideRight, width)
    // fm.elidedText(text, mode, width) -> elided string
    // fm.advanceWidth(text) -> qreal advance in pixels
}
```

### ScrollBar auto-hide
```qml
// Source: doc.qt.io/qt-6/qml-qtquick-controls-scrollbar.html — 'active' documented;
// policy AsNeeded + visible‖hovered is the standard overlay pattern
ScrollBar.vertical: ScrollBar {
    id: vbar
    policy: ScrollBar.AsNeeded
    visible: vbar.active || vbar.hovered
    opacity: visible ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 120 } }
}
```

### IShellItemImageFactory (classic extraction)
```cpp
// Source: learn.microsoft.com .../nf-shobjidl_core-ishellitemimagefactory-getimage
ComPtr<IShellItem> item;
HRESULT hr = SHCreateItemFromParsingName(path.toStdWString().c_str(), nullptr,
                                         IID_PPV_ARGS(&item));
ComPtr<IShellItemImageFactory> factory;
if (SUCCEEDED(hr)) hr = item->As(&factory);
HBITMAP hbm = nullptr;
if (SUCCEEDED(hr)) hr = factory->GetImage({64, 64},
        SIIGBF_ICONONLY | SIIGBF_RESIZETOFIT | SIIGBF_SCALEUP, &hbm);
QImage img = QImage::fromHBITMAP(hbm).copy();  // detach before DeleteObject
DeleteObject(hbm);
```

### QSettings makeSettings factory (SettingsStore precedent)
```cpp
// Source: src/core/LaunchHistory.cpp:24 (project precedent, verified)
QSettings makeSettings(const QString &settingsPath) {
    if (settingsPath.isEmpty())
        return QSettings(QSettings::IniFormat, QSettings::UserScope,
                         QStringLiteral("TID"), QStringLiteral("wisp"));
    return QSettings(settingsPath, QSettings::IniFormat);
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| SHGetFileInfo icon extraction | IShellItemImageFactory::GetImage | Win8/10 era; STACK locked | Arbitrary sizes, DPI-aware HBITMAPs; SHGetFileInfo = fallback only |
| UWP icon via `%ProgramFiles%\WindowsApps` scraping | PackageManager → manifest + SHLoadIndirectString + scale variants | PowerToys-documented; STACK locked | ACL-safe, sanctioned API path |
| QtGraphicalEffects / QML blur | (not in scope — VISU-04) | — | MultiEffect is the in-QML blur tool if ever needed; never Qt5Compat |
| Model-role icons | QQuickImageProvider (D-02) | Phase-5 lock | Off-thread decode, engine texture upload |

**Deprecated/outdated:**
- `SHGetFileInfo` as primary icon API: legacy, size-capped (32/48), DPI-unaware [STACK HIGH].
- `QQuickImageProvider::requestPixmap` over requestImage: fine, but QImage keeps threading semantics simplest [MEDIUM].

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | QImage::fromHBITMAP's borrowed-bits semantics — copy-before-DeleteObject is required (or harmless if it copies) | Pattern 2 | GDI handle-use-after-free crash if it borrows and we don't copy; the copy() makes both safe — LOW risk |
| A2 | SIIGBF_SCALEUP == 0x00000100 (cross-checked via third-party mirror; not in the fetched MS excerpt) | Pattern 2 | Wrong flag value → smaller icons not stretched; compile-time enum use avoids the constant entirely |
| A3 | Package.InstalledLocation() is readable non-elevated for current-user packages (PowerToys reads it unprivileged — strong precedent) | Pattern 2 | Access-denied for some system packages → GetLogo fallback + monogram covers it |
| A4 | GetLogo(Size) fallback stream decode works for most packages (OS resolves indirect strings internally) | Pattern 2 | If it also fails, manifest path is primary anyway — belt-and-suspenders |
| A5 | `Qt.lighter(#0078D4, ~1.45)` lands acceptably near the hand-picked #58A6FF | Pattern 4 | Slightly different highlight shade for default accent — cosmetic, planner tunes factor |
| A6 | Provider id arrives at requestImage percent-decoded / shell tolerates forward slashes | Pitfall 4 | Broken icons for space-containing paths — mitigated by the encoding spike (1 min) + defensive parsing |
| A7 | Theme.qml self-binding (`accentLight: Qt.lighter(accent, …)`) re-evaluates when `accent` is written from outside | Pattern 4 | If not, derived shades go stale after Phase-6 pick — mitigations: also assign variants in the Connections handler (belt) |
| A8 | Segoe MDL2 Assets U+E721 renders on all supported Windows 10/11 installs | Pattern 5 | Glyph tofu box — fall back to a unicode magnifier or no glyph (planner's call at visual check) |

## Open Questions (RESOLVED)

1. **Mixed-DPI IShellItemImageFactory behavior (from STATE.md)** — RESOLVED (05-01/05-02): fixed 64px extraction sidesteps the question (native icons are ≥48 for most apps; 64 covers 200% of 32); one live check on a 150%/200% monitor during the phase (05-05 t3, checkpoint 7).
   - What we know: GetImage returns an HBITMAP at the requested PIXEL size; the shell picks the source icon; MS docs say nothing about per-monitor DPI. The GDI stretch note (BIGGERSIZEOK) implies shell-side scaling is low quality.
   - What's unclear: whether the shell picks a bigger native icon on high-DPI contexts of the calling thread.
2. **UWP GetLogo stream scale behavior** — RESOLVED (05-01 t3): manifest-first path (AppxManifest Square44x44Logo → SHLoadIndirectString → scale variants 100-400 by dpr) is primary; GetLogo is only the fallback and accepts whatever stream it returns; monogram is the last resort.
   - What we know: manifest path (primary) selects scale deterministically; GetLogo is only a fallback.
   - What's unclear: whether GetLogo's stream content honors dpr (not documented).
3. **Exact derived-shade factors (accentLight/accentDark)** — RESOLVED (05-05 t1, per UI-SPEC line 95/120/122): accentLight = `Qt.lighter(accent, 1.45)` targeting ≈ #58A6FF at default; accentDark = `Qt.darker(accent, 1.4)`; contrast guard ≥ 4.5:1, tune only at visual checkpoint 8 with 3-4 probe accents; clamp factor if violated.
   - What we know: D-15 delegates the algorithm; Qt.lighter/Qt.darker are the tools.
   - What's unclear: ideal factors for readable contrast across arbitrary Phase-6 accents.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Qt 6.11.1 (Quick, Qml, Controls, Core) | everything | ✓ | 6.11.1 (CMake preset C:/Qt/6.11.1/msvc2022_64) | — |
| MSVC 2022 + vcvars64 (build.ps1) | build | ✓ | v143 | — |
| Windows SDK (shobjidl_core, shlwapi, winrt projections) | icon seams | ✓ | 10.0.26100 (Phase-3 verified) | — |
| ole32 / shell32 / windowsapp links | COM + WinRT | ✓ | already linked in wisp_core | — |
| build.ps1 / ctest | validation | ✓ | established (13 green suites) | — |
| A 150% or 200% DPI display | DPI visual check | ? | — | virtual display / Windows scaling during UI pass |

**Missing dependencies with no fallback:** none — this phase adds zero external dependencies (verified: all APIs are in-tree SDK/Qt).
**Missing dependencies with fallback:** the HiDPI display for the mixed-DPI check — use Windows Display Settings scaling on the dev machine (log out/in), or defer the check to the phase's manual UI pass.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Qt Test (Qt 6.11.1), ctest via build.ps1; established: 13 green suites |
| Config file | none — CMake `if(BUILD_TESTING)` + `tests/tst_*` targets (04-PATTERNS) |
| Quick run command | `build.ps1` (vcvars64 + cmake --build) then `ctest --test-dir build/dev` |
| Full suite command | same — ctest runs all suites; QtTest output via `-o <file>,txt` (Phase-3 lesson: console swallowing) |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| D-03/crit-4 | LRU bounded: cap honored, oldest evicted, hits re-order, thread-safe insert/get | unit | `ctest --test-dir build/dev -R tst_iconcache` | ❌ Wave 0 (new) |
| D-03 | Cache stores ≤ configured cap (500) — "no unbounded growth" proof | unit | same | ❌ Wave 0 |
| D-16 | SettingsStore: missing key → #0078D4; corrupt value → #0078D4; round-trip set→read; accentChanged emitted; persists across instances (QTemporaryDir seam) | unit | `ctest --test-dir build/dev -R tst_settings` | ❌ Wave 0 (new) |
| iconKey | ResultsModel iconKey role: Lnk iconRef passthrough, empty→path fallback, Uwp prefixed, File → path: | unit | `ctest --test-dir build/dev -R tst_resultsmodel` (extend existing) | ✅ exists (extend) |
| key parse | WinIconExtractor key parsing (last-';' split, uwp: prefix, plain path) — pure part only, no COM in tests | unit | `ctest --test-dir build/dev -R tst_icons` | ❌ Wave 0 (new) |
| LAUN-06 | MatchRangesRole shape (already covered) — rendering verified by live check | manual | UI pass checklist: query with 3+ runs, selected/unselected, long-title elide | — |
| VISU-02 | No light-mode leftovers / no literals | lint gate | grep gate: QML files contain zero non-Theme color/spacing literals (established literal-gate pattern) | — |
| crit-4 | No UI freeze during icon load; icons correct for app/file/UWP | manual | live smoke: type across app/file/UWP results; FrameTimeProbe (debug) shows no >17ms frames | — |
| D-04 | Crossfade: monogram → icon without flash | manual | live smoke on cold + warm cache | — |
| D-12 | Scrollbar auto-hide overlay behavior | manual | scroll 8+ results; hover list; assert fade | — |

### Sampling Rate
- **Per task commit:** build via build.ps1 + targeted ctest (`-R` on affected suites)
- **Per wave merge:** full `ctest --test-dir build/dev`
- **Phase gate:** full suite green + manual UI pass (icons, chips, theme, scrollbar) before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/tst_iconcache.cpp` — LRU bounds/eviction/reorder + QMutex smoke (wisp_core link, QtTest)
- [ ] `tests/tst_settings.cpp` — SettingsStore default/corrupt/round-trip/signal (QTemporaryDir seam)
- [ ] `tests/tst_icons.cpp` — icon key parsing pure functions (no COM/WinRT in unit tests — extractor COM code is verified by the manual UI pass)
- [ ] `tests/tst_resultsmodel.cpp` — extend: IconKeyRole derivation cases
- [ ] CMakeLists: register the three new test targets under `if(BUILD_TESTING)`; register new wisp_core sources (LNK2019 lesson: every new src/* source MUST be added to wisp_core's source list)

## Security Domain

### Applicable ASVS Categories
| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | — |
| V3 Session Management | no | — |
| V4 Access Control | no | — |
| V5 Input Validation | partial | icon provider ids originate from the shell (.lnk GetIconLocation / package manifests) — never trusted as code; parsed defensively, path fragments only used with QImage::load / SHCreateItemFromParsingName (no command execution) |
| V6 Cryptography | no | — |

### Known Threat Patterns for {stack}
| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malicious .lnk icon path (shell-provided string) | Tampering | iconRef is a display-only resource: only parsed for `;` split + passed to shell APIs (which own their own validation); never executed; failures are silent (D-16 discipline) |
| UWP manifest XML from disk | Tampering | QXmlStreamReader (well-formedness enforced); try/catch per package (WinUwpEnumerator.cpp:69-85 batch discipline); no elevation ever |
| Unbounded memory growth via cache | DoS (self) | D-03 bounded LRU + `cache:false` on Image; tst_iconcache asserts the cap |

No new attack surface: the phase reads files the OS already exposes to the process and renders them; the only C++-to-QML surface (iconKey strings) is treated as opaque data.

## Sources

### Primary (HIGH confidence)
- [Context7: doc_qt_io_qt-6_8] QQuickImageProvider/requestImage/requestPixmap (threading contract, reentrancy), QQuickAsyncImageProvider, ScrollBar active/policy, FontMetrics elidedText/advanceWidth, QImage::fromHBITMAP (Qt 6.0+)
- [doc.qt.io/qt-6/richtext-html-subset.html (Qt 6.11.1)] Supported HTML subset: background-color supported; border-radius absent; elide/truncated unsupported for rich text — fetched 2026-08-10
- [doc.qt.io/qt-6/qml-qtquick-text.html (Qt 6.11.1)] Rich text behavior, elide limitation, textFormat — fetched 2026-08-10
- [learn.microsoft.com .../nf-shobjidl_core-ishellitemimagefactory-getimage] GetImage signature, SIIGBF flags (RESIZETOFIT=0x0, ICONONLY=0x4, INCACHEONLY, BIGGERSIZEOK GDI-stretch note), UI-thread warning — fetched 2026-08-10
- [learn.microsoft.com/windows/apps/desktop/modernize/desktop-to-uwp-extensions] ms-resource indirect-string mechanics
- [microsoft.github.io/PowerToys/modules/launcher/plugins/program/] UWP icon strategy: manifest assets path + scale/theme variant selection
- [github.com/microsoft/PowerToys .../UWPApplication.cs] SHLoadIndirectString `@{PackageFullName?ms-resource:...}` resolution form (Wox-proven)
- [doc.qt.io/qt-6/qmllint-warnings-and-errors-context-properties.html (Qt 6.11.1)] Context-property lint; "use a singleton" guidance — the phase's wiring avoids both traps

### Secondary (MEDIUM confidence)
- [stackoverflow 77020283 + qt.io forum 148842] border-radius unsupported in QML rich text — matches official subset table
- [stackoverflow 50217328] UWP icon resolution real-world: SHLoadIndirectString yields scale-100 path; manifest VisualElements + variant files; multi-icon selection
- [learn.microsoft.com (GetImage remarks) + imageen flag mirror] SIIGBF_SCALEUP = 0x100 (cross-check)
- [stackoverflow 79432925] WindowsApps ACL constraint for non-elevated icon reads (confirms A3 concern; GetLogo/manifest precedent)

### Tertiary (LOW confidence — flagged in Assumptions Log)
- QImage::fromHBITMAP ownership semantics (A1)
- GetLogo(Size) stream scale behavior (A2/A4)
- Qt.lighter factor-to-shade mapping for #0078D4 (A5)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — every element verified against Qt 6.11.1 docs / MS docs / PowerToys source in this session
- Architecture: HIGH — provider-thread model + firewall seams match verified engine behavior and codebase precedent
- Pitfalls: HIGH — two verified against official docs (border-radius, elide), rest from codebase lessons (STATE.md)
- UWP specifics: MEDIUM — API behaviors cross-checked against PowerToys/Wox practice, not live-tested; the spike in the plan must validate on a real machine

**Research date:** 2026-08-10
**Valid until:** 2026-08-17 (7 days — Qt/MS API surface stable; re-verify only if Qt 6.12 alters rich-text subset)
