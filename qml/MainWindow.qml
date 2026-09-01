import QtQuick
import QtQuick.Controls
import QtQuick.Window
import wisp

Window {
    id: root
    title: "wisp"
    flags: Qt.Tool | Qt.FramelessWindowHint
    width: Theme.windowWidth      // 680 = surface 648 + 2×16 shadow margin
    height: Theme.windowHeight    // 472
    color: "transparent"
    visible: false   // D-02.1: resident — starts hidden, hotkey summons it

    // Transient admin-refusal hint text (D-11) — set by the Connections block
    // below, auto-cleared by hintTimer; non-modal by design.
    property string hintText: ""

    // Centered on primary screen availableGeometry (logical px, UI-SPEC Geometry).
    // Applied on every show, not just at creation: Qt tool windows get
    // WM-placed at the screen corner on first show when positioned hidden
    // (observed live: window landed at (0,0) despite onCompleted) — re-centering
    // on show is immune to that and matches spotlight behavior (launcher is
    // never dragged, D-02).
    function centerOnScreen() {
        x = Screen.availableX + Math.round((Screen.availableWidth - width) / 2)
        y = Screen.availableY + Math.round((Screen.availableHeight - height) / 2)
    }
    Component.onCompleted: {
        centerOnScreen()
        // D-13/D-16: read the stored accent once at startup (missing/corrupt
        // → SettingsStore silently falls back to #0078D4). Runs during load,
        // before first paint — every accent consumer (selection bg, left bar,
        // match chips, ▸ glyph) binds to Theme.accent. The reactive path for
        // Phase 6's picker lives in the Connections block below (D-15).
        Theme.accent = settingsStore.accent
        // 2026-08-15: start on the Favorites tab when the user has favorited
        // something (persisted set, seeded into the model before QML load);
        // otherwise All. Read once — subsequent tabs are user-driven.
        resultsModel.favoritesOnly = resultsModel.favoriteCount > 0
    }

    // The animated subtree: entire surface INCLUDING shadow (UI-SPEC rule 4).
    // Scale transform lives on this Item — animate ONLY opacity + Scale (rule 1).
    Item {
        id: shell
        objectName: "shell"
        anchors.fill: parent
        focus: true     // route key events (Keys) to the shell item
        // LAUN-05 keyboard contract (details below). NOTE: while the user
        // types, the search TextField owns focus — these handlers fire for
        // nav/Enter/Escape because searchField's Keys.forwardTo pipes every
        // key through this shell block first (accepted keys never reach the
        // caret; unaccepted character keys fall through to normal text input).
        Keys.onEscapePressed: ctxMenu.visible ? ctxMenu.closeMenu() : dismiss()   // D-08: Escape → close menu first, else close animation
        // INSTANT keyboard follow (D-06): each nav key moves the selection AND
        // jumps the viewport via followSelection() — the launcher never
        // animates under the keyboard. Hover NEVER scrolls (D-02), so the
        // follow lives HERE on the key paths, not on index changes.
        // Hover/kbd arbitration (2026-08-10): every nav key stamps the
        // current time into resultsView.lastKbPressMs. The delegate
        // MouseArea ignores hover selection while the keys are still active
        // (<250ms since the last nav press) — key auto-repeat owns the
        // selection no matter how the resting cursor jitters; once keys go
        // idle, any real cursor movement re-engages hover.
        // Repeat throttling (2026-08-11 user report): holding a nav key flew
        // off after 2-3s — Windows' OS auto-repeat rate takes over and the
        // list "gets momentum". REPEAT SCROLL SPEED IS CONSTANT BY DESIGN:
        // the REAL press moves once and starts a fixed-interval ticker;
        // auto-repeat events (event.isAutoRepeat) are consumed and never move
        // the selection, so the speed while held NEVER changes. The ticker
        // stops on key release.
        Keys.onUpPressed: (event) => {
            resultsView.keyboardActive = true; resultsView.lastKbPressMs = Date.now()
            launcherController.stateNote(event.isAutoRepeat ? "press up AUTO" : "press up REAL idx=" + resultsModel.selectedIndex)
            if (!event.isAutoRepeat) {
                resultsModel.moveSelection(-1)
                followSelection()
                startRepeat(-1)
            }
            event.accepted = true
        }
        Keys.onDownPressed: (event) => {
            resultsView.keyboardActive = true; resultsView.lastKbPressMs = Date.now()
            launcherController.stateNote(event.isAutoRepeat ? "press down AUTO" : "press down REAL idx=" + resultsModel.selectedIndex)
            if (!event.isAutoRepeat) {
                resultsModel.moveSelection(+1)
                followSelection()
                startRepeat(+1)
            }
            event.accepted = true
        }
        Keys.onReturnPressed: (event) => { shell.launchFromKey(event.modifiers); event.accepted = true }
        Keys.onEnterPressed: (event) => { shell.launchFromKey(event.modifiers); event.accepted = true }
        // Release stops the ticker — a held key must never keep scrolling the
        // list after the finger is up (the OS repeat flow ends with a fresh
        // press; releasing mid-tick is the one path that would leak motion).
        // 2026-08-11 (fix): AUTO-repeat releases must NOT stop the ticker —
        // this machine's driver emits a release pair during every repeat
        // cycle (trail: "release auto=true tickerRunning=false" mid-hold at
        // idx=30 — the hold froze "after a few items"). Real releases only
        // (auto=false) stop it. Also accepted: without this the event
        // propagated to the ancestor delivery path and the handler ran TWICE
        // per OS release (the logged pairs).
        Keys.onReleased: (event) => {
            if (event.key === Qt.Key_Up || event.key === Qt.Key_Down
                || event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
                launcherController.stateNote("release key=" + event.key + " auto=" + event.isAutoRepeat + " tickerRunning=" + repeatTimer.running)
                if (!event.isAutoRepeat)
                    repeatTimer.stop()
                event.accepted = true
            }
        }
        // Home/End/PageUp/PageDown have no specific Keys handlers in Qt 6.11
        // (verified against doc.qt.io Keys attached property) — handled here;
        // every other key must fall through so the field keeps typing.
        Keys.onPressed: (event) => {
            switch (event.key) {
            case Qt.Key_PageUp:   resultsView.keyboardActive = true; resultsView.lastKbPressMs = Date.now(); page(event, -7); event.accepted = true; break  // kVisibleRows=7
            case Qt.Key_PageDown: resultsView.keyboardActive = true; resultsView.lastKbPressMs = Date.now(); page(event, +7); event.accepted = true; break
            case Qt.Key_Home:
                resultsModel.selectIndex(0)
                resultsView.keyboardActive = true
                resultsView.lastKbPressMs = Date.now()
                followSelection()
                event.accepted = true
                break
            case Qt.Key_End:
                if (resultsView.count > 0) {
                    resultsModel.selectIndex(resultsView.count - 1)
                    resultsView.keyboardActive = true
                    resultsView.lastKbPressMs = Date.now()
                    followSelection()
                }
                event.accepted = true
                break
            case Qt.Key_H:
                // 05.1: Ctrl+H on the selected row — hide; in show-hidden
                // mode, unhide (mirrors the Ctrl+Enter modifier check in
                // launchFromKey, lines 246-253).
                if (event.modifiers & Qt.ControlModifier) {
                    if (resultsModel.showHidden)
                        resultsModel.unhideSelected()
                    else
                        resultsModel.hideSelected()
                    event.accepted = true
                }
                break
            }
        }
        // PageUp/PageDown repeat through the same constant-speed ticker (the
        // page step only happens on the REAL press — auto-repeats are drop).
        function page(event, delta) {
            if (event.isAutoRepeat)
                return
            resultsModel.moveSelection(delta)
            followSelection()
            startRepeat(delta)
        }

        // 2026-08-11 (5th revision): the pin scroll on the KEY path is
        // ANIMATED with the same duration as the tick's highlight slide
        // (Theme.animNav), so a press at the bottom edge keeps the tick glued
        // while the list slides under it. Before this, the highlight animated
        // 44px in content space while the viewport jumped 44px instantly —
        // the tick visibly rose one row ("goes to the 6th item and then the
        // 7th again") for the slide's duration. The hold ticker's pins stay
        // INSTANT (55ms steps can't chase a 140ms slide) — it stops this
        // animation before assigning contentY directly.
        NumberAnimation {
            id: navPinAnimation
            target: resultsView
            property: "contentY"
            duration: Theme.animNav
            easing.type: Easing.Linear
        }
        // A REAL nav press always starts a FRESH 250ms single-shot (the
        // OS-like initial delay — a small hold moves exactly ONE item, quick
        // taps still get the sliding tick), then the ticker ramps up through
        // 220 → 165 → 124 → 93 → 75ms (momentum: visibly slow start, ~0.7s to
        // cruise speed). The 100ms delay previously let a small hold jump 2+
        // items (user: "right now a small hold jumps 2 items").
        function startRepeat(delta) {
            repeatTimer.delta = delta
            repeatTimer.interval = 250
            repeatTimer.repeat = false
            repeatTimer.restart()
        }

        // The momentum repeat ticker (2026-08-11): single-shot 250ms after the
        // REAL press (OS-like initial delay — a quick tap still gets the 140ms
        // sliding tick), then the interval ramps 220 → 165 → 124 → 93 → 75ms
        // (×0.75 per tick, floored at 75 ≈ 13 rows/sec cruise). The
        // acceleration is OUR curve, never the OS auto-repeat ramp — the list
        // visibly starts slow and gathers speed ("momentum"), and stays
        // constant once at cruise.
        // Stopped on release AND on window hide (a release mid-hide would
        // otherwise never arrive). While the hold mode is active the
        // highlight animates with duration 0 (repeatTimer.running drives
        // ListView.highlightMoveDuration) so the tick tracks the selection
        // exactly — never a lagging "list updates faster than the bar".
        Timer {
            id: repeatTimer
            interval: 250
            repeat: false
            property int delta: 1
                // TEMP-INST-2 (2026-08-11): the follow logic is INLINED here —
                // the event trail proved followSelection() is never entered
                // from this handler (no "FS enter" note), while the identical
                // call from the Keys handlers works. Scope-chain resolution to
                // the shell's JS function fails inside the Timer's compiled
                // handler; ids and property accesses resolve from anywhere, so
                // the inline body cannot miss. Remove the diagnostic notes once
                // confirmed.
                // 2026-08-11 (3rd revision): from/to measured ARITHMETICALLY
                // (floor of contentY bounds) — indexAt(0,0) returns -1 once
                // the viewport is scrolled (boundary hit), which silently
                // killed the BEGIN pin while holding up. Pins fire ON the edge
                // (idx <= from / idx >= to) with ABSOLUTE row targets — the
                // tick lands exactly on the edge (the old ceil overscrolled
                // 40px: "stuck on the 6th item with one below it"), the
                // arrival scroll is sub-pixel (4px, invisible), and every
                // following step's scroll + selection move cancel in one frame:
                // the tick stays glued to the edge while the list slides.
            onTriggered: {
                if (!repeatTimer.repeat) {
                    repeatTimer.interval = 220   // first repeat: slow start
                    repeatTimer.repeat = true
                    repeatTimer.start()
                } else if (repeatTimer.interval > 75) {
                    repeatTimer.interval = Math.max(75, repeatTimer.interval * 0.75)   // momentum: ramp to cruise
                }
                    launcherController.stateNote("tick d=" + delta + " idx=" + resultsModel.selectedIndex)
                    resultsModel.moveSelection(delta)
                    launcherController.stateNote("FS enter idx=" + resultsModel.selectedIndex)
                    try {
                        if (resultsView.count === 0)
                            return
                        var fIdx = resultsModel.selectedIndex
                        var from = Math.floor(resultsView.contentY / Theme.rowHeight)
                        var to = Math.floor((resultsView.contentY + resultsView.height - 1) / Theme.rowHeight)
                        if (fIdx <= from) {
                            navPinAnimation.stop()
                            resultsView.contentY = Math.max(0, fIdx * Theme.rowHeight)
                            launcherController.stateNote("pin BEGIN idx=" + fIdx + " from=" + from + " to=" + to + " y=" + resultsView.contentY.toFixed(0))
                        } else if (fIdx >= to) {
                            navPinAnimation.stop()
                            var maxY = Math.max(0, resultsView.contentHeight - resultsView.height)
                            resultsView.contentY = Math.min((fIdx + 1) * Theme.rowHeight - resultsView.height, maxY)
                            launcherController.stateNote("pin END idx=" + fIdx + " from=" + from + " to=" + to + " y=" + resultsView.contentY.toFixed(0))
                        }
                    } catch (e) {
                        launcherController.stateNote("FS ERR " + e.name + ": " + e.message)
                    }
                }
        }

        // Enter / Ctrl+Shift+Enter (LAUN-04 elevation) — single handler on
        // the shell, modifier-checked per plan. Ctrl+Enter (LAUN-03) reveals
        // files in Explorer; Ctrl+Shift+Enter stays elevated apps-only
        // (D-05 maps files/folders to silent-normal).
        function launchFromKey(modifiers) {
            if ((modifiers & Qt.ControlModifier) && (modifiers & Qt.ShiftModifier))
                launchController.launchSelected(true)    // elevated (apps only — D-05 maps files to normal)
            else if (modifiers & Qt.ControlModifier)
                launchController.revealSelected()        // LAUN-03: Ctrl+Enter → Explorer reveal
            else
                launchController.launchSelected(false)   // normal launch
        }
        // INSTANT viewport follow — call ONLY from keyboard nav paths, never
        // from hover or index-change signals: hover selects without moving
        // the list (D-02), and the keyboard must never animate (D-06).
        // 2026-08-11 (user redesign): pin the selection to the EDGE — the
        // cursor stays put while the tick slides to it (previously: every
        // keypress CENTERED the selection, so the cursor seemed stuck mid-list
        // while the rows scrolled past). Still INSTANT (D-06):
        // positionViewAtIndex jumps, the sliding tick is the only animation.
        // 2026-08-11 (4th revision): the pin reads geometry mid-key-event and
        // sees a degenerate state — every key-path pin logged "from=-1 to=-1"
        // (contentY≈0 with height≈0) and fired spurious edge pins, jumping the
        // list on single presses ("the list isnt consistent"). Deferring the
        // pin one event-loop turn with Qt.callLater reads post-commit geometry
        // (the ListView has processed the currentIndex change by then); the
        // pin still lands in the same frame, before render. The ticker's
        // inline copy is unaffected — its reads are always sane.
        // 2026-08-11 (5th revision): pins animate through navPinAnimation on
        // the key path (single-shot window) and stay instant in hold mode —
        // see the navPinAnimation note.
        // TEMP-INST-1 (2026-08-11, diagnostics): the note + try/catch stay for
        // one more cycle to confirm the degenerate state is gone.
        function followSelection() {
            launcherController.stateNote("FS enter idx=" + resultsModel.selectedIndex)
            Qt.callLater(shell.applyFollow)
        }
        function applyFollow() {
            try {
                if (resultsView.count === 0)
                    return
                var idx = resultsModel.selectedIndex
                var cY = resultsView.contentY
                var h = resultsView.height
                var from = Math.floor(cY / Theme.rowHeight)                       // top visible row
                var to = Math.floor((cY + h - 1) / Theme.rowHeight)               // bottom visible row
                if (from < 0 || to < from) {
                    launcherController.stateNote("FS degenerate cY=" + cY.toFixed(1) + " h=" + h + " idx=" + idx)
                    return
                }
                launcherController.stateNote("FS late idx=" + idx + " from=" + from + " to=" + to)
                // 2026-08-11: positionViewAtIndex proved inert in this app
                // (event trail 04:36: from/to stayed 0-6 across 7 pins, no
                // error) — contentY is a plain Flickable property: assignment
                // always moves the viewport, and Flickable clamps it to bounds.
                // from/to are measured ARITHMETICALLY (floor of contentY
                // bounds) — indexAt(0,0) returns -1 once the viewport is
                // scrolled, silently killing the BEGIN pin on up-holds. Pins
                // fire ON the edge (idx <= from / idx >= to) with ABSOLUTE row
                // targets: the tick lands exactly on the edge — the old ceil
                // overscrolled 40px ("stuck on the 6th item with one below
                // it"); the arrival scroll is sub-pixel (4px, invisible);
                // every following step's scroll + selection move cancel in one
                // frame, so the tick stays glued to the edge while the list
                // slides.
                var animate = !(repeatTimer.running && repeatTimer.repeat)   // single-shot window: slide; hold: jump
                if (idx <= from) {
                    pinTo(Math.max(0, idx * Theme.rowHeight), animate)
                    launcherController.stateNote("pin BEGIN idx=" + idx + " from=" + from + " to=" + to + " y=" + resultsView.contentY.toFixed(0))
                } else if (idx >= to) {
                    var maxY = Math.max(0, resultsView.contentHeight - resultsView.height)
                    pinTo(Math.min((idx + 1) * Theme.rowHeight - resultsView.height, maxY), animate)
                    launcherController.stateNote("pin END idx=" + idx + " from=" + from + " to=" + to + " y=" + resultsView.contentY.toFixed(0))
                }
            } catch (e) {
                launcherController.stateNote("FS ERR " + e.name + ": " + e.message)
            }
        }
        // Shared pin application: stop any in-flight navPinAnimation, then
        // either animate to the target (key-path, synced with the tick's
        // 140ms slide) or assign directly (hold-mode ticker — its inline pins
        // also call this via navPinAnimation.stop()).
        function pinTo(y, animate) {
            navPinAnimation.stop()
            if (animate) {
                navPinAnimation.from = resultsView.contentY
                navPinAnimation.to = y
                navPinAnimation.start()
            } else {
                resultsView.contentY = y
            }
        }
        transform: Scale {
            id: shellScale
            origin.x: width / 2
            origin.y: height / 2
        }

        // Static pre-rendered shadow — a plain Image, no blur/filter (PITFALLS #12)
        Image {
            anchors.fill: parent
            source: "assets/shadow.png"
            opacity: Theme.shadowOpacity
        }

        // Neon halo (2026-08-15): a dim-orange ring just OUTSIDE the bright
        // app ring — reads as a neon tube around the whole app with zero blur
        // cost (D-06: never blur the hot path). It sits behind the opaque
        // surface, so only the outer line shows.
        Rectangle {
            id: appHalo
            anchors.fill: surface
            anchors.margins: -3
            color: "transparent"
            border.color: Theme.appOutlineDim
            border.width: 1
            z: 0
        }

        // The surface
        Rectangle {
            id: surface
            anchors.centerIn: parent
            width: Theme.surfaceWidth     // 648
            height: Theme.surfaceHeight   // 440
            radius: Theme.radiusSurface
            color: Theme.surface
            border.width: 0   // neon outline drawn by the ring overlay (last child, z 1)
            clip: true

            // ── Search field region (Phase-3 vertical slice) ──
            // Focus reality (LAUN-05): THIS field owns focus while typing, so
            // the shell's Keys block would never fire. Keys.forwardTo routes
            // every key through the shell first (above) — nav/Enter/Escape are
            // accepted there and never reach the caret; character keys fall
            // through to normal text input.
            TextField {
                id: searchField
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: viewSeg.left       // 2026-08-15: leave the top-right for the All|Favorites tab
                anchors.rightMargin: Theme.spaceMd
                height: Theme.rowHeight
                verticalAlignment: TextInput.AlignVCenter
                placeholderText: "Type to search apps and files…"  // RESEARCH §7 verbatim
                font.pixelSize: Theme.fontSizeQuery
                color: Theme.textPrimary
                placeholderTextColor: Theme.placeholderColor  // D-10 dedicated token
                selectionColor: Theme.accent
                background: Rectangle { color: "transparent" }
                padding: 0
                leftPadding: Theme.spaceMd
                rightPadding: Theme.spaceMd
                topPadding: 1  // nudge 1px to center between top edge and underline (44 + 2)/2
                Keys.forwardTo: [shell]   // LAUN-05: shell block sees every key
                // Typing debounce — coalesces maniac typing (20ms) so the
                // model doesn't rebuild on every single keystroke. Empty
                // query still fires immediately (clear list instantly).
                Timer {
                    id: queryDebounce
                    interval: 20
                    repeat: false
                    onTriggered: {
                        resultsModel.setQuery(searchField.text)
                        fileSearch.setQuery(searchField.text)
                    }
                }
                onTextChanged: {
                    ctxMenu.closeMenu()
                    if (text.length === 0) {
                        queryDebounce.stop()
                        resultsModel.setQuery(text)
                        fileSearch.setQuery(text)
                    } else {
                        queryDebounce.restart()
                    }
                }
            }


            // Search-field focus identity (2026-08-15 UI pass): a 2px
            // neon-orange underline (appOutline — matches the app outline and
            // separators; the user dropped the accent-blue here, 2026-08-15)
            // that fades in while the field owns focus (the field is focused
            // on every open — LAUN-05 — so this bar IS the launcher's live
            // state). Replaces the old static hairline — a dead 1px separator
            // under the hero field read as "border", not "ready". Fades via
            // the shared 120ms opacity-only micro-animation contract; overlays
            // the 8px list gap, never participates in layout.
            Rectangle {
                anchors.top: searchField.bottom
                anchors.left: parent.left
                anchors.right: viewSeg.left       // track the field's right edge (2026-08-15)
                anchors.rightMargin: Theme.spaceMd
                height: Theme.searchUnderlineHeight
                color: Theme.appOutline
                opacity: searchField.activeFocus ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.animFade } }
            }

            // ── All | Favorites tab (2026-08-15) ──
            // A compact segmented control beside the search field: All = the
            // normal merged list; Favorites = favoritesOnly mode (the model
            // filters m_order to favorited rows). Active segment = neon-orange
            // fill with black text (the app's identity color); inactive =
            // transparent with dim text, brightening on hover. Drives
            // resultsModel.setFavoritesOnly. Vertical identity: fills the
            // 44px field row so it reads as one control.
            Item {
                id: viewSeg
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.rightMargin: Theme.spaceMd
                width: allSeg.width + Theme.spaceXs + favSeg.width
                height: Theme.rowHeight

                Rectangle {
                    id: allSeg
                    anchors.verticalCenter: parent.verticalCenter
                    width: allSegText.implicitWidth + Theme.spaceMd * 2
                    height: Theme.rowHeight - Theme.spaceSm * 2
                    radius: Theme.chipRadius
                    color: resultsModel.favoritesOnly ? "transparent" : Theme.appOutline
                    Text {
                        id: allSegText
                        anchors.centerIn: parent
                        text: "All"
                        color: resultsModel.favoritesOnly
                             ? (allHover.containsMouse ? Theme.textPrimary : Theme.textSecondary)
                             : Theme.surface
                        font.pixelSize: Theme.fontSizeSubtitle
                        font.weight: Theme.fontWeightSemibold
                    }
                    MouseArea {
                        id: allHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: resultsModel.setFavoritesOnly(false)
                    }
                }

                Rectangle {
                    id: favSeg
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: allSeg.right
                    anchors.leftMargin: Theme.spaceXs
                    width: favSegText.implicitWidth + Theme.spaceMd * 2
                    height: Theme.rowHeight - Theme.spaceSm * 2
                    radius: Theme.chipRadius
                    color: resultsModel.favoritesOnly ? Theme.appOutline : "transparent"
                    Text {
                        id: favSegText
                        anchors.centerIn: parent
                        text: "\u2605 Favorites"
                        color: resultsModel.favoritesOnly
                             ? Theme.surface
                             : (favHover.containsMouse ? Theme.textPrimary : Theme.textSecondary)
                        font.pixelSize: Theme.fontSizeSubtitle
                        font.weight: Theme.fontWeightSemibold
                    }
                    MouseArea {
                        id: favHover
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: resultsModel.setFavoritesOnly(true)
                    }
                }
            }

            // ── Results list panel (LAUN-05) — darker inset region ──
            // The neon outline now frames the WHOLE app surface (2026-08-15);
            // this list keeps only the darker panel (listBg — a bit darker
            // than the surface) inset by spaceSm — the "small layout around
            // the list".
            Rectangle {
                id: listFrame
                anchors.top: searchField.bottom
                anchors.topMargin: Theme.spaceSm
                anchors.left: parent.left
                anchors.leftMargin: Theme.spaceSm
                anchors.right: parent.right
                anchors.rightMargin: Theme.spaceSm
                anchors.bottom: footerRow.top
                anchors.bottomMargin: Theme.spaceSm
                color: Theme.listBg
                radius: 0
                z: 0
            }
            ListView {
                id: resultsView
                // Tight inner padding so rows sit INSIDE the orange border;
                // the frame's own spaceSm margin is the outer "layout".
                anchors.fill: listFrame
                anchors.leftMargin: Theme.spaceXs
                anchors.rightMargin: Theme.spaceXs
                anchors.topMargin: Theme.spaceXs
                anchors.bottomMargin: Theme.spaceXs
                clip: true
                focus: false                 // keys live on the shell
                keyNavigationEnabled: false  // shell owns ↑/↓ — never the view
                boundsBehavior: Flickable.StopAtBounds
                cacheBuffer: 3000            // keep ~70 offscreen rows alive — maniac flick stays in cache
                reuseItems: true             // recycle delegates instead of create/destroy
                displayMarginBeginning: 1000
                displayMarginEnd: 1000
                model: resultsModel
                delegate: ResultsRow {
                    // 05.1: right-click → shell opens the in-window curation
                    // menu at the cursor (list-space coords from the
                    // delegate's mapToItem — no popup windows, no transforms).
                    onContextMenuRequested: (index, isHidden, isFavorite, x, y) => ctxMenu.openMenu(index, isHidden, isFavorite, x, y)
                }
                // Selection truth stays in ResultsModel (moveSelection /
                // selectIndex / hover). This binding makes ListView.isCurrentItem
                // track the model property; selectionChanged NOTIFY keeps it
                // live. Guarded on count>0 — no ghost current row pre-catalog.
                currentIndex: resultsView.count > 0 ? resultsModel.selectedIndex : -1
                // Sliding selection (2026-08-11 user redesign): the left accent
                // tick IS the ListView highlight. The viewport follow stays
                // INSTANT and OURS (D-06): highlightFollowsCurrentItem stays
                // TRUE so the view repositions the highlight onto the current
                // item (false froze the tick on row 0 — observed 2026-08-11),
                // but its ANIMATED viewport scroll is neutralized by
                // highlightMoveDuration: 0 below — the view places the
                // highlight instantly and never competes with the pins. The
                // tick's own Behavior on y (below) slides it with the SAME
                // duration + Linear easing as navPinAnimation — tick and list
                // move in lockstep, every frame, always.
                highlightFollowsCurrentItem: true
                // Constant 0: the ListView must NEVER animate the highlight
                // (its internal slide is velocity-based and drifted off the
                // pin scroll — "the indicator falls behind"). The tick slides
                // through its own Behavior on y, which reads the hold-mode
                // duration itself.
                highlightMoveDuration: 0
                highlightResizeDuration: 0
                highlight: Item {
                    id: selTick
                    width: Theme.tickWidth
                    height: Theme.rowHeight
                    // Hidden until a real current row exists (empty query /
                    // pre-catalog) — never a ghost tick.
                    visible: resultsView.count > 0 && resultsView.currentIndex >= 0
                    // OUR slide (2026-08-11): the ListView repositions this
                    // item instantly (highlightMoveDuration: 0 above); this
                    // Behavior animates every reposition with the SAME
                    // duration (hold mode → 0, jumps) and the SAME Linear
                    // easing as navPinAnimation — during a pin scroll both the
                    // tick and the viewport move 140ms linearly, so the tick
                    // stays glued to the edge while the list slides under it.
                    Behavior on y {
                        NumberAnimation {
                            duration: (repeatTimer.running && repeatTimer.repeat) ? 0 : Theme.animNav
                            easing: Easing.Linear
                        }
                    }
                    Rectangle {
                        // Pinned to the LEFT edge regardless of the width the
                        // view assigns the highlight (it mirrors the current
                        // item's size — a centered anchor would drift the tick
                        // mid-row; observed 2026-08-11).
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.tickWidth
                        height: Theme.tickHeight
                        radius: Theme.tickRadius
                        // 2026-08-15: the selection indicator is the EXACT
                        // picked accent (was accentLight — Qt.lighter(1.45)
                        // rendered the tick a paler, washed-out shade of the
                        // selected color). The row bg is accent too, so the
                        // selected row reads uniformly as the chosen color.
                        color: Theme.accent
                    }
                }
                // NEVER auto-scroll the viewport on selection change: hover
                // must not move the list (D-02 — hover only selects; observed
                // 2026-08-10: positionViewAtIndex on index change made the
                // viewport chase the cursor — "list flies by"). Keyboard
                // follow is INSTANT and explicit: the shell Keys handlers
                // call followSelection() after each nav key (D-06).
                // Input-mode arbitration (2026-08-10): while a nav key is
                // held, the auto-repeat must own the selection — neither the
                // row sliding under a stationary cursor NOR resting-cursor
                // micro-jitter may re-select via hover, or key scrolling
                // speed depends on cursor position (observed twice). Rule:
                // keyboardActive stays true; the delegate MouseArea re-engages
                // hover ONLY after lastKbPressMs is older than 250ms.
                property bool keyboardActive: false
                property int lastKbPressMs: 0

                // D-12: auto-hide overlay scrollbar — zero layout footprint
                // (overlay: never consumes list width). Shows only while the
                // list actually scrolls (size < 1.0) AND is being scrolled
                // (active/moving) or hovered (list or thumb); 120ms opacity
                // fade in/out (Theme.animFade — shared with the icon
                // crossfade). Thumb 6px, inset 2px, radius 3 (declared
                // sub-grid exceptions); hovered thumb textSecondary.
                ScrollBar.vertical: ScrollBar {
                    id: vbar
                    policy: ScrollBar.AsNeeded
                    width: Theme.scrollbarWidth + Theme.scrollbarInset   // 6px thumb + 2px inset
                    // 2026-08-11 fix: the template's 2px padding insets the
                    // thumb; the contentItem must NEVER anchor (anchors.fill
                    // pinned it to the full track — "a big scrollbar that
                    // never moves"; the control positions/sizes it from
                    // position+size itself, Basic-style).
                    // 2026-08-17 fix: padding on ALL sides also inset the
                    // thumb VERTICALLY (top/bottom), so at position 0 it
                    // stopped 2px short of the track's top. Inset only the
                    // right edge — the thumb travels the full track.
                    leftPadding: 0
                    topPadding: 0
                    bottomPadding: 0
                    rightPadding: Theme.scrollbarInset
                    visible: vbar.size < 1.0 && (vbar.active || vbar.hovered || resultsView.hovered)
                    opacity: visible ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: Theme.animFade } }
                    contentItem: Rectangle {
                        implicitWidth: Theme.scrollbarWidth
                        implicitHeight: 140   // warm-start thumb height; control sizes it
                        radius: Theme.scrollbarRadius
                        color: vbar.hovered || resultsView.hovered ? Theme.scrollbarThumbHover : Theme.scrollbarThumb
                    }
                    background: Rectangle { color: Theme.scrollbarTrack }  // no track — overlay
                }
            }

            // ── Single-line action footer (2026-08-15) ──
            // "Add folder to scan…", "Add executable…" and "Show hidden (N)"
            // all live on ONE 44px row (the user asked to stop stacking them);
            // that frees vertical space for the results list above. Each
            // action is its own clickable MouseArea — click-only, no focus,
            // never part of the model (the keyboard owns the list).
            Rectangle {
                id: footerRow
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: Theme.rowHeight
                color: "transparent"   // show the black surface — Rectangle defaults to white
                // 1px top hairline — same structural constant as the field
                // separator (declared exception: UI-SPEC hairline rule).
                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.separator
                }

                // Left group: the two "add" actions (both neon-orange +).
                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spaceLg   // aligns with the list's icon column
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spaceXl
                    // "Add folder to scan…" (07-06) — scanning folders is the
                    // launcher's primary inventory path (07-06 pivot).
                    MouseArea {
                        id: addFolderArea
                        width: addFolderContent.width + Theme.spaceXs
                        height: Theme.rowHeight
                        hoverEnabled: true
                        onClicked: launcherController.addScanRoot()
                        Row {
                            id: addFolderContent
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spaceXs
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "\uE710" // MDL2 "Add" (a plus) — neon-orange identity
                                font.family: "Segoe MDL2 Assets"
                                font.pixelSize: Theme.fontSizeSubtitle
                                color: Theme.appOutline
                            }
                            Text {
                                id: addFolderLabel
                                text: "Add folder to scan…"
                                font.pixelSize: Theme.fontSizeTitle
                                font.weight: Theme.fontWeightRegular
                                color: addFolderArea.containsMouse ? Theme.textPrimary : Theme.textSecondary
                            }
                        }
                    }
                    // "Add executable…" (D-11) — native dialog opens in C++
                    // (fileSearch.addExecutable, 04-02).
                    MouseArea {
                        id: addExeArea
                        width: addExeContent.width + Theme.spaceXs
                        height: Theme.rowHeight
                        hoverEnabled: true
                        onClicked: fileSearch.addExecutable()
                        Row {
                            id: addExeContent
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spaceXs
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "\uE710" // MDL2 "Add" — orange, matches the Add-folder + 
                                font.family: "Segoe MDL2 Assets"
                                font.pixelSize: Theme.fontSizeSubtitle
                                color: Theme.appOutline
                            }
                            Text {
                                id: addExeLabel
                                text: "Add executable…"
                                font.pixelSize: Theme.fontSizeTitle
                                font.weight: Theme.fontWeightRegular
                                color: addExeArea.containsMouse ? Theme.textPrimary : Theme.textSecondary
                            }
                        }
                    }
                }

                // Right: "Show hidden (N)" (05.1) — visible whenever hidden
                // entries exist (rule- AND user-hidden — the discoverability
                // surface for the whole feature). Toggling reveals dimmed rows
                // for Unhide (CUR-03). Sits LEFT of the settings gear
                // (2026-08-15) so the two right-side actions never overlap.
                MouseArea {
                    id: showHiddenArea
                    visible: resultsModel.hiddenCount > 0
                    anchors.right: settingsGear.left
                    anchors.rightMargin: Theme.spaceLg
                    anchors.verticalCenter: parent.verticalCenter
                    width: showHiddenLabel.width
                    height: Theme.rowHeight
                    hoverEnabled: true
                    onClicked: resultsModel.setShowHidden(!resultsModel.showHidden)
                    Text {
                        id: showHiddenLabel
                        anchors.verticalCenter: parent.verticalCenter
                        text: resultsModel.showHidden
                              ? "Hide hidden (" + resultsModel.hiddenCount + ")"
                              : "Show hidden (" + resultsModel.hiddenCount + ")"
                        font.pixelSize: Theme.fontSizeTitle
                        font.weight: Theme.fontWeightRegular
                        color: showHiddenArea.containsMouse ? Theme.textPrimary : Theme.textSecondary
                    }
                }

                // Right: settings gear (2026-08-15) — the launcher-side
                // affordance for the settings surface (previously tray-only,
                // D-04). Small icon-only hit target in the footer row; opens
                // settings via the controller seam (main.cpp wiring hides the
                // launcher first, so the surface opens cleanly on top).
                MouseArea {
                    id: settingsGear
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spaceLg
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.rowHeight - Theme.spaceSm
                    height: Theme.rowHeight - Theme.spaceSm
                    hoverEnabled: true
                    onClicked: launcherController.openSettings()
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.chipRadius
                        color: settingsGear.containsMouse ? Theme.chipBg : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: "\uE713" // MDL2 "Settings" (gear)
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: Theme.fontSizeSubtitle
                            color: settingsGear.containsMouse ? Theme.textPrimary : Theme.textSecondary
                        }
                    }
                }
            }

            // ── In-window context menu (05.1) — right-click curation
            // (CUR-02/03). Overlay sibling of the list (never a popup window:
            // delegate-scoped Popup landed wrong on the scaled delegates —
            // observed 2026-08-11). Positioned in resultsView space by
            // ctxMenu.openMenu; closes on Escape, outside press, hide/launch.
            // The dismissal catcher is a SIBLING below the menu (same parent,
            // z: 15) — a child anchored to resultsView would land in the
            // menu's coordinate space and cover the wrong region (H-01).
            MouseArea {
                id: ctxDismiss
                anchors.fill: resultsView
                z: Theme.catcherZ
                visible: ctxMenu.visible
                onClicked: ctxMenu.closeMenu()
            }
            Item {
                id: ctxMenu
                property int targetIndex: -1
                property bool targetIsHidden: false
                property bool targetIsFavorite: false
                visible: false
                width: Theme.menuWidth
                height: Theme.menuItemHeight * 2 + Theme.spaceXs * 2
                z: Theme.menuZ
                function openMenu(index, isHidden, isFavorite, x, y) {
                    targetIndex = index
                    targetIsHidden = isHidden
                    targetIsFavorite = isFavorite
                    // x/y arrive in resultsView space (delegate mapToItem);
                    // translate into shell space (this overlay is a sibling
                    // of the list), then clamp so the menu never hangs off
                    // the shell edge (right-click near right/bottom edges).
                    ctxMenu.x = Math.max(0, Math.min(resultsView.x + x, resultsView.x + resultsView.width - width - Theme.spaceXs))
                    ctxMenu.y = Math.max(0, Math.min(resultsView.y + y, resultsView.y + resultsView.height - height - Theme.spaceXs))
                    visible = true
                }
                function closeMenu() { visible = false }

                // Background WITH its own full-size MouseArea — without it,
                // clicks on the border strip would fall through to the row
                // underneath and LAUNCH the app (H-02). A press here is a
                // miss: dismiss.
                Rectangle {
                    anchors.fill: parent
                    color: Theme.surfaceSecondary
                    radius: Theme.menuRadius
                    border.width: 1
                    border.color: Theme.border
                    MouseArea {
                        anchors.fill: parent
                        onClicked: ctxMenu.closeMenu()
                    }
                }
                Column {
                    anchors.fill: parent
                    anchors.margins: Theme.spaceXs
                    spacing: 0

                    // Item 1: favorite toggle (2026-08-15) — same machinery as
                    // the hover star, discoverable here without hover.
                    Rectangle {
                        width: parent.width
                        height: Theme.menuItemHeight
                        radius: Theme.menuRadius - Theme.spaceXs
                        color: ctxFavItem.containsMouse ? Theme.hoverBg : "transparent"
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spaceMd
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spaceSm
                            anchors.verticalCenter: parent.verticalCenter
                            text: ctxMenu.targetIsFavorite ? "Remove from favorites" : "Add to favorites"
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeMenu
                            font.weight: Theme.fontWeightRegular
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            id: ctxFavItem
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                ctxMenu.closeMenu()
                                resultsModel.selectIndex(ctxMenu.targetIndex)
                                if (ctxMenu.targetIsFavorite)
                                    resultsModel.unfavoriteSelected()
                                else
                                    resultsModel.favoriteSelected()
                            }
                        }
                    }

                    // Item 2: hide/unhide.
                    Rectangle {
                        width: parent.width
                        height: Theme.menuItemHeight
                        radius: Theme.menuRadius - Theme.spaceXs
                        color: ctxItem.containsMouse ? Theme.hoverBg : "transparent"
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spaceMd
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spaceSm
                            anchors.verticalCenter: parent.verticalCenter
                            text: ctxMenu.targetIsHidden ? "Unhide" : "Hide"
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSizeMenu
                            font.weight: Theme.fontWeightRegular
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            id: ctxItem
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                ctxMenu.closeMenu()
                                if (ctxMenu.targetIsHidden)
                                    resultsModel.unhideSelected()
                                else
                                    resultsModel.hideSelected()
                            }
                        }
                    }
                }
            }

            // ── Empty / no-match states (RESEARCH §7 verbatim copy) ──
            // D-18 gate: when the indexer is troubled AND the list is empty,
            // the status row below takes the space (same area as "No
            // results…"); when results exist, both stay truthful.
Item {
                    id: emptyState
                    anchors.fill: resultsView
                    visible: resultsView.count === 0 && fileSearch.indexerOk
                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.spaceSm
                        // 2026-08-15 (UI pass): the composed well — U+E721
                        // "Search" (declared; present on Win10/11) centered in
                        // a 48px surfaceSecondary circle. Muted textSecondary
                        // glyph, never accent (accent reserved-list) — the
                        // well gives the state presence without shouting.
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: Theme.emptyStateWellSize
                            height: Theme.emptyStateWellSize
                            radius: Theme.emptyStateWellRadius
                            color: Theme.surfaceSecondary
                            Text {
                                anchors.centerIn: parent
                                text: "\uE721"
                                font.family: "Segoe MDL2 Assets"
                                font.pixelSize: Theme.emptyStateWellGlyphSize
                                color: Theme.emptyStateGlyphColor
                            }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            // 07-06: a completed scan shows its summary ("Last
                            // scan HH:mm — N entries") so picking a folder has
                            // visible confirmation; never scanned → the hint.
                            text: resultsModel.query === ""
                                  ? (fileSearch.lastScanSummary !== ""
                                     ? fileSearch.lastScanSummary
                                     : "No files here yet — select a folder to scan")
                                  : "No results for \"" + resultsModel.query + "\""
                            font.pixelSize: Theme.fontSizeTitle
                            font.weight: Theme.fontWeightRegular
                            color: Theme.textSecondary
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Press Esc to close"
                            font.pixelSize: Theme.fontSizeSubtitle
                            font.weight: Theme.fontWeightRegular
                            color: Theme.textSecondary
                        }
                        // 07-06: "Select a folder to scan" — the empty-QUERY
                        // branch only (a live query has its own flow); whole
                        // action wired in main.cpp (picker → store → scan).
                        Item {
                            width: folderRow.implicitWidth
                            height: folderRow.implicitHeight
                            visible: resultsModel.query === ""
                            Row {
                                id: folderRow
                                spacing: Theme.spaceXs
Text {
                                    text: "\uE8B7" // MDL2 "Folder"
                                    font.family: "Segoe MDL2 Assets"
                                    font.pixelSize: Theme.fontSizeSubtitle
                                    color: folderRowHover.containsMouse ? Theme.textPrimary : Theme.textSecondary
                                }
                                Text {
                                    text: "Select a folder to scan"
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeSubtitle
                                    font.weight: Theme.fontWeightRegular
                                    color: folderRowHover.containsMouse ? Theme.textPrimary : Theme.textSecondary
                                }
                            }
                            MouseArea {
                                id: folderRowHover
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: launcherController.addScanRoot()
                            }
                        }
                    }
                }

            // ── D-04 no-roots prompt: the empty-QUERY branch of the status
            // row. Gates: no query typed AND file backend troubled (no roots
            // / error / scanning) AND no rows. Mutually exclusive by
            // construction: the empty-state (line 708) is gated on
            // fileSearch.indexerOk, the status row (line 752) on
            // query !== "" — this branch owns query === "".
            // Copy is single-homed in FileSearch::statusText (verbatim).
            Item {
                id: noRootsPrompt
                anchors.fill: resultsView
                visible: resultsModel.query === "" && !fileSearch.indexerOk
                         && resultsView.count === 0
                Column {
                    anchors.centerIn: parent
                    spacing: Theme.spaceXs
                    // D-11: 16px Segoe MDL2 Assets glyph above the message —
                    // U+E721 "Search" (declared; present on Win10/11). Muted
                    // textSecondary, never accent (accent reserved-list).
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "\uE721"
                        font.family: "Segoe MDL2 Assets"
                        font.pixelSize: Theme.emptyStateGlyphSize
                        color: Theme.emptyStateGlyphColor
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: fileSearch.statusText
                        font.pixelSize: Theme.fontSizeTitle
                        font.weight: Theme.fontWeightRegular
                        color: Theme.textSecondary
                    }
                    // 2026-08-15: indeterminate scan-progress bar while a scan
                    // is in flight (no-roots / first-scan empty state).
                    Item {
                        id: noRootsBar
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 200
                        height: Theme.scanBarHeight
                        visible: fileSearch.scanning
                        Rectangle {
                            anchors.fill: parent
                            color: Theme.surfaceSecondary
                            radius: Theme.scanBarRadius
                        }
                        Rectangle {
                            id: noRootsChunk
                            width: noRootsBar.width / 3
                            height: noRootsBar.height
                            color: Theme.appOutline
                            radius: Theme.scanBarRadius
                            x: noRootsChunkAnim.value
                            SequentialAnimation on x {
                                id: noRootsChunkAnim
                                running: noRootsChunk.visible
                                loops: Animation.Infinite
                                PropertyAnimation {
                                    from: -noRootsBar.width
                                    to: noRootsBar.width
                                    duration: 900
                                    easing.type: Easing.InOutCubic
                                }
                            }
                        }
                    }
                    // 07-06: direct "start" action — pick a folder without
                    // going through Settings first (same main.cpp wiring).
                    Item {
                        width: folderRow2.implicitWidth
                        height: folderRow2.implicitHeight
                        Row {
                            id: folderRow2
                            spacing: Theme.spaceXs
                            Text {
                                text: "\uE8B7" // MDL2 "Folder"
                                font.family: "Segoe MDL2 Assets"
                                font.pixelSize: Theme.fontSizeSubtitle
                                color: folderRowHover2.containsMouse ? Theme.textPrimary : Theme.textSecondary
                            }
                            Text {
                                text: "Select a folder to scan"
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSubtitle
                                font.weight: Theme.fontWeightRegular
                                color: folderRowHover2.containsMouse ? Theme.textPrimary : Theme.textSecondary
                            }
                        }
                        MouseArea {
                            id: folderRowHover2
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: launcherController.addScanRoot()
                        }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Press Esc to close"
                        font.pixelSize: Theme.fontSizeSubtitle
                        font.weight: Theme.fontWeightRegular
                        color: Theme.textSecondary
                    }
                }
            }

            // ── Indexer status row (D-17/D-18, RESEARCH §9) — non-selectable
            // overlay (never a model row, never focusable); renders the
            // verbatim locked copy owned by FileSearch.cpp. Visible only while
            // a query is active AND troubled AND the list is EMPTY (WR-04) —
            // with app matches on screen the row must never cover them; it
            // owns the space only when the list would otherwise be blank
            // (the same space as the "No results for…" empty state, which is
            // itself gated on indexerOk — the two are mutually exclusive).
            Item {
                id: statusRow
                anchors.fill: resultsView
                visible: resultsModel.query !== "" && !fileSearch.indexerOk
                         && resultsView.count === 0
                Column {
                    anchors.centerIn: parent
                    spacing: Theme.spaceSm
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: fileSearch.statusText
                        font.pixelSize: Theme.fontSizeTitle
                        font.weight: Theme.fontWeightRegular
                        color: Theme.textSecondary
                    }
                    // 2026-08-15: indeterminate scan-progress bar while a scan
                    // is in flight (empty live-query state, same region).
                    Item {
                        id: statusBar
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 200
                        height: Theme.scanBarHeight
                        visible: fileSearch.scanning
                        Rectangle {
                            anchors.fill: parent
                            color: Theme.surfaceSecondary
                            radius: Theme.scanBarRadius
                        }
                        Rectangle {
                            id: statusChunk
                            width: statusBar.width / 3
                            height: statusBar.height
                            color: Theme.appOutline
                            radius: Theme.scanBarRadius
                            x: statusChunkAnim.value
                            SequentialAnimation on x {
                                id: statusChunkAnim
                                running: statusChunk.visible
                                loops: Animation.Infinite
                                PropertyAnimation {
                                    from: -statusBar.width
                                    to: statusBar.width
                                    duration: 900
                                    easing.type: Easing.InOutCubic
                                }
                            }
                        }
                    }
                }
            }

            // ── Transient admin-refusal hint (D-11, non-modal) ──
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.spaceMd
                visible: root.hintText !== ""
                text: root.hintText
                font.pixelSize: Theme.fontSizeSubtitle
                font.weight: Theme.fontWeightRegular
                color: Theme.textSecondary
            }
            Timer {
                id: hintTimer
                interval: 2500
                onTriggered: root.hintText = ""
            }
            Connections {
                target: launchController
                function onAdminRequestRefused() {
                    root.hintText = "Only desktop apps can run as administrator"
                    hintTimer.restart()
                }
            }
            // D-13..D-15: accent flows SettingsStore → Theme. Context property
            // + Connections (research Pattern 4) — never a singleton↔context
            // read, never qmlRegisterSingletonInstance (invisible to
            // qmlcachegen). Derived shades update by binding (A7 belt: the
            // bindings are primary; assignments would only be added if the
            // visual pass ever showed stale shades). Phase-6 picker path.
            Connections {
                target: settingsStore
                function onAccentChanged(c) {
                    Theme.accent = c
                }
            }

            // Neon app outline (2026-08-15): the LAST child draws the bright
            // orange ring ON TOP of the surface's content, so the frame is
            // never broken by opaque rows reaching an edge (footer hover
            // fills, etc.). Transparent fill — it only contributes the 2px
            // ring. z 1 keeps it below the dismissal catcher (15) and the
            // context menu (20).
            Rectangle {
                id: appRing
                anchors.fill: parent
                color: "transparent"
                border.color: Theme.appOutline
                border.width: Theme.appOutlineWidth
                z: 1
            }
        }
    }

    // ── Open (VISU-01 contract) — starts from onVisibleChanged (D-10) ──
    function playOpen() {
        shell.opacity = 0
        shellScale.xScale = Theme.animScaleFrom
        shellScale.yScale = Theme.animScaleFrom
        openAnim.start()
    }
    onVisibleChanged: {
        if (visible) {
            ctxMenu.closeMenu()    // 05.1: never resurrect the menu on reopen (M-03)
            centerOnScreen()      // see centerOnScreen() — re-apply every show
            resultsView.keyboardActive = false   // fresh input mode each open
            // 2026-08-11: fresh-slate selection every open — the selection
            // and viewport persisted across hides (04:43 trail opened at
            // idx=6, mid-list). Open with row 0 selected, viewport at top.
            resultsModel.selectIndex(0)
            resultsView.contentY = 0
            playOpen()
            searchField.forceActiveFocus()   // typing lands in the field (LAUN-05)
        } else {
            closing = false    // D-02.1: resident — every hide re-arms Escape
            searchField.text = ""   // new-open slate: never persist the last query
            repeatTimer.stop()      // a release can never arrive while hidden
        }
    }

    // Focus hand-off (QML half of the focus sequence, CONTEXT.md): when the
    // window becomes active again after the C++ show→raise→deferred
    // requestActivate, push focus onto the search field so typing lands
    // immediately. Keys.forwardTo still routes nav/Enter/Escape through the
    // shell block while the field owns focus.
    onActiveChanged: if (window.active) searchField.forceActiveFocus()

    ParallelAnimation {
        id: openAnim
        NumberAnimation { target: shell;    property: "opacity"; from: 0;  to: 1; duration: Theme.animOpenDuration; easing.type: Theme.easingOpen }
        NumberAnimation { target: shellScale; property: "xScale"; from: Theme.animScaleFrom; to: Theme.animScaleTo; duration: Theme.animOpenDuration; easing.type: Theme.easingOpen }
        NumberAnimation { target: shellScale; property: "yScale"; from: Theme.animScaleFrom; to: Theme.animScaleTo; duration: Theme.animOpenDuration; easing.type: Theme.easingOpen }
    }

    // ── Close (VISU-01 + D-09): animation completes → hide; app stays resident ──
    property bool closing: false
    function dismiss() {
        if (closing) return
        closing = true
        closeAnim.start()
    }
    // Instant dismiss API (HOTK-03 launch dismissal, controller.hideNow()
    // consumes it): reset the close lock, stop any mid-flight close
    // animation, hide without animation.
    function hideNow() {
        closing = false
        closeAnim.stop()
        root.hide()
    }
    // Alt+F4 / WM_CLOSE → same path (D-09): reject the FIRST close so the
    // animation plays; the re-close from closeAnim.onFinished is accepted.
    onClosing: (close) => {
        if (!closing) {
            close.accepted = false
            dismiss()
        }
    }
    ParallelAnimation {
        id: closeAnim
        NumberAnimation { target: shell;    property: "opacity"; to: 0; duration: Theme.animCloseDuration; easing.type: Theme.easingClose }
        NumberAnimation { target: shellScale; property: "xScale"; to: Theme.animScaleFrom; duration: Theme.animCloseDuration; easing.type: Theme.easingClose }
        NumberAnimation { target: shellScale; property: "yScale"; to: Theme.animScaleFrom; duration: Theme.animCloseDuration; easing.type: Theme.easingClose }
        onFinished: {
            root.hide()      // hide ONLY here — never mid-animation (rule 2)
            // D-02.1: NO root.close()/Qt.quit() — the app stays resident; the
            // only exit is tray → Quit (02-03, QCoreApplication::quit()).
        }
    }
}
