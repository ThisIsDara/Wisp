<p align="center">
  <img src="assets/icons/wisp-01.png" width="96" alt="wisp icon">
</p>

<p align="center">
  <a href="https://github.com/ThisIsDara/Wisp/releases/latest"><img src="https://img.shields.io/github/v/release/ThisIsDara/Wisp" alt="Latest release"></a>
  <a href="https://github.com/ThisIsDara/Wisp/releases"><img src="https://img.shields.io/github/downloads/ThisIsDara/Wisp/total" alt="Total downloads"></a>
  <a href="https://github.com/ThisIsDara/Wisp/releases/latest"><img src="https://img.shields.io/github/downloads/ThisIsDara/Wisp/latest/total" alt="Latest release downloads"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0-blue" alt="License: GPL-3.0"></a>
  <a href="https://github.com/ThisIsDara"><img src="https://img.shields.io/badge/profile-ThisIsDara-blue?style=flat" alt="Profile: ThisIsDara"></a>
</p>

# wisp

An instant app launcher for Windows 10/11. Press `Alt+Space`, type a few
letters, pick a result, press Enter. Built with Qt 6 + QML + C++.

wisp lives in the tray, opens instantly with a global hotkey, and launches
apps, Store/UWP apps, and indexed files from one merged, ranked list.

## Features

### Instant launch

Press `Alt+Space` anywhere and wisp is there. Type, press Enter, done.
Opens and launches in under a second.

<img src="assets/gifs/instant-launch.gif" width="640" alt="Instant launch demo">

### Favorites

Star any result to pin it. The Favorites tab shows just the ones you picked,
and the launcher starts there when you have favorites.

<img src="assets/gifs/favorites.gif" width="640" alt="Favorites demo">


### Settings

Tray icon opens Settings: change the hotkey, pick an accent color, choose
scan folders, and toggle per-user autostart.

<img src="assets/gifs/settings.gif" width="640" alt="Settings demo">

### Look and feel

Acrylic backdrop and 60fps animations. A small, centered launcher that
dismisses as smoothly as it opens.

<img src="assets/gifs/feel.gif" width="640" alt="Look and feel demo">


## Shortcuts

| Key | Action |
| --- | --- |
| `Alt+Space` | Open / close the launcher (configurable in Settings) |
| `Enter` | Launch the selected result |
| `Ctrl+Enter` | Reveal the selected file in Explorer |
| `Ctrl+Shift+Enter` | Launch the selected app elevated |
| `Esc` | Close the context menu, else dismiss |
| `Up` / `Down` | Navigate results |
| `PageUp` / `PageDown` | Jump 7 results |
| `Home` / `End` | Jump to first / last result |
| `Ctrl+H` | Hide the selected entry (or unhide it in show-hidden mode) |
| Right-click a result | Context menu: favorite, hide, launch, reveal |

## Build from source

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 with the Desktop development with C++ workload
- Qt 6.11.1 MSVC 2022 x64 (Qt Quick, Qt Core, Qt Gui, Qt Widgets, Qt Shader Tools)
- CMake 3.25+ and Ninja (bundled with the Qt installer)
- NSIS 3.x (only needed to build the installer)

```powershell
.\build.ps1 -Config dev                          # configure and build
.\run.ps1                                        # run the launcher
.\deploy.ps1 -Config release                     # produce a standalone folder
.\packaging\build-installer.ps1 -Config release  # build wisp-setup.exe
ctest --test-dir build\dev                       # run the test suite
```

`build.ps1` locates Qt at `C:\Qt\6.11.1\msvc2022_64\bin` by default and finds
vcvars64 automatically, so a plain `.\build.ps1` usually just works.

## Install

Download the latest `wisp-setup.exe` from
[Releases](https://github.com/ThisIsDara/Wisp/releases). Per-user install,
no admin rights required.

## License

GPLv3. Qt is dynamically linked under LGPLv3; see
`packaging/THIRD-PARTY-NOTICES.txt`.
