# Third-party components

n2-cmd's application license does not replace the licenses or copyright notices
of the components listed below. Each component remains under its own terms.

| Component | Version | Use | License / notice |
| --- | --- | --- | --- |
| Qt Core, Gui, Widgets and Windows/image plugins | 6.8.3 | Dynamically linked interface and image support | LGPL-3.0-only selected; [LGPL](licenses/LGPL-3.0-only.txt), [GPL v3](licenses/GPL-3.0-only.txt), [component notices](licenses/qt-6.8.3-THIRD-PARTY.txt) |
| Lua | 5.4.6 | Statically linked configuration interpreter | [MIT](licenses/lua-LICENSE.txt), Copyright 1994-2023 Lua.org, PUC-Rio |
| libvterm | 0.3.3 | Statically linked terminal parser | [MIT](licenses/libvterm-LICENSE.txt), Copyright 2008 Paul Evans |
| Vazir Code Hack font | Bundled source asset | Embedded terminal font | [Vazir Code/Bitstream Vera](licenses/Vazir-Code-LICENSE.txt) and [Hack](licenses/Hack-LICENSE.md) notices |
| Microsoft Visual C++ runtime | From the build toolchain | Application-local runtime DLLs | [Redistribution notice](licenses/MSVC-RUNTIME-NOTICE.txt) |

## Exact sources

- Qt Base 6.8.3: <https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz>
- Qt Image Formats 6.8.3: <https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtimageformats-everywhere-src-6.8.3.tar.xz>
- Qt Base tag: <https://github.com/qt/qtbase/tree/v6.8.3>
- Qt Image Formats tag: <https://github.com/qt/qtimageformats/tree/v6.8.3>
- Lua 5.4.6: <https://www.lua.org/ftp/lua-5.4.6.tar.gz>
- libvterm pinned revision: <https://github.com/neovim/libvterm/tree/9d6d2112335080312ef8c36667fa717ded4f7daf>
- Vazir Code: <https://github.com/rastikerdar/vazir-code-font>
- Hack: <https://github.com/source-foundry/Hack>

The Lua and libvterm source files needed for the build are included under
`third_party`. Their existing notices remain in those sources. The Qt version
and every deployed file's SHA-256 hash are recorded in each package manifest.

## Qt redistribution and replacement

The standard package uses the LGPL v3 option offered by the Qt 6.8.3 modules
listed above. Qt is distributed as separate DLLs. There is no signature lock or
other application restriction preventing compatible replacement Qt libraries.
Reverse engineering needed to debug modifications to these LGPL libraries is
permitted. Replace the Qt DLLs and matching plugins together with ABI-compatible
x64 builds; preserve the folder layout. The application can also be rebuilt
against a replacement Qt SDK using `scripts/Build.ps1 -QtRoot <sdk>`.

The corresponding Qt sources, including their third-party components, are
available from the exact-version archive links above. Qt build instructions:
<https://doc.qt.io/qt-6/windows-building.html>. Binary redistributors must preserve
these notices and the accompanying license texts and provide the corresponding
library source as required by LGPL v3. Any modified Qt libraries must be accompanied
by the corresponding modified source and build information.

`licenses/qt-6.8.3-THIRD-PARTY.txt` records runtime-related components, versions,
copyrights, declared license expressions and upstream locations from the installed
Qt 6.8.3 SDK's SPDX metadata. The separate standard license texts were copied from
Qt Base's `v6.8.3/LICENSES` directory. Some notices cover code statically included
inside Qt DLLs. Their presence does not mean that a separate DLL is shipped.

## Optional shells

CMD, PowerShell, Git Bash and WSL are detected from the user's system and are not
redistributed with n2-cmd. Clink is optional and not included in the standard
package; obtain it and its license from <https://github.com/chrisant996/clink>.

## Package scope

Release archives include Qt Core/Gui/Widgets, the Windows platform plugin, JPEG,
WebP, GIF and icon image plugins, application-local MSVC runtime DLLs, and the
notices in `licenses`. The application embeds its default wallpaper, icon and
font. Development SDKs, Qt Test, source build outputs, user settings, terminal
history, the Python interpreter and other runtimes are not part of the standard
binary package.
