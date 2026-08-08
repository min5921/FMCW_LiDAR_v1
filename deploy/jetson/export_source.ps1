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

# Copy-Item does not honor .gitignore. Keep the CubeIDE project sources, but
# remove local firmware build products and machine-specific indexer state.
$firmwareTarget = Join-Path $destinationPath "src\firmware"
if (Test-Path -LiteralPath $firmwareTarget) {
    Get-ChildItem -LiteralPath $firmwareTarget -Recurse -Directory |
        Where-Object { $_.Name -in @("Debug", "Release") } |
        Sort-Object { $_.FullName.Length } -Descending |
        Remove-Item -Recurse -Force
    Get-ChildItem -LiteralPath $firmwareTarget -Recurse -File |
        Where-Object { $_.Name -eq "language.settings.xml" } |
        Remove-Item -Force
}

$documentationTarget = Join-Path $destinationPath "docs"
New-Item -ItemType Directory -Path $documentationTarget | Out-Null
$documents = @(
    "build_setup.md",
    "alazar_supported_models.md",
    "configuration.md",
    "data_contract.md",
    "device_protocols.md",
    "hardware_acceptance.md",
    "phase_status.md"
)
foreach ($document in $documents) {
    Copy-Item -LiteralPath (Join-Path $repositoryRoot "docs\$document") -Destination $documentationTarget
}

$revision = "uncommitted-source"
try {
    $revision = (git -C $repositoryRoot rev-parse HEAD).Trim()
    $revisionPaths = @($files) + @($directories)
    $revisionPaths += $documents | ForEach-Object { "docs/$_" }
    $statusArguments = @(
        "-C", $repositoryRoot,
        "status", "--porcelain", "--untracked-files=normal", "--"
    ) + $revisionPaths
    $sourceStatus = @(& git @statusArguments)
    if ($LASTEXITCODE -ne 0) {
        throw "git status failed while calculating the source revision"
    }
    if ($sourceStatus.Count -gt 0) {
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

# Compress-Archive records Windows directory attributes that Linux unzip can
# interpret as mode 0664, leaving extracted directories without execute bits.
# Store file entries only so Linux unzip creates traversable parent directories.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::Open(
    $zipPath,
    [System.IO.Compression.ZipArchiveMode]::Create
)
try {
    Get-ChildItem -LiteralPath $destinationPath -Recurse -File |
        Sort-Object FullName |
        ForEach-Object {
            $relative = $_.FullName.Substring($packageRoot.Length + 1).Replace("\", "/")
            $null = [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $archive,
                $_.FullName,
                $relative,
                [System.IO.Compression.CompressionLevel]::Optimal
            )
        }
} finally {
    $archive.Dispose()
}

Write-Host "Jetson source folder: $destinationPath"
Write-Host "Jetson source ZIP:    $zipPath"
