[CmdletBinding()]
param(
    [string]$QtRoot = $env:QT_ROOT_DIR,
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$utf8 = [Text.UTF8Encoding]::new($false)
[Console]::InputEncoding = $utf8
[Console]::OutputEncoding = $utf8
$OutputEncoding = $utf8
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $QtRoot) {
    $localSdk = [IO.Path]::GetFullPath((Join-Path $project '..\tools\Qt\6.8.3\msvc2022_64'))
    if (Test-Path -LiteralPath $localSdk) { $QtRoot = $localSdk }
}
if (-not $QtRoot -or -not (Test-Path -LiteralPath (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    throw 'Set QT_ROOT_DIR or pass -QtRoot with a Qt 6.8+ MSVC x64 SDK.'
}
$build = Join-Path $project 'build\native'
& cmake -S $project -B $build -G 'Visual Studio 17 2022' -A x64 ('-DCMAKE_PREFIX_PATH='+$QtRoot) -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $build --config $Configuration --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }

$target = Join-Path $build $Configuration
$deploy = Join-Path $QtRoot 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $deploy -PathType Leaf)) { throw 'windeployqt.exe was not found in the Qt SDK.' }
$originalPath = $env:PATH
try {
    $env:PATH = (Join-Path $QtRoot 'bin') + ';' + $originalPath
    & $deploy ('--' + $Configuration.ToLowerInvariant()) --no-translations --no-plugins --no-network `
        --exclude-plugins qsvg,qsvgicon --skip-plugin-types iconengines,generic,styles --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler `
        --compiler-runtime (Join-Path $target 'n2-cmd.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Qt runtime deployment failed.' }
} finally { $env:PATH = $originalPath }

$suffix = if ($Configuration -eq 'Debug') { 'd' } else { '' }
$qtVersion = (Get-Item -LiteralPath (Join-Path $QtRoot 'bin\Qt6Core.dll')).VersionInfo.ProductVersion
$packaged = Join-Path $project 'dist\current'
$packagedCore = Join-Path $packaged 'Qt6Core.dll'
$canUsePackaged = $false
if ($Configuration -eq 'Release' -and (Test-Path -LiteralPath $packagedCore -PathType Leaf)) {
    $canUsePackaged = $qtVersion -eq (Get-Item -LiteralPath $packagedCore).VersionInfo.ProductVersion
}
$plugins = @("platforms\qwindows$suffix.dll", "imageformats\qjpeg$suffix.dll", "imageformats\qwebp$suffix.dll",
    "imageformats\qgif$suffix.dll", "imageformats\qico$suffix.dll")
foreach ($plugin in $plugins) {
    $source = Join-Path (Join-Path $QtRoot 'plugins') $plugin
    if (-not (Test-Path -LiteralPath $source -PathType Leaf) -and $canUsePackaged) {
        $source = Join-Path $packaged $plugin
    }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw ('Required Qt plugin is missing; install Qt Image Formats: ' + $plugin) }
    $destination = Join-Path $target $plugin
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}

$savedEnvironment = @{}
foreach ($variable in @('PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QPA_PLATFORM')) {
    $savedEnvironment[$variable] = [Environment]::GetEnvironmentVariable($variable, 'Process')
}
try {
    $env:PATH = $target + ';' + (Join-Path $env:SystemRoot 'System32') + ';' + $env:SystemRoot
    $env:QT_PLUGIN_PATH = $target
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $target 'platforms'
    $env:QT_QPA_PLATFORM = 'windows'
    $reportPath = Join-Path $build ('runtime-' + $Configuration + '.json')
    $process = Start-Process -FilePath (Join-Path $target 'n2-cmd.exe') -ArgumentList @('--runtime-report', ('"' + $reportPath + '"')) `
        -WorkingDirectory $target -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(15000)) {
        $process.Kill()
        throw 'Build runtime check timed out.'
    }
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
        throw 'Build runtime check failed; the executable is not self-contained.'
    }
    if ($Test) {
        $env:PATH = $target + ';' + (Join-Path $QtRoot 'bin') + ';' + $originalPath
        & ctest --test-dir $build -C $Configuration --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }
    }
} finally {
    foreach ($variable in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($variable, $savedEnvironment[$variable], 'Process')
    }
}
