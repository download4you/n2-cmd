# n2-cmd 1.0.0

n2-cmd is a Windows terminal for Persian and mixed-direction text. It is built
with C, C++ and Lua: Qt 6 Widgets for the interface, Win32 ConPTY for shells,
libvterm for terminal parsing, and Lua for defaults and quick commands. Python
is not required at runtime.

## Screenshots

![n2-cmd main window](https://raw.githubusercontent.com/download4you/n2-cmd/main/docs/screenshots/main.png)

![n2-cmd settings](https://raw.githubusercontent.com/download4you/n2-cmd/main/docs/screenshots/settings.png)

![n2-cmd quick commands](https://raw.githubusercontent.com/download4you/n2-cmd/main/docs/screenshots/quick-menu.png)

## Highlights

- Persian shaping, mixed RTL/LTR text, selection, clipboard, IME and bidi-aware cursor hit-testing.
- Shift+Enter inserts a new line in Codex and other TUI editors.
- Scrollback can be navigated by clicking or dragging the right-hand scrollbar.
- Dragging the window to a screen edge or corner snaps it like Windows 10/11; this can be disabled in Settings.
- A header pin button keeps the window above other windows.
- Tabs with shell selection, duplicate, rename/reset, restart, close-other-tabs and output export.
- 28 built-in quick commands with shell-specific mappings, pins and custom favorites.
- Wallpaper, opacity, dim, blur, terminal tint, font, size, accent, scrollback and copy-on-select settings.
- Native CMD, optional Clink-enhanced CMD, Windows PowerShell, PowerShell 7, Git Bash and installed WSL distributions.
- ANSI color palette, default-title filtering and 72-character title bounds.
- Rounded window mask, settings shadow, terminal accent lines, styled menus, toasts and an ended-session banner.
- Per-user installer with Start Menu entry, optional desktop shortcut and uninstaller.

## Install

Run `n2-cmd-1.0.0-windows-x64-setup.exe`. Administrator rights are not required.
The default install directory is `%LOCALAPPDATA%\Programs\n2-cmd`.

A portable ZIP is also attached. Extract it to a writable folder and run
`n2-cmd.exe`.

## Requirements

- Windows 10 version 1809 or later, or Windows 11
- x64 processor

## Verification

- Release build with MSVC 2022 and Qt 6.8.3
- Settings, terminal, UI, ConPTY and live integration tests
- Package manifest and SHA-256 checksums
- Isolated runtime check for PNG, JPEG, WebP, BMP and GIF

## License

The n2-cmd source is MIT licensed. Dependency licenses and upstream sources are
recorded in `THIRD_PARTY.md`. Qt remains dynamically linked and replaceable;
its LGPL/GPL license texts and third-party notices are included in release
archives.
