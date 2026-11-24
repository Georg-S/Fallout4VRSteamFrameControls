# Build SteamFrameControls.dll (x64 Release) and package SteamFrameControls-MO2.zip
$ErrorActionPreference = "Stop"

$RootDir = $PSScriptRoot
$BuildDir = Join-Path $RootDir "build"
$ModDir = Join-Path $RootDir "mod\SteamFrameControls"
$PluginsDir = Join-Path $ModDir "F4SE\Plugins"
$ZipPath = Join-Path $RootDir "SteamFrameControls-MO2.zip"

Write-Host "Configuring CMake..."
cmake -S $RootDir -B $BuildDir -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Building Release..."
cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$BuiltDll = Join-Path $BuildDir "Release\SteamFrameControls.dll"
New-Item -ItemType Directory -Force -Path $PluginsDir | Out-Null
Copy-Item $BuiltDll (Join-Path $PluginsDir "SteamFrameControls.dll") -Force

Write-Host "Creating $ZipPath..."
if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($ModDir, $ZipPath)

Write-Host "Done: $ZipPath"
