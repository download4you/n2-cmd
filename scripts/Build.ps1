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
if ($Test) {
    $env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
    & ctest --test-dir $build -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }
}
