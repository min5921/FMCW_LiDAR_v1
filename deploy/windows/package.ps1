param(
  [string]$BuildDirectory = "build/preset-windows-centerpoint/src",
  [string]$OutputDirectory = "build/package/Windows-CenterPoint"
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
Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $buildRoot) "BUILD_FEATURES.txt") -Destination $outputRoot -Force
Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $buildRoot) "BUILD_SOURCES.sha256") -Destination $outputRoot -Force

$configSource = Join-Path $repoRoot "config"
$configDestination = Join-Path $outputRoot "config"
New-Item -ItemType Directory -Path $configDestination -Force | Out-Null
Get-ChildItem -LiteralPath $configSource -Force | ForEach-Object {
  Copy-Item -LiteralPath $_.FullName -Destination $configDestination -Recurse -Force
}

Get-ChildItem -LiteralPath $buildRoot -File | Where-Object {
  $_.Name -eq "fftw3f.dll" -or $_.Name -like "cufft64_*.dll" -or
      $_.Name -like "cudnn*.dll"
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
  Write-Warning "ATSApi.dll was not found. An ATS-enabled executable may fail to load; package smoke test must pass."
}

& $qtDeploy --release --no-translations --compiler-runtime --dir $outputRoot $packagedExe
if ($LASTEXITCODE -ne 0) {
  throw "windeployqt failed with exit code $LASTEXITCODE"
}

$redistRoots = @()
if ($env:VCToolsRedistDir) { $redistRoots += $env:VCToolsRedistDir }
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
if (Test-Path -LiteralPath $vswhere) {
  $installation = & $vswhere -latest -products '*' -property installationPath
  if ($installation) {
    $redistParent = Join-Path $installation "VC/Redist/MSVC"
    if (Test-Path -LiteralPath $redistParent) {
      $redistRoots += Get-ChildItem -LiteralPath $redistParent -Directory |
          Where-Object { $_.Name -match '^\d+\.\d+' } |
          Sort-Object { [version]$_.Name } -Descending |
          Select-Object -ExpandProperty FullName
    }
  }
}
foreach ($redistRoot in $redistRoots) {
  $crt = Join-Path $redistRoot "x64/Microsoft.VC143.CRT"
  if (-not (Test-Path -LiteralPath $crt)) { continue }
  foreach ($component in @("Microsoft.VC143.CRT", "Microsoft.VC143.OpenMP")) {
    $componentPath = Join-Path $redistRoot "x64/$component"
    if (Test-Path -LiteralPath $componentPath) {
      Get-ChildItem -LiteralPath $componentPath -Filter '*.dll' -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $outputRoot -Force
      }
    }
  }
  break
}

$requiredFiles = @(
  $packagedExe,
  (Join-Path $outputRoot "msvcp140.dll"),
  (Join-Path $outputRoot "vcruntime140.dll"),
  (Join-Path $outputRoot "vcruntime140_1.dll"),
  (Join-Path $outputRoot "vcomp140.dll"),
  (Join-Path $outputRoot "Qt6Core.dll"),
  (Join-Path $outputRoot "Qt6Gui.dll"),
  (Join-Path $outputRoot "Qt6OpenGL.dll"),
  (Join-Path $outputRoot "Qt6OpenGLWidgets.dll"),
  (Join-Path $outputRoot "Qt6Widgets.dll"),
  (Join-Path $outputRoot "platforms/qwindows.dll")
)
$missingFiles = $requiredFiles | Where-Object { -not (Test-Path -LiteralPath $_) }
if ($missingFiles) {
  throw "Windows package is incomplete: $($missingFiles -join ', ')"
}

Push-Location $outputRoot
$originalPath = $env:PATH
try {
  $env:PATH = "$outputRoot;$env:WINDIR\System32;$env:WINDIR"
  $smoke = Start-Process -FilePath $packagedExe -ArgumentList "--smoke-test" `
      -WorkingDirectory $outputRoot -WindowStyle Hidden -PassThru
  if (-not $smoke.WaitForExit(30000)) {
    $smoke.Kill()
    throw "Packaged executable smoke test timed out"
  }
  if ($smoke.ExitCode -ne 0) {
    throw "Packaged executable smoke test failed with exit code $($smoke.ExitCode)"
  }
} finally {
  $env:PATH = $originalPath
  Pop-Location
}

$getFileHash = Get-Command Get-FileHash -ErrorAction SilentlyContinue
if ($getFileHash) {
  $hash = (Get-FileHash -LiteralPath $packagedExe -Algorithm SHA256).Hash
} else {
  # Windows PowerShell normally provides Get-FileHash, but some stripped-down
  # deployment shells do not load Microsoft.PowerShell.Utility. Keep packaging
  # deterministic by falling back to the .NET SHA-256 implementation.
  $stream = [System.IO.File]::OpenRead($packagedExe)
  try {
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
      $hash = [System.BitConverter]::ToString($sha256.ComputeHash($stream)).Replace("-", "")
    } finally {
      $sha256.Dispose()
    }
  } finally {
    $stream.Dispose()
  }
}
Write-Host "Windows package ready: $outputRoot"
Write-Host "Executable SHA-256: $hash"
