param(
  [string]$BuildDirectory = "build/preset-windows-msvc-release/src",
  [string]$OutputDirectory = "build/package/FMCW_LiDAR"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$buildRoot = Join-Path $repoRoot $BuildDirectory
$outputRoot = Join-Path $repoRoot $OutputDirectory
$sourceExe = Join-Path $buildRoot "fmcw_lidar_windows.exe"
$packagedExe = Join-Path $outputRoot "FMCW_LiDAR.exe"

if (-not (Test-Path -LiteralPath $sourceExe)) {
  throw "Release executable was not found: $sourceExe"
}

$qtDeployCandidates = @()
if ($env:QT_ROOT) {
  $qtDeployCandidates += Join-Path $env:QT_ROOT "bin/windeployqt.exe"
}

$cachePath = Join-Path (Split-Path -Parent $buildRoot) "CMakeCache.txt"
if (Test-Path -LiteralPath $cachePath) {
  $qtCacheMatch = Select-String -Path $cachePath -Pattern '^Qt6_DIR:PATH=(.+)$' |
      Select-Object -First 1
  if ($qtCacheMatch) {
    $qtCmakePath = $qtCacheMatch.Matches[0].Groups[1].Value -replace '/', '\'
    $qtRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $qtCmakePath))
    $qtDeployCandidates += Join-Path $qtRoot "bin/windeployqt.exe"
  }
}

$qtDeployCommand = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
if ($qtDeployCommand) {
  $qtDeployCandidates += $qtDeployCommand.Source
}
$qtDeployCandidates += "C:\Qt\6.11.0\msvc2022_64\bin\windeployqt.exe"
$qtDeploy = $qtDeployCandidates | Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (-not $qtDeploy) {
  throw "windeployqt.exe was not found. Set QT_ROOT to the MSVC Qt installation."
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
Copy-Item -LiteralPath $sourceExe -Destination $packagedExe -Force

$configSource = Join-Path $repoRoot "config"
$configDestination = Join-Path $outputRoot "config"
New-Item -ItemType Directory -Path $configDestination -Force | Out-Null
Get-ChildItem -LiteralPath $configSource -Force | ForEach-Object {
  Copy-Item -LiteralPath $_.FullName -Destination $configDestination -Recurse -Force
}

Get-ChildItem -LiteralPath $buildRoot -File | Where-Object {
  $_.Name -eq "fftw3f.dll" -or $_.Name -like "cufft64_*.dll"
} | ForEach-Object {
  Copy-Item -LiteralPath $_.FullName -Destination $outputRoot -Force
}

$atsCandidates = @(
  (Join-Path $env:WINDIR "System32/ATSApi.dll"),
  "C:\AlazarTech\ATS-SDK\25.1.0\Samples_C\Library\x64\ATSApi.dll"
)
$atsRuntime = $atsCandidates | Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if ($atsRuntime) {
  Copy-Item -LiteralPath $atsRuntime -Destination $outputRoot -Force
} else {
  Write-Warning "ATSApi.dll was not found. Simulator works, but Alazar mode requires the runtime DLL."
}

& $qtDeploy --release --no-translations --compiler-runtime --dir $outputRoot $packagedExe
if ($LASTEXITCODE -ne 0) {
  throw "windeployqt failed with exit code $LASTEXITCODE"
}

$requiredFiles = @(
  $packagedExe,
  (Join-Path $outputRoot "Qt6Core.dll"),
  (Join-Path $outputRoot "Qt6Widgets.dll"),
  (Join-Path $outputRoot "platforms/qwindows.dll")
)
$missingFiles = $requiredFiles | Where-Object { -not (Test-Path -LiteralPath $_) }
if ($missingFiles) {
  throw "Windows package is incomplete: $($missingFiles -join ', ')"
}

Push-Location $outputRoot
try {
  & $packagedExe --smoke-test
  if ($LASTEXITCODE -ne 0) {
    throw "Packaged executable smoke test failed with exit code $LASTEXITCODE"
  }
} finally {
  Pop-Location
}

$hash = (Get-FileHash -LiteralPath $packagedExe -Algorithm SHA256).Hash
Write-Host "Windows package ready: $outputRoot"
Write-Host "Executable SHA-256: $hash"
