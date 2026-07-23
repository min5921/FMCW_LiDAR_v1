param(
    [string]$Destination = ""
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDirectory "..\.."))
$packageRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot "build\package"))

if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $packageRoot "FMCW_LiDAR_Jetson_Source"
}

$destinationPath = [System.IO.Path]::GetFullPath($Destination)
$packagePrefix = $packageRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) +
    [System.IO.Path]::DirectorySeparatorChar
if (-not $destinationPath.StartsWith($packagePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Destination must remain under $packageRoot"
}

if (Test-Path -LiteralPath $destinationPath) {
    Remove-Item -LiteralPath $destinationPath -Recurse -Force
}
New-Item -ItemType Directory -Path $destinationPath | Out-Null

$files = @(
    ".gitattributes",
    "CMakeLists.txt",
    "CMakePresets.json",
    "README.md"
)
$directories = @(
    "src",
    "tests",
    "config",
    "deploy\jetson"
)

foreach ($file in $files) {
    Copy-Item -LiteralPath (Join-Path $repositoryRoot $file) -Destination $destinationPath
}
foreach ($directory in $directories) {
    $source = Join-Path $repositoryRoot $directory
    $target = Join-Path $destinationPath $directory
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Recurse
}

$documentationTarget = Join-Path $destinationPath "docs"
New-Item -ItemType Directory -Path $documentationTarget | Out-Null
foreach ($document in @(
    "build_setup.md",
    "alazar_supported_models.md",
    "configuration.md",
    "data_contract.md",
    "device_protocols.md",
    "hardware_acceptance.md",
    "phase_status.md"
)) {
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "docs\$document") -Destination $documentationTarget
}

$revision = "uncommitted-source"
try {
    $revision = (git -C $repositoryRoot rev-parse HEAD).Trim()
    if (-not [string]::IsNullOrWhiteSpace((git -C $repositoryRoot status --porcelain))) {
        $revision = "$revision-dirty"
    }
} catch {
}
$utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText(
    (Join-Path $destinationPath "SOURCE_REVISION.txt"),
    "$revision`n",
    $utf8WithoutBom
)

$manifestLines = Get-ChildItem -LiteralPath $destinationPath -Recurse -File |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($destinationPath.Length + 1).Replace("\", "/")
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $relative"
    }
[System.IO.File]::WriteAllText(
    (Join-Path $destinationPath "SOURCE_MANIFEST.sha256"),
    ($manifestLines -join "`n") + "`n",
    $utf8WithoutBom
)

$zipPath = "$destinationPath.zip"
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -LiteralPath $destinationPath -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host "Jetson source folder: $destinationPath"
Write-Host "Jetson source ZIP:    $zipPath"
