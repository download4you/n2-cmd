[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackageDirectory,
    [string]$RuntimeReport
)

$ErrorActionPreference = 'Stop'
$utf8 = [Text.UTF8Encoding]::new($false)
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8
$package = [IO.Path]::GetFullPath($PackageDirectory).TrimEnd('\', '/')
if (-not (Test-Path -LiteralPath $package -PathType Container)) { throw 'Package directory does not exist.' }
$prefix = $package + [IO.Path]::DirectorySeparatorChar
$required = @(
    'n2-cmd.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
    'platforms\qwindows.dll', 'imageformats\qjpeg.dll', 'imageformats\qwebp.dll',
    'imageformats\qgif.dll', 'imageformats\qico.dll',
    'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
    'README.md', 'LICENSE', 'THIRD_PARTY.md', 'package-manifest.json',
    'config\defaults.lua', 'config\quick_commands.lua',
    'licenses\LGPL-3.0-only.txt', 'licenses\GPL-3.0-only.txt',
    'licenses\qt-6.8.3-THIRD-PARTY.txt', 'licenses\lua-LICENSE.txt',
    'licenses\libvterm-LICENSE.txt', 'licenses\Vazir-Code-LICENSE.txt',
    'licenses\Hack-LICENSE.md', 'licenses\MSVC-RUNTIME-NOTICE.txt'
)
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $relative) -PathType Leaf)) {
        throw ('Required package file is missing: ' + $relative)
    }
}

$allowedQt = @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll')
$allowedPlugins = @('platforms/qwindows.dll', 'imageformats/qjpeg.dll', 'imageformats/qwebp.dll', 'imageformats/qgif.dll', 'imageformats/qico.dll')
$allFiles = @(Get-ChildItem -LiteralPath $package -Recurse -Force -File)
foreach ($file in $allFiles) {
    $relative = $file.FullName.Substring($prefix.Length).Replace('\', '/')
    if ($relative -match '(^|/)(\.git|\.vs|target|__pycache__)(/|$)' -or
        $file.Name -match '^(settings\.json|preferences\.lua|wallpaper_custom\..*)$' -or
        $file.Extension -match '^\.(pdb|obj|ilk|lib|exp|py|pyc|pyo|user|suo|log|tmp)$') {
        throw ('Development or user-state file found in package: ' + $relative)
    }
    if ($file.Name -like 'Qt6*.dll' -and $file.Name -notin $allowedQt) {
        throw ('Unexpected Qt runtime library: ' + $relative)
    }
    if ($file.Extension -ieq '.dll') {
        if ($relative.Contains('/')) {
            if ($relative -notin $allowedPlugins) { throw ('Unexpected plugin DLL: ' + $relative) }
        } elseif ($file.Name -notin $allowedQt -and
            $file.Name -notmatch '^(msvcp140[^/]*|vcruntime140[^/]*|concrt140|vccorlib140)\.dll$') {
            throw ('Unexpected runtime DLL: ' + $relative)
        }
    }
    if ($file.Extension -ieq '.exe' -and $file.Name -ine 'n2-cmd.exe') {
        throw ('Unexpected executable in package: ' + $relative)
    }
}

$binary = [IO.File]::ReadAllBytes((Join-Path $package 'n2-cmd.exe'))
if ($binary.Length -lt 256 -or $binary[0] -ne 0x4d -or $binary[1] -ne 0x5a) { throw 'n2-cmd.exe is not a PE executable.' }
$peOffset = [BitConverter]::ToInt32($binary, 0x3c)
if ($peOffset -lt 0 -or $peOffset + 6 -gt $binary.Length -or
    [BitConverter]::ToUInt32($binary, $peOffset) -ne 0x00004550 -or
    [BitConverter]::ToUInt16($binary, $peOffset + 4) -ne 0x8664) {
    throw 'n2-cmd.exe is not a Windows x64 executable.'
}

$manifest = Get-Content -LiteralPath (Join-Path $package 'package-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.schema_version -ne 1 -or $manifest.architecture -ne 'windows-x64' -or $manifest.qt_version -ne '6.8.3') {
    throw 'Unsupported or inconsistent package manifest.'
}
$entries = @($manifest.files)
if ($entries.Count -ne $allFiles.Count - 1) { throw 'Manifest file count differs from the package.' }
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $entries) {
    if (-not $seen.Add([string]$entry.path)) { throw ('Duplicate manifest path: ' + $entry.path) }
    if ([IO.Path]::IsPathRooted($entry.path) -or $entry.path -match '(^|[/\\])\.\.([/\\]|$)') { throw 'Unsafe manifest path.' }
    $path = [IO.Path]::GetFullPath((Join-Path $package $entry.path))
    if (-not $path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest path escapes the package.' }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw ('Manifest file is missing: ' + $entry.path) }
    if ((Get-Item -LiteralPath $path).Length -ne $entry.size -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $entry.sha256) {
        throw ('Manifest integrity check failed: ' + $entry.path)
    }
}

foreach ($dll in $allowedQt) {
    $version = (Get-Item -LiteralPath (Join-Path $package $dll)).VersionInfo.ProductVersion
    if ($version -notmatch '^6\.8\.3(?:\.|$)') { throw ('Unexpected Qt version in ' + $dll + ': ' + $version) }
}
if ($RuntimeReport) {
    $report = Get-Content -LiteralPath $RuntimeReport -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($report.qt_version -ne '6.8.3' -or $report.application_version -ne $manifest.application_version) {
        throw 'Runtime version does not match the package manifest.'
    }
    foreach ($format in @('png', 'jpg', 'jpeg', 'webp', 'bmp', 'gif')) {
        if ($format -notin $report.image_formats) { throw ('Packaged image format unavailable: ' + $format) }
    }
}
Write-Output ('Package verified: ' + $allFiles.Count + ' files, x64, Qt 6.8.3, SHA-256 manifest matches.')
