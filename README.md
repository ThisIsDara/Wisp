<p align="center">
  <img src="assets/icons/wisp-01.png" width="96" alt="wisp — an instant app launcher for Windows">
</p>

# wisp

*The instant app launcher for Windows 10/11. Press `Alt+Space` anywhere, type a few letters, and the app you want is already open.*

[![Downloads](https://img.shields.io/github/downloads/ThisIsDara/wisp/total)](https://github.com/ThisIsDara/wisp/releases)
[![Release](https://img.shields.io/github/v/release/ThisIsDara/wisp)](https://github.com/ThisIsDara/wisp/releases/latest)
[![CI](https://img.shields.io/github/actions/workflow/status/ThisIsDara/wisp/release.yml?label=release%20pipeline)](.github/workflows/release.yml)
[![License: GPLv3](https://img.shields.io/github/license/ThisIsDara/wisp)](LICENSE)

**wisp is a small, centered, acrylic-blurred search box that appears wherever
you are with a global hotkey.** Type and fuzzy-ranked results — installed
apps, Store/UWP apps, indexed files, and executables you added yourself —
appear instantly; Enter launches; it dismisses as smoothly as it opened.
No terminal, no config files, no learning curve.

## Screenshots

*Screenshots and a demo GIF will be added here.*

<!-- Drop media here, e.g.:
<p align="center"><img src="assets/screenshot-default.png" width="640" alt="Launcher open with the default app list"></p>
<p align="center"><img src="assets/demo.gif" width="640" alt="Demo: hotkey, type, launch"></p>
-->

## Features

- **Global hotkey** — `Alt+Space` anywhere on Windows; change it in Settings
- **Fuzzy search everything** — Start Menu apps, UWP/Store apps, and Windows
  Search–indexed files in one merged, ranked list with match highlighting
- **Zero-config default list** — installed apps on the empty query; no setup
- **Bring your own executables** — tray menu → *Add executable…*; then hide
  or unhide any entry (`Ctrl+H` / `Ctrl+Shift+H`) to curate your list
- **Acrylic blur + 60fps animation** — built for feel, opens and launches in
  under a second
- **Tray-resident** — single instance, per-user autostart option, no admin
  rights anywhere

## Installation

[**Download wisp-setup.exe**](https://github.com/ThisIsDara/wisp/releases/latest/download/wisp-setup.exe) from the latest release.

- Per-user install — **no admin rights required**; the Visual C++ runtime is
  installed automatically if your system needs it
- Verify your download against the `SHA256SUMS` file attached to each release
- **Only trust downloads from this repository's Releases page**

> **SmartScreen:** if Windows shows *"Windows protected your PC"*, click
> *More info → Run anyway*. Code signing is in progress via SignPath
> Foundation (see the [code signing policy](CODE_SIGNING_POLICY.md)); until
> signed, this is normal for a new open-source project — the warning clears
> as more users install the app.

## Usage

Default hotkey | Action
--- | ---
`Alt+Space` | Open / close the launcher (configurable in **Settings** — tray icon → Settings)
`Type` | Fuzzy filter apps, files, and your added executables
`Enter` | Launch the selected result
`Esc` | Dismiss
`↑` `↓` / `Ctrl+N` `Ctrl+P` | Navigate results
`Ctrl+H` | Hide the selected entry (curation)
`Ctrl+Shift+H` | Toggle showing hidden entries (to unhide)

## Design scope

wisp is a launcher, **focused by design**: no plugin system, no calculator or
command modes, no scripting. It does one job — an instant, beautiful,
hotkey-driven way to open anything — and does it smoothly. Feature requests
live in [Issues](https://github.com/ThisIsDara/wisp/issues).

## Changelog

See [Releases](https://github.com/ThisIsDara/wisp/releases) — every release
links its changes.

## Building from source

Requirements: Windows 10/11 x64 · Visual Studio 2022 with the *Desktop
development with C++* workload · Qt 6.11.1 MSVC 2022 x64 (Qt Quick, Qt Core,
Qt Gui, Qt Widgets, Qt Shader Tools) · CMake ≥ 3.25 + Ninja.

```powershell
.\build.ps1 -Config dev                          # configure + build (dev)
.\run.ps1                                        # run the launcher
.\deploy.ps1 -Config release                     # standalone deploy folder
.\packaging\build-installer.ps1 -Config release  # per-user NSIS installer
ctest --test-dir build\dev -C RelWithDebInfo     # run the test suite
```

Release builds are produced by the CI pipeline
(`.github/workflows/release.yml`): push a `v*` tag, and GitHub Actions
builds, deploys, assembles the installer, submits it for code signing via
SignPath Foundation, and publishes a GitHub Release with `SHA256SUMS`.

## Contributing

Issues and pull requests are welcome — please read the
[Code of Conduct](CODE_OF_CONDUCT.md) first. This is a small project: a
clear issue description or a focused PR beats a long one. By contributing,
you agree your contributions are licensed under the same GPLv3 terms as the
project.

## License

- **wisp source code:** [GPLv3](LICENSE) — free to use, modify, and
  distribute; derivatives must stay GPL (no closed-source forks)
- **Qt (6.x):** LGPLv3, dynamically linked — the combination is permitted
  (LGPLv3 §4); Qt obligations (source offer, notices) are covered by the
  [THIRD-PARTY-NOTICES.txt](packaging/THIRD-PARTY-NOTICES.txt) shipped with
  every release
- **Visual C++ runtime:** distributed under the VC_redist license via the
  official Microsoft installer