param(
  [Parameter(Mandatory = $true)]
  [string]$CenterPointSource,
  [Parameter(Mandatory = $true)]
  [string]$WeightsRoot,
  [Parameter(Mandatory = $true)]
  [string]$CudnnRoot,
  [string]$SamplePoints = $env:CENTERPOINT_SAMPLE_POINTS,
  [string]$QtRoot = $(if ($env:QT_ROOT) { $env:QT_ROOT } else { "C:\Qt\6.11.0\msvc2022_64" }),
  [string]$BuildDirectory = "build/preset-windows-centerpoint-release"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$sourceRoot = (Resolve-Path -LiteralPath $CenterPointSource).Path
$weightsPath = (Resolve-Path -LiteralPath $WeightsRoot).Path
$cudnnPath = (Resolve-Path -LiteralPath $CudnnRoot).Path
$qtPath = (Resolve-Path -LiteralPath $QtRoot).Path
$buildPath = Join-Path $repoRoot $BuildDirectory
$samplePath = $null
if ($SamplePoints) {
  $samplePath = (Resolve-Path -LiteralPath $SamplePoints).Path
}

$runtimeRoot = $sourceRoot
if (Test-Path -LiteralPath (Join-Path $sourceRoot "20_active_count_nms_project")) {
  $runtimeRoot = Join-Path $sourceRoot "20_active_count_nms_project"
}

$requiredFiles = @(
  (Join-Path $runtimeRoot "include/centerpoint/gpu_preprocess.hpp"),
  (Join-Path $runtimeRoot "cuda/gpu_preprocess.cu"),
  (Join-Path $weightsPath "04_pfn/weights_metadata.json"),
  (Join-Path $weightsPath "06_rpn/rpn_weights_metadata.json"),
  (Join-Path $weightsPath "07_head/head_weights_metadata.json")
)
$missingFiles = $requiredFiles | Where-Object { -not (Test-Path -LiteralPath $_) }
if ($missingFiles) {
  throw "CenterPoint source or weights are incomplete: $($missingFiles -join ', ')"
}

$cudnnHeader = Get-ChildItem -LiteralPath $cudnnPath -Recurse -Filter cudnn.h |
    Select-Object -First 1
$cudnnLibrary = Get-ChildItem -LiteralPath $cudnnPath -Recurse -Filter cudnn.lib |
    Select-Object -First 1
$cudnnRuntime = Get-ChildItem -LiteralPath $cudnnPath -Recurse -Filter cudnn64_9.dll |
    Select-Object -First 1
if (-not $cudnnHeader -or -not $cudnnRuntime) {
  throw "cuDNN files cudnn.h and cudnn64_9.dll are required under $cudnnPath"
}

$env:FMCW_CENTERPOINT_WEIGHTS_ROOT = $weightsPath
$env:FMCW_CENTERPOINT_TEST_WEIGHTS_ROOT = $weightsPath
if ($samplePath) {
  $env:FMCW_CENTERPOINT_TEST_POINTS_BIN = $samplePath
} else {
  Remove-Item Env:FMCW_CENTERPOINT_TEST_POINTS_BIN -ErrorAction SilentlyContinue
}
$cmakeArguments = @(
  "-S", $repoRoot,
  "-B", $buildPath,
  "-G", "NMake Makefiles",
  "-DCMAKE_BUILD_TYPE=Release",
  "-DFMCW_TARGET_PLATFORM=WINDOWS",
  "-DFMCW_BUILD_QT_APPS=ON",
  "-DFMCW_BUILD_TESTS=ON",
  "-DFMCW_WITH_CUDA=ON",
  "-DFMCW_REQUIRE_CUDA=ON",
  "-DFMCW_WITH_CENTERPOINT=ON",
  "-DFMCW_CENTERPOINT_SOURCE_DIR=$sourceRoot",
  "-DFMCW_CUDNN_ROOT=$cudnnPath",
  "-DCMAKE_PREFIX_PATH=$qtPath",
  "-DFMCW_CUDNN_INCLUDE_DIR=$($cudnnHeader.DirectoryName)",
  "-DFMCW_CUDNN_RUNTIME_DLL=$($cudnnRuntime.FullName)"
)
if ($cudnnLibrary) {
  $cmakeArguments += "-DFMCW_CUDNN_LIBRARY=$($cudnnLibrary.FullName)"
}
& cmake @cmakeArguments
if ($LASTEXITCODE -ne 0) {
  throw "CenterPoint CMake configure failed with exit code $LASTEXITCODE"
}

& cmake --build $buildPath
if ($LASTEXITCODE -ne 0) {
  throw "CenterPoint build failed with exit code $LASTEXITCODE"
}

& ctest --test-dir $buildPath --output-on-failure
if ($LASTEXITCODE -ne 0) {
  throw "CenterPoint tests failed with exit code $LASTEXITCODE"
}

Write-Host "Windows CenterPoint build completed: $buildPath"
Write-Host "Set FMCW_CENTERPOINT_WEIGHTS_ROOT before launching the application:"
Write-Host ('$env:FMCW_CENTERPOINT_WEIGHTS_ROOT = "{0}"' -f $weightsPath)
