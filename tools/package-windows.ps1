<#
功能：将已构建桌面程序部署到全新目录，可生成便携包和简体中文安装器。
参数：BuildDirectory为build下构建子目录，默认windows-release；OutputDirectory为独立空目录，默认UTC时间加随机标识；
      CreatePortableZip指定生成便携包；CreateInstaller指定调用本机NSIS。
返回：成功进程状态为0；路径、版本、部署或安装器错误抛异常，使脚本失败。
副作用：复制产品依赖、生成清单/归档，不复制用户数据库和凭据；卸载保留应用数据目录。
前置条件：构建目录含程序及CMake版本记录，Qt部署工具可执行；安装器另需NSIS。
#>
param(
    [string]$BuildDirectory = "build/windows-release",
    [string]$OutputDirectory = ("build/windows-package-" + [DateTime]::UtcNow.ToString("yyyyMMddHHmmss") + "-" + [Guid]::NewGuid().ToString("N")),
    [switch]$CreatePortableZip,
    [switch]$CreateInstaller
)

$ErrorActionPreference = "Stop"
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "build"))
$buildPath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$outputPath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
$buildPrefix = $buildRoot.TrimEnd([char[]]@('\', '/')) + [System.IO.Path]::DirectorySeparatorChar
$sourcePrefix = $buildPath.TrimEnd([char[]]@('\', '/')) + [System.IO.Path]::DirectorySeparatorChar

if (-not $buildPath.StartsWith($buildPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
    -not $outputPath.StartsWith($buildPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
    $outputPath.Equals($buildPath, [System.StringComparison]::OrdinalIgnoreCase) -or
    $outputPath.StartsWith($sourcePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "构建和部署目录必须是 build 下相互分离的子目录。"
}

if ((Test-Path -LiteralPath $outputPath) -and
    (Get-ChildItem -LiteralPath $outputPath -Force | Select-Object -First 1)) {
    throw "部署目录非空，请使用全新目录：$outputPath"
}

$executable = Join-Path $buildPath "xuyanforge_app.exe"
if (-not (Test-Path -LiteralPath $executable)) {
    throw "未找到已构建的程序：$executable"
}
if (-not (Get-Command windeployqt -ErrorAction SilentlyContinue)) {
    throw "未找到 Qt 部署工具，请先配置工具路径。"
}

# 构建版本冻结；不允许给旧二进制贴上已经变更的源码版本。
$builtVersionPath = Join-Path $buildPath 'release-version.json'
if (-not (Test-Path -LiteralPath $builtVersionPath)) { throw '构建目录缺少版本记录，请重新配置并编译。' }
$builtVersion = Get-Content -LiteralPath $builtVersionPath -Raw -Encoding UTF8 | ConvertFrom-Json
$sourceVersion = Get-Content -LiteralPath (Join-Path $projectRoot 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($field in @('version', 'channel', 'displayName')) {
    if ($builtVersion.$field -ne $sourceVersion.$field) { throw '源码与构建版本不一致，请重新编译。' }
}
if ($builtVersion.version -notmatch '^\d+\.\d+\.\d+$' -or $builtVersion.channel -notmatch '^[a-z][a-z0-9]*$') {
    throw '版本记录格式无效。'
}
$artifactVersion = $builtVersion.version + '-' + $builtVersion.channel

New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$deployedExecutable = Join-Path $outputPath "xuyanforge_app.exe"
Copy-Item -LiteralPath $executable -Destination $deployedExecutable -Force
& windeployqt --release --translations zh_CN --qmldir (Join-Path $projectRoot "ui/qml") --dir $outputPath $deployedExecutable
if ($LASTEXITCODE -ne 0) {
    throw "Qt 部署失败，退出码：$LASTEXITCODE"
}
if (-not (Test-Path -LiteralPath (Join-Path $outputPath 'translations/qt_zh_CN.qm'))) {
    throw '部署缺少 Qt 简体中文翻译，不能发布。'
}

$blockedExtensions = @('.sqlite', '.db', '.wal', '.shm', '.txt', '.md', '.key', '.pem')
$blockedNames = @('core_tests.exe', 'stress_tests.exe', 'CMakeCache.txt', '.env')
$unexpected = @(Get-ChildItem -LiteralPath $outputPath -Recurse -File -Force | Where-Object {
    $blockedExtensions -contains $_.Extension.ToLowerInvariant() -or
    $blockedNames -contains $_.Name
})
if ($unexpected.Count -gt 0) {
    throw "部署混入非产品文件，请检查：$outputPath"
}

Write-Host "桌面部署已生成：$outputPath"

# 中文展示版本来自已构建版本记录，机器字段保持稳定。
$manifest = @{
    product = "XuyanForge"
    version = $artifactVersion
    version_label = $builtVersion.displayName
    architecture = "x86_64"
    user_data_location = "Qt AppLocalDataLocation"
    uninstall_data_policy = "retain"
    signed = $false
    generated_utc = [DateTime]::UtcNow.ToString("o")
} | ConvertTo-Json
Set-Content -LiteralPath (Join-Path $outputPath "release-manifest.json") -Value $manifest -Encoding UTF8

if ($CreatePortableZip) {
    $artifactId = [DateTime]::UtcNow.ToString("yyyyMMddHHmmss") + "-" + [Guid]::NewGuid().ToString("N")
    $zipPath = Join-Path (Split-Path $outputPath -Parent) "XuyanForge-$artifactVersion-windows-x86_64-portable-$artifactId.zip"
    Compress-Archive -Path (Join-Path $outputPath "*") -DestinationPath $zipPath -CompressionLevel Optimal
    Write-Host "便携压缩包已生成：$zipPath"
}

if ($CreateInstaller) {
    $makeNsis = Get-Command makensis -ErrorAction SilentlyContinue
    if (-not $makeNsis) { throw "未找到 NSIS 工具，请配置该工具或去掉安装器开关。" }
    $artifactId = [DateTime]::UtcNow.ToString("yyyyMMddHHmmss") + "-" + [Guid]::NewGuid().ToString("N")
    $installerPath = Join-Path (Split-Path $outputPath -Parent) "XuyanForge-$artifactVersion-windows-x86_64-setup-$artifactId.exe"
    $scriptPath = Join-Path (Split-Path $outputPath -Parent) "xuyanforge-installer-$artifactId.generated.nsi"
    $escapedOutput = $outputPath.Replace('$', '$$')
    $escapedInstaller = $installerPath.Replace('$', '$$')
    $nsis = @"
Unicode True
LoadLanguageFile "`${NSISDIR}\Contrib\Language files\SimpChinese.nlf"
Name "叙演工坊"
OutFile "$escapedInstaller"
InstallDir "`$PROGRAMFILES64\XuyanForge"
RequestExecutionLevel admin
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles
UninstallText "确定卸载叙演工坊吗？" "本地工作区和小说资料将保留。"
Section "安装叙演工坊" SEC_MAIN
  SetOutPath "`$INSTDIR"
  File /r "$escapedOutput\*"
  CreateDirectory "`$SMPROGRAMS\XuyanForge"
  CreateShortcut "`$SMPROGRAMS\XuyanForge\叙演工坊.lnk" "`$INSTDIR\xuyanforge_app.exe"
  WriteUninstaller "`$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\XuyanForge" "DisplayName" "叙演工坊"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\XuyanForge" "UninstallString" '"`$INSTDIR\Uninstall.exe"'
SectionEnd
; NSIS 要求此内部段名为 Uninstall；可见卸载提示由上方中文词条提供。
Section "Uninstall"
  Delete "`$SMPROGRAMS\XuyanForge\叙演工坊.lnk"
  RMDir "`$SMPROGRAMS\XuyanForge"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\XuyanForge"
  RMDir /r "`$INSTDIR"
  ; 卸载只移除安装目录，用户工作区保留。
SectionEnd
"@
    Set-Content -LiteralPath $scriptPath -Value $nsis -Encoding UTF8
    & $makeNsis.Source $scriptPath
    if ($LASTEXITCODE -ne 0) { throw "安装器编译失败，退出码：$LASTEXITCODE" }
    Write-Host "未签名安装器已生成：$installerPath；卸载保留用户工作区。"
}
