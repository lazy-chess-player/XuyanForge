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
    throw "Build and output directories must be separate subdirectories of build."
}

if ((Test-Path -LiteralPath $outputPath) -and
    (Get-ChildItem -LiteralPath $outputPath -Force | Select-Object -First 1)) {
    throw "Output directory is not empty. Choose a fresh directory to avoid bundling stale files: $outputPath"
}

$executable = Join-Path $buildPath "xuyanforge_app.exe"
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Application executable not found: $executable"
}
if (-not (Get-Command windeployqt -ErrorAction SilentlyContinue)) {
    throw "windeployqt is not on PATH. Start a Qt command environment first."
}

New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$deployedExecutable = Join-Path $outputPath "xuyanforge_app.exe"
Copy-Item -LiteralPath $executable -Destination $deployedExecutable -Force
& windeployqt --release --translations zh_CN --qmldir (Join-Path $projectRoot "ui/qml") --dir $outputPath $deployedExecutable
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
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
    throw "Deployment contains non-product files; inspect the fresh output directory: $outputPath"
}

Write-Host "Windows deployment created at $outputPath"

# PowerShell 5.1 may decode a UTF-8 script without BOM as a legacy code page.
$manifest = @{
    product = "XuyanForge"
    version = "0.2-alpha"
    version_label = ("0.2 " + [char]0x9884 + [char]0x89C8 + [char]0x7248)
    architecture = "x86_64"
    user_data_location = "Qt AppLocalDataLocation"
    uninstall_data_policy = "retain"
    signed = $false
    generated_utc = [DateTime]::UtcNow.ToString("o")
} | ConvertTo-Json
Set-Content -LiteralPath (Join-Path $outputPath "release-manifest.json") -Value $manifest -Encoding UTF8

if ($CreatePortableZip) {
    $artifactId = [DateTime]::UtcNow.ToString("yyyyMMddHHmmss") + "-" + [Guid]::NewGuid().ToString("N")
    $zipPath = Join-Path (Split-Path $outputPath -Parent) "XuyanForge-0.2-alpha-windows-x86_64-portable-$artifactId.zip"
    Compress-Archive -Path (Join-Path $outputPath "*") -DestinationPath $zipPath -CompressionLevel Optimal
    Write-Host "Portable archive created at $zipPath"
}

if ($CreateInstaller) {
    $makeNsis = Get-Command makensis -ErrorAction SilentlyContinue
    if (-not $makeNsis) { throw "NSIS makensis was not found. Install NSIS or omit -CreateInstaller." }
    $artifactId = [DateTime]::UtcNow.ToString("yyyyMMddHHmmss") + "-" + [Guid]::NewGuid().ToString("N")
    $installerPath = Join-Path (Split-Path $outputPath -Parent) "XuyanForge-0.2-alpha-windows-x86_64-setup-$artifactId.exe"
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
Section "安装叙演工坊" SEC_MAIN
  SetOutPath "`$INSTDIR"
  File /r "$escapedOutput\*"
  CreateDirectory "`$SMPROGRAMS\XuyanForge"
  CreateShortcut "`$SMPROGRAMS\XuyanForge\叙演工坊.lnk" "`$INSTDIR\xuyanforge_app.exe"
  WriteUninstaller "`$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\XuyanForge" "DisplayName" "叙演工坊"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\XuyanForge" "UninstallString" '"`$INSTDIR\Uninstall.exe"'
SectionEnd
Section "卸载"
  Delete "`$SMPROGRAMS\XuyanForge\叙演工坊.lnk"
  RMDir "`$SMPROGRAMS\XuyanForge"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\XuyanForge"
  RMDir /r "`$INSTDIR"
  ; User workspaces under AppLocalDataLocation are intentionally retained.
SectionEnd
"@
    Set-Content -LiteralPath $scriptPath -Value $nsis -Encoding UTF8
    & $makeNsis.Source $scriptPath
    if ($LASTEXITCODE -ne 0) { throw "makensis failed with exit code $LASTEXITCODE" }
    Write-Host "Unsigned installer created at $installerPath; user data is retained on uninstall."
}
