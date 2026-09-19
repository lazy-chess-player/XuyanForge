param(
    [switch]$NoLaunch,
    [switch]$RunTests
)

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

# Keep toolchain changes inside this PowerShell process; never persist user PATH.
$qtRoot = if ($env:CMAKE_PREFIX_PATH -and (Test-Path -LiteralPath (Join-Path $env:CMAKE_PREFIX_PATH 'bin'))) {
    $env:CMAKE_PREFIX_PATH
} elseif (Test-Path -LiteralPath 'C:\QT\6.11.1\mingw_64\bin') {
    'C:\QT\6.11.1\mingw_64'
} else {
    throw 'Qt kit not found. Set CMAKE_PREFIX_PATH to your Qt kit (for example C:\QT\6.11.1\mingw_64).'
}
$env:CMAKE_PREFIX_PATH = $qtRoot

$toolDirectories = @(
    (Join-Path $qtRoot 'bin'),
    'C:\QT\Tools\mingw1310_64\bin',
    'C:\QT\Tools\Ninja',
    'C:\QT\Tools\CMake_64\bin'
) | Where-Object { Test-Path -LiteralPath $_ }
$env:PATH = ($toolDirectories -join [System.IO.Path]::PathSeparator) + [System.IO.Path]::PathSeparator + $env:PATH

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw 'CMake not found on PATH.' }
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) { throw 'Ninja not found on PATH.' }

Push-Location $projectRoot
try {
    & cmake --preset windows-dev
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)." }
    & cmake --build --preset windows-dev
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }
    if ($RunTests) {
        & ctest --preset windows-dev
        if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)." }
    }
    $executable = Join-Path $projectRoot 'build/windows-dev/xuyanforge_app.exe'
    if (-not (Test-Path -LiteralPath $executable)) { throw "App executable not found: $executable" }
    Write-Host "Build complete: $executable"
    if (-not $NoLaunch) {
        Start-Process -FilePath $executable -WorkingDirectory $projectRoot
    }
} finally {
    Pop-Location
}
