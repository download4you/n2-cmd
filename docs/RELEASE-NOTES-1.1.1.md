# n2-cmd 1.1.0

n2-cmd is a native Windows terminal for Persian and mixed-direction text, built with C, C++ and Lua. Qt Widgets provides the interface, Win32 ConPTY runs shell sessions, libvterm parses terminal output, and Lua supplies configuration.

## Screenshots

![n2-cmd main window](https://raw.githubusercontent.com/download4you/n2-cmd/main/docs/screenshots/main.png)

![n2-cmd settings](https://raw.githubusercontent.com/download4you/n2-cmd/main/docs/screenshots/settings.png)

![n2-cmd quick commands](https://raw.githubusercontent.com/download4you/n2-cmd/main/docs/screenshots/quick-menu.png)

## Highlights

- **Real live background blur:** continuously refreshed desktop content with self-capture prevention and safe capture-affinity restoration.
- **Lower backdrop latency:** captures only the console's client region rather than the full virtual desktop. The default refresh interval is 200 ms and the cross-fade is 75 ms.
- **Measured improvement:** five full-window color-change tests had a 405 ms median and 414 ms worst result, down from an 1117 ms baseline.
- **Configurable live background:** 0–40 px live blur strength and a 100–1000 ms refresh interval. Lower refresh values are more responsive but may use more CPU.
- **Clearer Settings:** dedicated Live background, Wallpaper, Terminal, Behavior, Accent color, and Shortcuts groups. Live blur and wallpaper blur are separate controls.
- **Persistent preferences:** live blur strength and refresh interval are saved in JSON and supported by Lua preferences, sanitization, and appearance reset.
- **Standalone build output:** the standard build deploys the required Qt libraries and plugins and performs a clean runtime launch check.
- **Regression coverage:** real pixel-level live-blur checks, normal opacity/dim coverage, settings persistence, keyboard navigation, and sustained TUI redraw/resize tests.

## Install

Run `n2-cmd-1.1.0-windows-x64-setup.exe`. The installer uses a per-user installation under `%LOCALAPPDATA%\Programs\n2-cmd`; administrator rights are not required.

A portable ZIP is also attached. Extract it to a writable folder and run `n2-cmd.exe`. Keep the DLLs and plugin folders beside the executable.

## Requirements

- Windows 10 version 1809 or later, or Windows 11
- x64 processor

## Verification

- Release build with MSVC 2022 and Qt 6.8.3
- All five native test suites passed: settings, terminal, UI, ConPTY, and live integration
- Real red-to-green live-background checks and checkerboard blur-energy checks
- Settings persistence, reset, keyboard navigation, and live-on/live-off visual inspection
- Five measured full-window latency runs: 351, 401, 405, 408, and 414 ms
- Package manifest, file-integrity checks, and SHA-256 checksums
- Isolated runtime check for PNG, JPEG, WebP, BMP, and GIF
- Confirmed Kimi Code startup in the standalone executable

## License

The n2-cmd source is MIT licensed. Dependency licenses and upstream sources are recorded in `THIRD_PARTY.md`. Qt remains dynamically linked and replaceable; its LGPL/GPL license texts and third-party notices are included in release archives.
