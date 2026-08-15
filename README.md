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


## Install

Download the latest `wisp-setup.exe` from
[Releases](https://github.com/ThisIsDara/Wisp/releases). Per-user install,
no admin rights required.

## Build from source

See the scripts in the repo root (`build.ps1`, `deploy.ps1`,
`packaging/build-installer.ps1`) and the project docs.

## License

GPLv3. Qt is dynamically linked under LGPLv3; see
`packaging/THIRD-PARTY-NOTICES.txt`.
