[CmdletBinding()]
param(
    [string]$QtRoot = $env:QT_ROOT_DIR,
    [string]$BuildDirectory,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$utf8 = [Text.UTF8Encoding]::new($false)
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $project 'build\native' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $project 'dist' }
$build = [IO.Path]::GetFullPath($BuildDirectory)
$output = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\', '/')
if (-not $QtRoot) {
    $candidate = [IO.Path]::GetFullPath((Join-Path $project '..\tools\Qt\6.8.3\msvc2022_64'))
    if (Test-Path -LiteralPath $candidate) { $QtRoot = $candidate }
}
if (-not $QtRoot) { throw 'Pass -QtRoot or set QT_ROOT_DIR to the Qt 6.8.3 MSVC x64 SDK.' }
$QtRoot = [IO.Path]::GetFullPath($QtRoot)
$deploy = Join-Path $QtRoot 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $deploy -PathType Leaf)) { throw 'windeployqt.exe was not found in the Qt SDK.' }
if ((Get-Item -LiteralPath (Join-Path $QtRoot 'bin\Qt6Core.dll')).VersionInfo.ProductVersion -notmatch '^6\.8\.3(?:\.|$)') {
    throw 'Release packaging requires the pinned Qt 6.8.3 SDK.'
}
if (-not (Test-Path -LiteralPath (Join-Path $build 'Release\n2-cmd.exe') -PathType Leaf)) {
    throw 'Build the Release target and run tests before packaging: scripts\Build.ps1 -Test.'
}
foreach ($notice in @('LICENSE', 'README.md', 'THIRD_PARTY.md', 'licenses\qt-6.8.3-THIRD-PARTY.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $project $notice) -PathType Leaf)) { throw ('Missing publication file: ' + $notice) }
}

$cmake = Get-Content -LiteralPath (Join-Path $project 'CMakeLists.txt') -Raw -Encoding UTF8
$match = [regex]::Match($cmake, 'project\(n2-cmd\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $match.Success) { throw 'Cannot read the application version from CMakeLists.txt.' }
$version = $match.Groups[1].Value
$stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$name = 'n2-cmd-' + $version + '-windows-x64-' + $stamp
$stage = [IO.Path]::GetFullPath((Join-Path $output $name))
if (-not $stage.StartsWith($output + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package staging path is outside the output directory.'
}
if (Test-Path -LiteralPath $stage) { throw 'Package staging directory already exists.' }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
& cmake --install $build --config Release --prefix $stage
if ($LASTEXITCODE -ne 0) { throw 'CMake install failed; the staged output is retained for inspection.' }
Copy-Item -LiteralPath (Join-Path $project 'THIRD_PARTY.md') -Destination (Join-Path $stage 'THIRD_PARTY.md')
New-Item -ItemType Directory -Path (Join-Path $stage 'docs') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $project 'docs\FEATURES.md') -Destination (Join-Path $stage 'docs\FEATURES.md')

$originalPath = $env:PATH
try {
    $env:PATH = (Join-Path $QtRoot 'bin') + ';' + $originalPath
    & $deploy --release --no-translations --no-plugins --no-network --exclude-plugins qsvg,qsvgicon --skip-plugin-types iconengines,generic,styles --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler --no-compiler-runtime (Join-Path $stage 'n2-cmd.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
} finally { $env:PATH = $originalPath }

$packaged = Join-Path $project 'dist\current'
$packagedCore = Join-Path $packaged 'Qt6Core.dll'
$canUsePackaged = $false
if (Test-Path -LiteralPath $packagedCore -PathType Leaf) {
    $canUsePackaged = (Get-Item -LiteralPath (Join-Path $QtRoot 'bin\Qt6Core.dll')).VersionInfo.ProductVersion -eq
        (Get-Item -LiteralPath $packagedCore).VersionInfo.ProductVersion
}
$plugins = @('platforms\qwindows.dll', 'imageformats\qjpeg.dll', 'imageformats\qwebp.dll', 'imageformats\qgif.dll', 'imageformats\qico.dll')
foreach ($plugin in $plugins) {
    $source = Join-Path (Join-Path $QtRoot 'plugins') $plugin
    if (-not (Test-Path -LiteralPath $source -PathType Leaf) -and $canUsePackaged) {
        $source = Join-Path $packaged $plugin
    }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw ('Required Qt plugin is missing; install Qt Image Formats: ' + $plugin) }
    $destination = Join-Path $stage $plugin
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
}

$crt = $null
if ($env:VCToolsRedistDir) {
    $candidate = Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC143.CRT'
    if (Test-Path -LiteralPath $candidate -PathType Container) { $crt = $candidate }
}
if (-not $crt) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw 'Cannot locate the Visual Studio redistributable runtime.' }
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'Cannot locate a Visual Studio C++ installation.' }
    $redist = Join-Path ([string]$installation).Trim() 'VC\Redist\MSVC'
    $versions = @(Get-ChildItem -LiteralPath $redist -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending)
    foreach ($directory in $versions) {
        $candidate = Join-Path $directory.FullName 'x64\Microsoft.VC143.CRT'
        if (Test-Path -LiteralPath $candidate -PathType Container) { $crt = $candidate; break }
    }
}
if (-not $crt) { throw 'Visual Studio x64 Microsoft.VC143.CRT redistributable directory was not found.' }
foreach ($dll in Get-ChildItem -LiteralPath $crt -File -Filter '*.dll') {
    Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $stage $dll.Name)
}

$reportPath = Join-Path $output ($name + '.runtime.json')
$savedEnvironment = @{}
foreach ($variable in @('PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QPA_PLATFORM')) {
    $savedEnvironment[$variable] = [Environment]::GetEnvironmentVariable($variable, 'Process')
}
try {
    $env:PATH = $stage + ';' + (Join-Path $env:SystemRoot 'System32') + ';' + $env:SystemRoot
    $env:QT_PLUGIN_PATH = $stage
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $stage 'platforms'
    $env:QT_QPA_PLATFORM = 'windows'
    $process = Start-Process -FilePath (Join-Path $stage 'n2-cmd.exe') -ArgumentList @('--runtime-report', ('"' + $reportPath + '"')) -WorkingDirectory $stage -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(15000)) {
        $process.Kill()
        throw 'Packaged runtime check timed out.'
    }
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
        throw 'Packaged runtime check failed to create its report.'
    }
} finally {
    foreach ($variable in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($variable, $savedEnvironment[$variable], 'Process')
    }
}

$prefix = $stage.TrimEnd('\') + '\'
$files = @(Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($prefix.Length).Replace('\', '/')
        size = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
$manifest = [ordered]@{
    schema_version = 1
    application_version = $version
    qt_version = '6.8.3'
    architecture = 'windows-x64'
    created_utc = [DateTime]::UtcNow.ToString('o')
    files = $files
}
[IO.File]::WriteAllText((Join-Path $stage 'package-manifest.json'), ($manifest | ConvertTo-Json -Depth 5), $utf8)
& (Join-Path $PSScriptRoot 'Inspect-Package.ps1') -PackageDirectory $stage -RuntimeReport $reportPath

$archive = Join-Path $output ($name + '.zip')
Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($archive + '.sha256'), ($archiveHash + '  ' + [IO.Path]::GetFileName($archive) + "`n"), $utf8)

$current = Join-Path $output 'current'
if (Test-Path -LiteralPath $current) {
    $existing = Get-Item -LiteralPath $current -Force
    if ($existing.LinkType -ne 'Junction') {
        throw ('Refusing to replace a non-junction path: ' + $current)
    }
    [IO.Directory]::Delete($current, $false)
}
New-Item -ItemType Junction -Path $current -Target $stage | Out-Null

Write-Output ('Release package: ' + $archive)
Write-Output ('Current package: ' + $current)
Write-Output ('SHA-256: ' + $archiveHash)
