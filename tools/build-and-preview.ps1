<#
功能：使用本机Qt工具链配置和编译桌面程序，可运行完整回归，并按需启动界面供用户查看。
参数：NoLaunch为真时仅构建/测试；RunTests为真时在构建成功后运行CTest，默认不运行。
返回：成功状态为0；环境、配置、编译或测试失败以异常终止，不启动旧程序。
副作用：工具路径只在本脚本进程内变化；更新build/windows-dev；启动应用由用户的一键预览操作触发。
#>
param(
    [switch]$NoLaunch,
    [switch]$RunTests
)

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

# 工具链只作用于当前进程，避免修改用户永久环境。
$qtRoot = if ($env:CMAKE_PREFIX_PATH -and (Test-Path -LiteralPath (Join-Path $env:CMAKE_PREFIX_PATH 'bin'))) {
    $env:CMAKE_PREFIX_PATH
} elseif (Test-Path -LiteralPath 'C:\QT\6.11.1\mingw_64\bin') {
    'C:\QT\6.11.1\mingw_64'
} else {
    throw '未找到 Qt 工具链，请将 CMAKE_PREFIX_PATH 设置为本机 Qt 安装目录。'
}
$env:CMAKE_PREFIX_PATH = $qtRoot

$toolDirectories = @(
    (Join-Path $qtRoot 'bin'),
    'C:\QT\Tools\mingw1310_64\bin',
    'C:\QT\Tools\Ninja',
    'C:\QT\Tools\CMake_64\bin'
) | Where-Object { Test-Path -LiteralPath $_ }
$env:PATH = ($toolDirectories -join [System.IO.Path]::PathSeparator) + [System.IO.Path]::PathSeparator + $env:PATH

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw '未找到 CMake 构建工具。' }
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) { throw '未找到 Ninja 编译调度工具。' }

Push-Location $projectRoot
try {
    & cmake --preset windows-dev
    if ($LASTEXITCODE -ne 0) { throw "构建配置失败，退出码：$LASTEXITCODE" }
    & cmake --build --preset windows-dev
    if ($LASTEXITCODE -ne 0) { throw "编译失败，退出码：$LASTEXITCODE" }
    if ($RunTests) {
        & ctest --preset windows-dev
        if ($LASTEXITCODE -ne 0) { throw "回归测试失败，退出码：$LASTEXITCODE" }
    }
    $executable = Join-Path $projectRoot 'build/windows-dev/xuyanforge_app.exe'
    if (-not (Test-Path -LiteralPath $executable)) { throw "未找到本次构建的桌面程序：$executable" }
    Write-Host "编译完成：$executable"
    if (-not $NoLaunch) {
        Start-Process -FilePath $executable -WorkingDirectory $projectRoot
    }
} finally {
    Pop-Location
}
