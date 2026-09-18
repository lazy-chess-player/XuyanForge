param(
    [string]$BuildDirectory = "build/windows-release",
    [string]$OutputDirectory = "build/windows-package",
    [switch]$CreatePortableZip,
    [switch]$CreateInstaller
)

$ErrorActionPreference = "Stop"
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildPath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$outputPath = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))

if (-not $buildPath.StartsWith($projectRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
    -not $outputPath.StartsWith($projectRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Build and output directories must stay inside the XuyanForge workspace."
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
& windeployqt --release --qmldir (Join-Path $projectRoot "ui/qml") --dir $outputPath $deployedExecutable
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

Write-Host "Windows deployment created at $outputPath"

$manifest = @{
    product = "XuyanForge"
    version = "0.2-alpha"
    architecture = "x86_64"
    user_data_location = "Qt AppLocalDataLocation"
    uninstall_data_policy = "retain"
    signed = $false
    generated_utc = [DateTime]::UtcNow.ToString("o")
} | ConvertTo-Json
Set-Content -LiteralPath (Join-Path $outputPath "release-manifest.json") -Value $manifest -Encoding UTF8

if ($CreatePortableZip) {
    $zipPath = Join-Path (Split-Path $outputPath -Parent) "XuyanForge-0.2-alpha-windows-x86_64-portable.zip"
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
    Compress-Archive -Path (Join-Path $outputPath "*") -DestinationPath $zipPath -CompressionLevel Optimal
    Write-Host "Portable archive created at $zipPath"
}

if ($CreateInstaller) {
    $makeNsis = Get-Command makensis -ErrorAction SilentlyContinue
    if (-not $makeNsis) { throw "NSIS makensis was not found. Install NSIS or omit -CreateInstaller." }
    $installerPath = Join-Path (Split-Path $outputPath -Parent) "XuyanForge-0.2-alpha-windows-x86_64-setup.exe"
    $scriptPath = Join-Path (Split-Path $outputPath -Parent) "xuyanforge-installer.generated.nsi"
    $escapedOutput = $outputPath.Replace('$', '$$')
    $escapedInstaller = $installerPath.Replace('$', '$$')
    $nsis = @"
Unicode True
Name "叙演工坊"
OutFile "$escapedInstaller"
InstallDir "`$PROGRAMFILES64\XuyanForge"
RequestExecutionLevel admin
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles
Section "XuyanForge" SEC_MAIN
  SetOutPath "`$INSTDIR"
  File /r "$escapedOutput\*"
  CreateDirectory "`$SMPROGRAMS\XuyanForge"
  CreateShortcut "`$SMPROGRAMS\XuyanForge\叙演工坊.lnk" "`$INSTDIR\xuyanforge_app.exe"
  WriteUninstaller "`$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\XuyanForge" "DisplayName" "叙演工坊"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\XuyanForge" "UninstallString" '"`$INSTDIR\Uninstall.exe"'
SectionEnd
Section "Uninstall"
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
