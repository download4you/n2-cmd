# Feature coverage

n2-cmd is a C, C++ and Lua terminal for Windows. Qt Widgets provides the
interface, Win32 ConPTY runs shell sessions, libvterm parses terminal output,
and Lua supplies configuration. Python is not an application dependency.

## Appearance and behavior

| Option | Supported values |
| --- | --- |
| Window opacity | 30-100% |
| Background image | PNG, JPEG, WebP and BMP; copied into the user profile |
| Background dim | 0-92% |
| Background blur | 0-40 px |
| Terminal tint | 0-88% |
| Terminal font | Vazir Code Hack default; Cascadia Mono, Cascadia Code, Consolas, JetBrains Mono, Fira Code, Courier New and installed fonts |
| Font size | 7.5-26 pt; 10 pt default |
| Copy on select | Enabled or disabled |
| Accent | Cyan, Violet, Mint, Amber and Rose; Mint default |
| Scrollback | 500-50,000 retained lines; 6,000 default |
| Scrollback navigation | Mouse wheel; click or drag the right-hand scrollbar |
| Window snapping | Drag to a screen edge or corner; on by default, toggle in Settings |
| TUI newline | Shift+Enter sends a native Shift+Enter key record |
| Always on top | Pin button in the header |
| Default shell | Any detected profile |
| Window state | Saved normal dimensions, position and maximized state; negative monitor coordinates retained |
| Reset appearance | Restores visual defaults while preserving shell, selection behavior, favorites, pins and geometry |

The minimum window size is 640 x 400. The default size is 1240 x 780.

## Shell profiles

The default profile is native Command Prompt. Enhanced CMD appears when a
compatible Clink executable is available in `runtime/clink`, `clink`, or `PATH`.
Windows PowerShell, PowerShell 7 and Git Bash appear when their executables are
found. WSL appears only after `wsl --list --quiet` reports installed distributions;
each installed distribution also has its own selectable profile.

Native CMD, Enhanced CMD and the `cmd` profile key all use CMD command
syntax. PowerShell profiles use PowerShell syntax. Git Bash and WSL profiles use
Unix syntax. A command with no Unix variant is unavailable in those profiles.
Selecting a missing saved profile falls back to native CMD, then to the first
available profile. Shell detection does not install shells or distributions.

## Quick commands

The Lua catalog contains 28 entries with descriptions, shell-specific text and
the administrator flag. `settings_tests` checks each entry against a digest
recorded for this release.

| Category | Command | Unix variant |
| --- | --- | --- |
| Network | Flush DNS cache | Yes; requires administrator/root privileges |
| Network | Show local IP | Yes |
| Network | Show public IP | Yes |
| Network | Show DNS servers | Yes |
| Network | Ping Google DNS | Yes |
| Network | Trace route to Google | Yes |
| Network | DNS lookup google.com | Yes |
| Network | Established connections | Yes |
| Network | Listening ports | Yes |
| Network | Full network config | Yes |
| Network | Saved Wi-Fi networks | No |
| Network | Current Wi-Fi | No |
| System | System information | Yes |
| System | System uptime | Yes |
| System | Disk free space | Yes |
| System | Top processes by CPU | Yes |
| System | Startup programs | No |
| System | Who am I | Yes |
| Files | Show current folder | Yes |
| Files | Open current folder | No |
| Files | List files | Yes |
| Files | Largest items here | Yes |
| Files | Open Downloads | No |
| Developer | Git status | Yes |
| Developer | Recent Git commits | Yes |
| Developer | Docker containers | Yes |
| Developer | Python version | Yes |
| Developer | Node version | Yes |

Command availability still depends on the selected shell's installed tools.
For example, the preserved CMD disk/startup commands use `wmic`, which some
Windows installations do not include. The Python and Node version entries are
optional commands for checking those tools; n2-cmd itself requires neither.

## Favorites and persistence

Built-in commands can be pinned and unpinned. Custom favorites retain a name
and a command, can be run in the active tab, and can be removed. The limits are
60 unique pins, 60 favorites, 64 Unicode characters per favorite name and 2,000
Unicode characters per favorite command. Additions over a limit report an error
without silently shortening the new entry. Failed favorite or pin writes restore
the previous in-memory collection.

Settings are stored as UTF-8 `settings.json`, atomically replaced using Qt's
`QSaveFile`. The directory is `N2_CMD_DATA_DIR` when set, otherwise Qt's local
application-data directory. This override also allows isolated portable profiles
and test runs. Unknown JSON keys are ignored. Invalid types retain defaults;
out-of-range numeric settings are clamped. Corrupt input reports an error and is
left untouched during loading. Oversized saved collections are bounded in memory
with an explicit load error.

Embedded `config/defaults.lua` supplies the initial settings. An optional UTF-8
`preferences.lua` in the same data directory can supply initial preferences:

```lua
return {
    font_family = "Vazir Code Hack",
    font_size = 10,
    accent = "#3ddc97",
    default_shell = "native",
    pinned_commands = { "cwd", "git-status" },
    favorite_commands = {
        { name = "Project status", command = "git status --short" },
    },
}
```

Load precedence is embedded defaults, optional Lua preferences, then saved JSON.
Existing saved settings therefore override the initial Lua preferences. Lua
configuration must return a table. It has a 100,000-instruction execution budget,
an 8 MiB allocation budget and no filesystem, process, package or debug library.
Configuration files are limited to 2 MiB and must contain valid UTF-8; an optional
UTF-8 BOM is accepted. Built-in commands are data and are never executed while
loading the catalog or running its tests.

## Tab interactions

The interface provides the quick-command menu (`Ctrl+Shift+P`), renaming with F2
or a tab double-click, resetting a custom tab name, and UTF-8 output export
(`Ctrl+Shift+S`). A custom name takes precedence over shell title updates.
Export contains retained terminal history rather than already-evicted lines.

See the main README for keyboard shortcuts and build instructions. Release
verification is summarized in `RELEASE-NOTES-1.0.0.md` and the workflow logs.
