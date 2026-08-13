#pragma once

#include <QColor>
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QSystemTrayIcon>

// Resident tray presence (D-02.2): Open wisp / Settings / Change hotkey… /
// Quit menu (order locked, D-03) and the HOTK-02 conflict notification. Thin
// Qt Widgets wrapper — the app's only Widgets surface; everything else stays
// in the QML/Quick world.
class TrayIcon : public QObject
{
    Q_OBJECT

public:
    explicit TrayIcon(QObject *parent = nullptr);
    ~TrayIcon() override;

    // Shows the icon + installs the menu. Must be called before any
    // showMessage() — balloons on a hidden tray are silently dropped.
    void show();

    // The tray context menu (parent-free by design — widgets cannot be
    // object-parented to non-widgets). Test accessor.
    QMenu *menu() const { return m_menu; }

    // HOTK-02: never-silent conflict surfacing. The message points at the
    // tray menu's Change hotkey… path (D-02.6).
    void notifyHotkeyConflict(const QString &combo);

    // UI-SPEC tray icon contract: the disc fill is live-bound to the accent.
    // main.cpp calls this at startup with SettingsStore::accent() and on every
    // accentChanged (06-04 wiring) — TrayIcon NEVER reaches into the store
    // itself (PATTERNS anti-pattern 1). The white "w" stays white at all
    // accents. Invalid colors are silently ignored (D-16).
    void setAccent(const QColor &accent);

signals:
    void openWisp();
    void settingsRequested();
    void changeHotkeyRequested();
    void quitRequested();

private:
    QColor m_accent;   // default #0078D4 — disc fill (repainted on setAccent)
    QPointer<QSystemTrayIcon> m_tray;
    QPointer<QMenu> m_menu;
};
