# Phase 05: Theme & Visual Polish - Pattern Map

**Mapped:** 2026-08-10
**Files analyzed:** 16 (6 new files, 5 new test files, 5 modified files)
**Analogs found:** 14 / 16

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/core/SettingsStore.{h,cpp}` (NEW) | store/service | CRUD (QSettings INI) | `src/core/LaunchHistory.{h,cpp}` | exact |
| `src/core/IconCache.{h,cpp}` (NEW) | utility (bounded LRU) | CRUD (insert/get/evict) | `src/core/LaunchHistory.cpp` (QMutex WR-01 discipline) | role-match |
| `src/core/IconProvider.{h,cpp}` (NEW) | provider (QQuickImageProvider) | request-response (provider thread) | `src/win/WinSearchQuery.h` (pure-function seam + failure out-param) | partial |
| `src/win/WinIconExtractor.{h,cpp}` (NEW) | firewall seam (COM) | request-response | `src/win/WinSearchQuery.{h,cpp}` + `WinStartMenuEnumerator.cpp` | role-match |
| `src/win/WinUwpLogo.{h,cpp}` (NEW) | firewall seam (WinRT) | request-response (file-I/O + WinRT) | `src/win/WinUwpEnumerator.{h,cpp}` | exact |
| `src/win/WinUwpEnumerator.{h,cpp}` (MOD) | firewall seam (WinRT) | event-driven (enumeration) | itself (fill `iconRef` like `buildAumid`) | exact |
| `src/core/ResultsModel.{h,cpp}` (MOD) | model | CRUD read-path | itself (`IsFolderRole` precedent) | exact |
| `src/app/main.cpp` (MOD) | composition root | request-response | itself (context-property registration block) | exact |
| `qml/Theme.qml` (MOD) | token singleton | config | itself (readonly token pattern) | exact |
| `qml/ResultsRow.qml` (MOD) | component (delegate) | event-driven (render) | itself (monogram slot) + RESEARCH Pattern 1/3 sketches | exact |
| `qml/MainWindow.qml` (MOD) | component (window) | event-driven | itself (emptyState + Connections wiring) | exact |
| `tests/tst_iconcache.cpp` (NEW) | test | — | `tests/tst_history.cpp` (QTemporaryDir + QtConcurrent thread test) | exact |
| `tests/tst_settings.cpp` (NEW) | test | — | `tests/tst_history.cpp` (round-trip via temp INI) | exact |
| `tests/tst_icons.cpp` (NEW) | test | — | `tests/tst_model.cpp` (pure-helper slots) + `tst_history.cpp` | role-match |
| `tests/tst_model.cpp` (MOD — NOT a new tst_resultsmodel) | test | — | itself (fixture helpers + new slots; CMake target is `tst_model`) | exact |
| `CMakeLists.txt` (MOD) | config | — | itself (`BUILD_TESTING` block + `set_tests_properties`) | exact |

> **Naming notes for the planner:** (1) RESEARCH.md calls the UWP seam `WinUwpLogo.{h,cpp}` (also referenced as `WinUwpIconResolver` in the orchestrator brief — pick one; research/UI-SPEC consistently use WinUwpLogo). (2) The research says "extend tst_resultsmodel" but the existing suite is `tests/tst_model.cpp` → CMake target `tst_model` (CMakeLists.txt:102) — extend THAT target, do not create tst_resultsmodel. (3) Provider scheme is `image://wispicons/{key}` (research Pattern 1 + UI-SPEC), not `image://wisp/icons/`.

## Pattern Assignments

### `src/core/SettingsStore.{h,cpp}` (store, CRUD)

**Analog:** `src/core/LaunchHistory.{h,cpp}` — **copy the makeSettings factory verbatim** (research: "copy the LaunchHistory makeSettings factory verbatim").

**Header shape** (LaunchHistory.h:16-53 — plain class, no QObject inheritance in the analog; but research sketch adds QObject + NOTIFY for the QML accent binding — planner's call within D-14; the LaunchHistory header is the struct precedent):
```cpp
// LaunchHistory.h:16-21, 48-52 (structure to mirror)
class LaunchHistory
{
public:
    explicit LaunchHistory(const QString &settingsPath = {});
    ...
private:
    mutable QMutex m_mutex;
    QSettings m_settings; // non-copyable member (QSettings) — class is move-less by design
};
```

**makeSettings factory** (LaunchHistory.cpp:24-30 — copy verbatim; SettingsStore uses a NON-colliding key like `theme/accent` inside the same INI):
```cpp
// LaunchHistory.cpp:24-30
QSettings makeSettings(const QString &settingsPath)
{
    if (settingsPath.isEmpty())
        return QSettings(QSettings::IniFormat, QSettings::UserScope,
                         QStringLiteral("TID"), QStringLiteral("wisp"));
    return QSettings(settingsPath, QSettings::IniFormat);
}
```

**Accent read with silent fallback (D-16)** — pattern: value() default + QColor::fromString validity check:
```cpp
// LaunchHistory.cpp:112 (value-with-default precedent)
return m_settings.value(keyFor(kLaunchHistoryGroup, path), 0).toInt();
```

**Persistence discipline:** `m_settings.sync()` after every write (LaunchHistory.cpp:48, 58).

**Threading note (Pitfall 7):** SettingsStore is UI-thread-only by contract — no mutex needed (unlike LaunchHistory's WR-01). Document that; add the mutex only if a worker ever touches it.

---

### `src/core/IconCache.{h,cpp}` (utility, CRUD)

**Analog:** `LaunchHistory.cpp` QMutex discipline (WR-01) — LRU replaces the QSettings body; the mutex pattern is the copy.

**Mutex-guarded access pattern** (LaunchHistory.cpp:44-48 — the READ-MODIFY-WRITE under one lock, which is exactly what LRU get-touch-insert needs):
```cpp
// LaunchHistory.cpp:44-48
const QMutexLocker locker(&m_mutex);
const QString key = keyFor(kLaunchHistoryGroup, entry.targetPath);
m_settings.setValue(key, m_settings.value(key, 0).toInt() + 1);
m_settings.sync();
```

**Reentrancy contract** (from research Pattern 1): provider thread calls `get`/`insert`; `QMutex` (non-recursive) + `QMutexLocker` — never call one public method from inside another.

**Test analog:** `tests/tst_history.cpp:173-210` `concurrentAccessThreadSafe_WR01` — the QtConcurrent reader/writer race test structure copies directly for the LRU's thread-safety smoke test:
```cpp
// tst_history.cpp:187-204
std::atomic<bool> start{ false };
const auto writer = [&] { while (!start.load()) QThread::yieldCurrentThread(); ... };
QFuture<void> w = QtConcurrent::run(writer);
QFuture<void> r = QtConcurrent::run(reader);
start = true;
w.waitForFinished(); r.waitForFinished();
```

---

### `src/core/IconProvider.{h,cpp}` (provider, request-response)

**Analog:** none in-codebase for QQuickImageProvider (first one). Partial analog: `WinSearchQuery.h`'s pure-function seam shape (namespace + tiny struct-free API, failure = null). The implementation sketch is in research Pattern 1 (05-RESEARCH.md:166-187) — planner copies that shape, with the codebase's doc-comment style (see WinSearchQuery.h:29-42 header comments).

**Key contract points to lock in the header comment** (from research Pattern 1 + Pitfall 2/4):
- `requestImage` runs on the engine's dedicated provider thread — blocking extraction is SAFE there (verified Qt 6.11); reentrancy required.
- Cache `QImage` at 64px fixed; downscale to `requestedSize` via `img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)` (research Pattern 1 sketch lines 179-183).
- Failure → null QImage, **never cached**.
- Register with `engine.addImageProvider("wispicons", ...)` BEFORE `loadFromModule` (main.cpp:99 pattern below).

**Sketch to copy** (05-RESEARCH.md:168-187):
```cpp
class IconProvider : public QQuickImageProvider {
public:
    IconProvider(IconCache *cache) : QQuickImageProvider(QQuickImageProvider::Image),
                                     m_cache(cache) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override {
        QImage img = m_cache->get(id);
        if (img.isNull()) {
            img = WinIconExtractor::extract(id); // provider thread — COM-safe here
            if (!img.isNull()) m_cache->insert(id, img);
        }
        *size = img.size();
        if (requestedSize.isValid() && requestedSize != img.size())
            img = img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return img;
    }
private:
    IconCache *m_cache; // owned by main, lives for the engine's lifetime
};
```

---

### `src/win/WinIconExtractor.{h,cpp}` (firewall seam, request-response)

**Analog:** `src/win/WinSearchQuery.{h,cpp}` (namespace seam with pure-parse helpers + live COM function + `bool *ok` out-param) + `WinStartMenuEnumerator.cpp` COM apartment discipline.

**Interface shape** (WinSearchQuery.h:9, 24, 29 — namespace, live function, pure helpers section):
```cpp
// WinSearchQuery.h:9, 24
namespace WinSearchQuery {
QVector<FileResult> queryFiles(const QString &query, bool *ok = nullptr);
// ── Pure helpers (unit-tested in tst_search; the live COM paths call these) ──
```

**COM apartment per call** (WinStartMenuEnumerator.cpp:110-115 — copy verbatim for the extraction thread):
```cpp
// WinStartMenuEnumerator.cpp:110-115
const HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE) {
    qWarning() << "WinStartMenuEnumerator: CoInitializeEx failed" << initHr;
    return entries;
}
const bool weInitialized = (initHr == S_OK);
// ... work ...
if (weInitialized) CoUninitialize();
```

**Core extraction** (05-RESEARCH.md:414-424 — verified MS pattern; copy):
```cpp
ComPtr<IShellItem> item;
HRESULT hr = SHCreateItemFromParsingName(path.toStdWString().c_str(), nullptr,
                                         IID_PPV_ARGS(&item));
ComPtr<IShellItemImageFactory> factory;
if (SUCCEEDED(hr)) hr = item->As(&factory);
HBITMAP hbm = nullptr;
if (SUCCEEDED(hr)) hr = factory->GetImage({64, 64},
        SIIGBF_ICONONLY | SIIGBF_RESIZETOFIT | SIIGBF_SCALEUP, &hbm);
QImage img = QImage::fromHBITMAP(hbm).copy();  // detach before DeleteObject (A1)
DeleteObject(hbm);
```

**Error discipline (D-16):** failure anywhere → null QImage, silent, never cached. Batch-error precedent (per-item qWarning, never abort) at WinStartMenuEnumerator.cpp:128-136.

---

### `src/win/WinUwpLogo.{h,cpp}` (firewall seam, WinRT)

**Analog:** `src/win/WinUwpEnumerator.{h,cpp}` — exact (same projection, same try/catch discipline, same toQString helper).

**Header:** pure decision helpers as free functions + live WinRT function (WinUwpEnumerator.h:12-42 shape):
```cpp
// WinUwpEnumerator.h:19-27 (pure helpers pattern to mirror)
bool isSkippable(bool isFramework, bool hasAppListEntry, bool hasDisplayName);
QString buildAumid(const QString &packageFamilyName, const QString &appId);
QString displayNameOr(const QString &displayName, const QString &fallback);
```

**toQString helper** (WinUwpEnumerator.cpp:13-17 — copy verbatim):
```cpp
// WinUwpEnumerator.cpp:13-17
QString toQString(const winrt::hstring &s)
{
    return QString::fromWCharArray(s.c_str(), int(s.size()));
}
```

**Batch try/catch discipline** (WinUwpEnumerator.cpp:43-86 — copy the nested try/catch shape; every package/step failure is a qWarning, never an abort):
```cpp
// WinUwpEnumerator.cpp:43-49, 69-74
for (const auto &package : packages) {
    try {
        ...
    } catch (const winrt::hresult_error &e) {
        qWarning() << "skipping UWP package:"
                   << QString::fromStdString(winrt::to_string(e.message()));
    }
}
```

**Manifest path:** `PackageManager::GetPackageByFullName` → `package.InstalledLocation().Path()` → `QXmlStreamReader` on `AppxManifest.xml` (namespace-tolerant); `SHLoadIndirectString(L"@{PackageFullName?ms-resource:...}", ...)` for indirect strings; scale-variant probe `base.scale-{400,300,200,150,125,100}.ext` nearest to `devicePixelRatio`; `GetLogo({64,64})` stream → `QImage::fromData` fallback (research Pattern 2 steps 1-6). All-fail → null QImage.

---

### `src/win/WinUwpEnumerator.{h,cpp}` (MOD — fill iconRef)

**Analog:** itself. Fill `entry.iconRef` in the loop where `entry.aumid` is set (WinUwpEnumerator.cpp:66-68). Key derivation: split `aumid` after the **last `!`** → `PackageFullName` = aumid left of last `!`, appId = right; `iconRef = "uwp:" + packageFullName + "|" + appId` (data contract, UI-SPEC §Data Contract). Pure helper for the split (mirror `buildAumid` at WinUwpEnumerator.cpp:26-29 — build a unit-tested free function `QString buildIconRef(const QString &aumid)`):
```cpp
// WinUwpEnumerator.cpp:26-29 (mirror this shape for the new helper)
QString buildAumid(const QString &packageFamilyName, const QString &appId)
{
    return packageFamilyName + u'!' + appId;
}
```
Update the header contract comment (WinUwpEnumerator.h:36-37 says `iconRef ""` — change to document the `uwp:PFN|appId` format). `AppEntry::iconRef` field already exists (AppEntry.h:19).

---

### `src/core/ResultsModel.{h,cpp}` (MOD — iconKey role)

**Analog:** itself. Three touch points, all with existing precedents:

**1. Enum value** (ResultsModel.h:27-33 — add `IconKeyRole` after `IsFolderRole`):
```cpp
// ResultsModel.h:27-33
enum Roles {
    DisplayNameRole = Qt::UserRole + 1,
    SubtitleRole,
    MatchRangesRole,
    AumidRole,
    IsFolderRole, // D-04: QML folder glyph — true for folder file rows only
};
```

**2. roleNames** (ResultsModel.cpp:17-23 — add `{ IconKeyRole, "iconKey" }`):
```cpp
// ResultsModel.cpp:17-23
return {
    { DisplayNameRole, "displayName" },
    ...
    { IsFolderRole, "isFolder" },
};
```

**3. data() switch case** (ResultsModel.cpp:235-237 — copy the IsFolderRole case shape; derivation per the data contract: `Lnk` → `iconRef` passthrough (empty → `"path:" + targetPath`); `Uwp` → `iconRef` (`uwp:...` filled by WinUwpEnumerator); `File` → `"path:" + targetPath`):
```cpp
// ResultsModel.cpp:235-237 (case shape to copy)
case IsFolderRole:
    // D-04: folder file rows only — apps and plain files are false.
    return entry.isFolder;
```
The `entry` resolution (ResultsModel.cpp:218-220 — `row.fromFiles ? m_fileEntries.at(...) : m_entries.at(...)`) already gives access to `entry.iconRef` — no structural change needed.

**Header contract comment** (ResultsModel.h:9-19 — extend with the iconKey shape per source).

---

### `src/app/main.cpp` (MOD — provider + SettingsStore registration)

**Analog:** itself. Three insertions:

**1. Context property before loadFromModule** (main.cpp:45-50 construction block + 96-99 registration — copy the pattern):
```cpp
// main.cpp:95-99
QQmlApplicationEngine engine;
engine.rootContext()->setContextProperty("resultsModel", &resultsModel);
engine.rootContext()->setContextProperty("launchController", &launch);
engine.rootContext()->setContextProperty("fileSearch", &fileSearch);
engine.loadFromModule("wisp", "MainWindow");
```
→ construct `SettingsStore settingsStore;` in the block at main.cpp:45-50, register `setContextProperty("settingsStore", &settingsStore)` at 96-98, and `engine.addImageProvider("wispicons", new IconProvider(&iconCache))` **BEFORE** `loadFromModule` (line 99) — engine takes ownership (research Pattern 1).

**2. Header includes** (main.cpp:7-19 block — add `core/IconCache.h`, `core/IconProvider.h`, `core/SettingsStore.h`, `win/WinIconExtractor.h`, `win/WinUwpLogo.h`).

**3. Lifetime:** IconCache must outlive the engine (provider holds a raw pointer — research sketch comment "owned by main, lives for the engine's lifetime").

---

### `qml/Theme.qml` (MOD — tokens + mutable accent)

**Analog:** itself. Three edits:

**1. accent becomes mutable + derived variants** (Theme.qml:9-10 — the ONLY structural Theme change; D-15):
```qml
// Theme.qml:9-10 (current — to be replaced)
readonly property color accent: "#0078D4"           // selection/row highlight bg (Phase 3)
readonly property color accentLight: "#58A6FF"      // accent-colored TEXT on dark surface
```
```qml
// Replacement (research Pattern 4 sketch, 05-RESEARCH.md:246-249)
property color accent: "#0078D4"                        // written once at startup (D-13/D-16)
readonly property color accentLight: Qt.lighter(accent, 1.45)  // highlight on dark (D-05)
readonly property color accentDark: Qt.darker(accent, 1.4)     // chip on accent bg (D-06)
```
(Rename ripple: `accent` loses readonly; every consumer already binds — verified ResultsRow.qml:21,36,58 and MainWindow.qml:147.)

**2. New tokens** (D-10, UI-SPEC Color + Spacing tables): `hoverBg`, `pressedBg`, `placeholderColor`, `chipTextUnselected`, `chipBgUnselected`, `chipTextSelected`, `chipBgSelected`, `scrollbarThumb`, `scrollbarThumbHover`, `emptyStateGlyphColor` (colors); `iconSize: 32`, `chipRadius: 4`, `chipPadX: 4`, `chipHeight: 20`, `scrollbarThumbWidth: 6`, `scrollbarInset: 2`, `scrollbarRadius: 3` (spacing); `animFade: 120` (animation — both crossfade + scrollbar fade share it, UI-SPEC Animation table). All values from the UI-SPEC tables — **zero literals enter QML** (token-only hard rule).

**3. Derived-token style precedent** (pressedBg = `Qt.darker(surfaceSecondary, 1.15)` — derived in the same style as accentLight/accentDark; no literal).

---

### `qml/ResultsRow.qml` (MOD — icon layer + highlight chips)

**Analog:** itself (monogram slot, MouseArea, token-only) + research Pattern 1/3 sketches (no QML rich-text/chip precedent exists in-codebase — first one).

**Icon layer over the monogram** (D-04 crossfade; 05-RESEARCH.md:189-203 sketch — anchors.fill the existing 32px monogram at ResultsRow.qml:44-60):
```qml
// 05-RESEARCH.md:190-202 (copy; sourceSize must be present — anti-pattern: Image without sourceSize)
Image {
    id: iconImg
    anchors.fill: monogram             // 32px slot (D-01)
    source: "image://wispicons/" + encodeURIComponent(model.iconKey)
    sourceSize: Qt.size(32, 32)
    asynchronous: true
    cache: false                        // D-03: our LRU is the only cache
    opacity: 0
    fillMode: Image.PreserveAspectFit
    Behavior on opacity { NumberAnimation { duration: Theme.animFade } }
    onStatusChanged: if (status === Image.Ready) opacity = 1
    Component.onCompleted: if (status === Image.Ready) opacity = 1 // cached: no flash
}
```

**Selection/hover/pressed backgrounds** (ResultsRow.qml:19-24 — extend the existing ternary with `pressedBg` for mouse-down; hoverBg replaces the inline `Theme.surfaceSecondary`):
```qml
// ResultsRow.qml:19-24 (extend)
color: ListView.isCurrentItem ? Theme.accent
     : hoverArea.containsMouse ? Theme.surfaceSecondary
     : "transparent"
```

**Highlight chips** (LAUN-06, research Pattern 3 — new code, copy the geometry contract): FontMetrics-bound-to-title-text `fm.elidedText(model.displayName, Qt.ElideRight, titleWidth)`; clamp `model.matchRanges` runs to `elided.length()`; rich-text spans `<span style="color:...">` for color + Repeater of `Rectangle { radius: Theme.chipRadius; color: ...; x: fm.advanceWidth(elided.left(start)); width: fm.advanceWidth(runText) + 2*Theme.chipPadX; height: Theme.chipHeight; y: vertically centered on the title line box }` behind the Text; rebuild on width/data/isCurrentItem change ONLY (no per-frame work). Selected remap (D-06): white text + `Theme.chipBgSelected` (accentDark). Title-only (D-08): the subtitle Text at ResultsRow.qml:77-85 is untouched. **Fallback (last resort):** rectangular `background-color` spans (research Pattern 3 fallback note).

---

### `qml/MainWindow.qml` (MOD — empty state + scrollbar + accent wiring)

**Analog:** itself. Three edits:

**1. Accent wiring** (D-13..D-15 — the established Connections pattern, MainWindow.qml:314-320):
```qml
// MainWindow.qml:314-320 (copy this wiring shape for settingsStore)
Connections {
    target: launchController
    function onAdminRequestRefused() {
        root.hintText = "Only desktop apps can run as administrator"
        hintTimer.restart()
    }
}
```
```qml
// Add (research Pattern 4, 05-RESEARCH.md:252-257) — onCompleted fires during load, before first paint:
Component.onCompleted: Theme.accent = settingsStore.accent
Connections {
    target: settingsStore
    function onAccentChanged() { Theme.accent = settingsStore.accent } // Phase-6 picker path
}
```

**2. Scrollbar overlay** (D-12, research Pattern 5 — attach to `resultsView` at MainWindow.qml:171-207):
```qml
// 05-RESEARCH.md:280-288 (copy; overlay — never consumes layout space)
ScrollBar.vertical: ScrollBar {
    id: vbar
    policy: ScrollBar.AsNeeded
    visible: vbar.active || vbar.hovered          // verified props (Qt 6.11)
    opacity: visible ? 1.0 : 0.0
    Behavior on opacity { NumberAnimation { duration: Theme.animFade } }
    contentItem: Rectangle { radius: Theme.scrollbarRadius; color: Theme.scrollbarThumb }
    background: Rectangle { color: "transparent" } // overlay — no track
}
```
(`QtQuick.Controls` already imported at MainWindow.qml:2.)

**3. Empty state glyph** (D-11 — extend the existing emptyState Column at MainWindow.qml:250-274): 16px Segoe MDL2 Assets glyph (U+E721) above the existing message Text; color `Theme.emptyStateGlyphColor` (textSecondary); existing gating `resultsView.count === 0 && fileSearch.indexerOk` (line 253) and copy (lines 259-272) stay verbatim.

---

### New tests (`tst_iconcache`, `tst_settings`, `tst_icons`) + `tst_model.cpp` extension

**Analog:** `tests/tst_history.cpp` (QSettings round-trip + concurrency) and `tests/tst_model.cpp` (model fixture slots).

**Suite skeleton to copy** (tst_history.cpp:33-46 + 212-213):
```cpp
// tst_history.cpp:33-46, 212-213
class TstHistory : public QObject
{
    Q_OBJECT
private slots:
    void recordAndReloadRoundTrip_D10();
    ...
};
...
QTEST_MAIN(TstHistory)
#include "tst_history.moc"
```

**QTemporaryDir seam** (tst_history.cpp:50-53 — copy for tst_settings; every suite round-trips through a REAL temp INI, never %APPDATA%):
```cpp
// tst_history.cpp:50-53
QTemporaryDir dir;
QVERIFY(dir.isValid());
const QString iniPath = dir.filePath(QStringLiteral("wisp.ini"));
LaunchHistory history(iniPath);
```

**tst_settings cases** (from research Validation Map, D-16): missing key → `#0078D4`; corrupt value → `#0078D4`; set→read round-trip; `accentChanged` emitted; persists across instances (new SettingsStore on the same iniPath — the reload pattern at tst_history.cpp:59-67).

**tst_iconcache cases** (D-03): cap honored, oldest evicted, hits re-order, thread-safe insert/get (QtConcurrent race — tst_history.cpp:173-210 verbatim shape).

**tst_icons cases**: key parsing pure functions only (last-`;` split, `uwp:` prefix, plain path) — no COM/WinRT in unit tests (research Wave-0 list; COM verified by the manual UI pass).

**tst_model.cpp extension** (iconKey role): copy the fixture-helper pattern (tst_model.cpp:16-45 — `lnkEntry`/`uwpEntry`/`fileEntry` extended with iconRef fields) + `displayNameAt`-style accessor (tst_model.cpp:47-50) → `data(m.index(row), ResultsModel::IconKeyRole)`; cases: Lnk iconRef passthrough, empty iconRef → `path:` fallback, Uwp prefixed, File → `path:` (research Validation Map).

---

### `CMakeLists.txt` (MOD — sources + test targets)

**Analog:** itself. Two edits:

**1. wisp_core sources** (CMakeLists.txt:13-29 — LNK2019 lesson: EVERY new src source MUST be added here):
```cmake
# CMakeLists.txt:13-29 (add IconCache.cpp, IconProvider.cpp, SettingsStore.cpp,
# WinIconExtractor.cpp, WinUwpLogo.cpp to the list)
qt_add_library(wisp_core STATIC
    src/win/WinHotkey.cpp
    ...
)
```

**2. Test targets** (CMakeLists.txt:81-145 — copy the three-line target pattern + add the new targets to the ENVIRONMENT_MODIFICATION list):
```cmake
# CMakeLists.txt:131-133 (target pattern to copy per new test)
qt_add_executable(tst_history tests/tst_history.cpp)
target_link_libraries(tst_history PRIVATE Qt6::Core Qt6::Gui Qt6::Test wisp_core)
add_test(NAME tst_history COMMAND tst_history)
```
And extend the `set_tests_properties` list at CMakeLists.txt:139-145 with `tst_iconcache tst_settings tst_icons` (the PATH env modification is what makes plain `ctest --test-dir build/dev` work).

---

## Shared Patterns

### 1. QSettings makeSettings factory
**Source:** `src/core/LaunchHistory.cpp:24-30`
**Apply to:** SettingsStore (verbatim), tst_settings (QTemporaryDir seam)
**Rule:** same INI (`%APPDATA%\TID\wisp\wisp.ini`), non-colliding key (`theme/accent`), factory because QSettings is non-copyable/non-movable.

### 2. src/win firewall interface
**Source:** `src/win/WinStartMenuEnumerator.h`, `src/win/WinUwpEnumerator.h`, `src/win/WinSearchQuery.h` (namespace + pure C++ interface + test-seam functions)
**Apply to:** WinIconExtractor, WinUwpLogo (pure parse/decision helpers live in the header as free functions for tst_icons; COM/WinRT detail in the .cpp)

### 3. COM apartment discipline (MTA per call)
**Source:** `src/win/WinStartMenuEnumerator.cpp:110-115` (CoInitializeEx + weInitialized + CoUninitialize)
**Apply to:** WinIconExtractor (called from the provider thread)

### 4. WinRT batch try/catch + toQString
**Source:** `src/win/WinUwpEnumerator.cpp:13-17` (toQString), `:43-86` (nested per-package/per-app try/catch, qWarning never abort)
**Apply to:** WinUwpLogo (manifest read, indirect string, GetLogo fallback — Pitfall 8)

### 5. Context-property injection + provider registration before loadFromModule
**Source:** `src/app/main.cpp:95-99` (setContextProperty × 3 → loadFromModule)
**Apply to:** settingsStore context property; `addImageProvider("wispicons", ...)` at the same point (engine takes ownership)

### 6. QtTest suite structure
**Source:** `tests/tst_history.cpp:33-46,212-213` (Q_OBJECT class + private slots + QTEST_MAIN + .moc include), `tst_model.cpp:16-52` (fixture helpers + accessor lambdas)
**Apply to:** tst_iconcache, tst_settings, tst_icons, tst_model extension

### 7. CMake BUILD_TESTING wiring
**Source:** `CMakeLists.txt:80-145` (qt_add_executable × 3, add_test, set_tests_properties ENVIRONMENT_MODIFICATION)
**Apply to:** the three new test targets; wisp_core source list (LNK2019 rule)

### 8. QML Connections wiring for C++ → QML values
**Source:** `qml/MainWindow.qml:314-320` (Connections on launchController → root.hintText)
**Apply to:** `Connections { target: settingsStore; function onAccentChanged() { Theme.accent = ... } }` (research Pattern 4 — never singleton↔context-property reads, never qmlRegisterSingletonInstance)

### 9. ResultsModel role plumbing
**Source:** `src/core/ResultsModel.h:27-33` (Roles enum) + `ResultsModel.cpp:17-23` (roleNames) + `ResultsModel.cpp:213-259` (data() switch)
**Apply to:** IconKeyRole (same three touch points as IsFolderRole)

### 10. Token-only styling
**Source:** `qml/Theme.qml` (all of it) — QML never introduces literal colors/spacing/durations; every new visual ships as a Theme token first (hard rule, 01-UI-SPEC; UI-SPEC tables list the exact token values)

### 11. Silent-failure discipline (D-16)
**Source:** `src/win/WinStartMenuEnumerator.cpp:128-136` (skip + qWarning, never abort) + `WinUwpEnumerator.cpp:69-74`
**Apply to:** icon extraction failure → null QImage → monogram stays silent; missing/corrupt accent → default #0078D4; failures never cached

## No Analog Found

Files with no close match in the codebase (planner should use RESEARCH.md patterns + UI-SPEC tables instead):

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `src/core/IconProvider.{h,cpp}` | provider | request-response | First QQuickImageProvider in the codebase — no existing provider to copy; use research Pattern 1 sketch (05-RESEARCH.md:166-203) + verified threading contract |
| `qml/ResultsRow.qml` highlight chips | component | render | First rich-text/FontMetrics-metrics usage in the QML — no precedent; use research Pattern 3 (05-RESEARCH.md:229-239) + UI-SPEC chip tokens |
| `qml/MainWindow.qml` scrollbar overlay | component | render | First ScrollBar usage (QtQuick.Controls only imported for it) — use research Pattern 5 (05-RESEARCH.md:276-290) + verified Qt 6.11 props |

## Metadata

**Analog search scope:** `src/core/`, `src/win/`, `src/app/`, `src/tray/`, `qml/`, `tests/`, `CMakeLists.txt` (full repo scan)
**Files scanned:** 46 (38 sources + 4 QML + 13 test files + CMakeLists)
**Pattern extraction date:** 2026-08-10

**Cross-checks for the planner:**
- Provider scheme `wispicons` (research + UI-SPEC agree; UI-SPEC data contract says registered as "wispicons").
- The `tst_model` target (CMakeLists.txt:102) is where the iconKey role tests extend — research's "tst_resultsmodel" naming does not exist in the repo.
- `Theme.qml` line 9-10 edit is the ONLY structural Theme change (D-15 rename ripple) — every accent consumer verified to bind (ResultsRow.qml:21,36,58; MainWindow.qml:147).
- `AppEntry.iconRef` (AppEntry.h:19) already exists — WinUwpEnumerator MOD only fills it; ResultsModel only reads it.
