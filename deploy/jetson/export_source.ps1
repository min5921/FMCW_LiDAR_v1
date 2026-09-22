param(
    [string]$Destination = "",
    [switch]$ReplaceExisting
)

$ErrorActionPreference = "Stop"

$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDirectory "..\.."))
$packageRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot "build\package"))

if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $packageRoot "Jetson-Basic\FMCW_LiDAR_Basic_Jetson_Source"
}

$destinationPath = [System.IO.Path]::GetFullPath($Destination)
$packagePrefix = $packageRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) +
    [System.IO.Path]::DirectorySeparatorChar
if (-not $destinationPath.StartsWith($packagePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Destination must remain under $packageRoot"
}

$zipPath = "$destinationPath.zip"
if ((Test-Path -LiteralPath $destinationPath) -or (Test-Path -LiteralPath $zipPath)) {
    if (-not $ReplaceExisting) {
        throw "Source folder or ZIP exists. Choose a new destination or use -ReplaceExisting to archive it."
    }
    $archiveRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot "build\package_archive"))
    $archivePath = [System.IO.Path]::GetFullPath((Join-Path $archiveRoot (
        "{0}\Jetson-Basic-Source-{1}" -f (Get-Date -Format 'yyyy-MM-dd'), (Get-Date -Format 'HHmmssfff'))))
    if (-not $archivePath.StartsWith($archiveRoot + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Archive must remain under $archiveRoot"
    }
    if (Test-Path -LiteralPath $archivePath) { throw "Archive destination already exists: $archivePath" }
    foreach ($oldPath in @($destinationPath, $zipPath)) {
        if (Test-Path -LiteralPath $oldPath) {
            $oldItems = @((Get-Item -LiteralPath $oldPath -Force))
            if (Test-Path -LiteralPath $oldPath -PathType Container) {
                $oldItems += @(Get-ChildItem -LiteralPath $oldPath -Recurse -Force)
            }
            if (@($oldItems | Where-Object { $_.Attributes -band [System.IO.FileAttributes]::ReparsePoint }).Count) {
                throw "Cannot archive reparse points: $oldPath"
            }
        }
    }
    New-Item -ItemType Directory -Path $archivePath | Out-Null
    foreach ($oldPath in @($destinationPath, $zipPath)) {
        if (Test-Path -LiteralPath $oldPath) {
            Move-Item -LiteralPath $oldPath -Destination $archivePath
        }
    }
    Write-Host "Previous source bundle preserved: $archivePath"
}
New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force | Out-Null
New-Item -ItemType Directory -Path $destinationPath | Out-Null

$files = @(
    ".gitattributes",
    "CMakeLists.txt",
    "CMakePresets.json",
    "WORKSPACE.md"
)
$directories = @(
    "src",
    "tests",
    "config",
    "docs",
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
Copy-Item -LiteralPath (Join-Path $repositoryRoot "deploy\build_manifest.cmake") -Destination (Join-Path $destinationPath "deploy")

# Copy-Item does not honor .gitignore. Keep the CubeIDE project sources, but
# remove local firmware build products and machine-specific indexer state.
$firmwareTarget = Join-Path $destinationPath "src\firmware"
if (Test-Path -LiteralPath $firmwareTarget) {
    Get-ChildItem -LiteralPath $firmwareTarget -Recurse -Directory |
        Where-Object { $_.Name -in @("Debug", "Release") } |
        Sort-Object { $_.FullName.Length } -Descending |
        ForEach-Object {
            $cleanupPath = [System.IO.Path]::GetFullPath($_.FullName)
            if (-not $cleanupPath.StartsWith($destinationPath + [System.IO.Path]::DirectorySeparatorChar,
                    [System.StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe firmware cleanup path: $cleanupPath" }
            if (((Get-Item -LiteralPath $cleanupPath -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint) -or
                @(Get-ChildItem -LiteralPath $cleanupPath -Recurse -Force |
                    Where-Object { $_.Attributes -band [System.IO.FileAttributes]::ReparsePoint }).Count) {
                throw "Firmware output contains a reparse point: $cleanupPath"
            }
            Remove-Item -LiteralPath $cleanupPath -Recurse -Force
        }
    Get-ChildItem -LiteralPath $firmwareTarget -Recurse -File |
        Where-Object { $_.Name -eq "language.settings.xml" } |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
}

# Tests and documentation tools may have local Python bytecode caches.
Get-ChildItem -LiteralPath $destinationPath -Directory -Recurse -Force |
    Where-Object { $_.Name -eq "__pycache__" } |
    Sort-Object { $_.FullName.Length } -Descending |
    ForEach-Object {
        $cleanupPath = [System.IO.Path]::GetFullPath($_.FullName)
        if (-not $cleanupPath.StartsWith($destinationPath + [System.IO.Path]::DirectorySeparatorChar,
                [System.StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe cache cleanup path: $cleanupPath" }
        $cacheItems = @((Get-Item -LiteralPath $cleanupPath -Force)) + @(Get-ChildItem -LiteralPath $cleanupPath -Recurse -Force)
        if (@($cacheItems | Where-Object { $_.Attributes -band [System.IO.FileAttributes]::ReparsePoint }).Count) {
            throw "Python cache contains a reparse point: $cleanupPath"
        }
        Remove-Item -LiteralPath $cleanupPath -Recurse -Force
    }

$utf8WithoutBom = [System.Text.UTF8Encoding]::new($false)
$bundleReadme = @'
# FMCW LiDAR Basic for Jetson

This is a source bundle for a native Jetson ARM64 build, not a Windows executable.
It includes acquisition, MCU/EDFA control, CUDA/cuFFT, live 3D, storage and RAW replay.
PCD file playback, model weights, CenterPoint inference and cuDNN are excluded from the application.

1. Read [the Jetson deployment guide](deploy/jetson/README_KO.md).
2. Edit `deploy/jetson/jetson.env` for the Jetson's Alazar SDK, CUDA architecture and Qt installation.
3. From this source folder on the Jetson, run:

```bash
sha256sum -c SOURCE_MANIFEST.sha256
bash deploy/jetson/build.sh
bash build/package/Jetson-Basic/run.sh
```

Verify the source manifest before editing the environment file.
Jetson signal processing uses CUDA/cuFFT; FFTW is disabled.
Native ARM64 compilation and hardware operation must be verified on the target Jetson.
`SOURCE_REVISION.txt` and `SOURCE_MANIFEST.sha256` identify the exported working sources.
Older Windows-oriented workspace documents are reference material; use the Jetson guide above to build this bundle.

Documentation: [document index](docs/README.md), [source guide](docs/source/source_guide_ko.md),
[per-file source index](docs/source/source_file_index_ko.md).
'@
[System.IO.File]::WriteAllText((Join-Path $destinationPath "README.md"), $bundleReadme + "`n", $utf8WithoutBom)

$revision = "uncommitted-source"
try {
    $revision = (git -C $repositoryRoot rev-parse HEAD).Trim()
    $revisionPaths = @($files) + @($directories)
    $revisionPaths += "deploy/build_manifest.cmake"
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
[System.IO.File]::WriteAllText(
    (Join-Path $destinationPath "SOURCE_REVISION.txt"),
    "$revision`n",
    $utf8WithoutBom
)

$manifestLines = Get-ChildItem -LiteralPath $destinationPath -Recurse -File -Force |
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

if (Test-Path -LiteralPath $zipPath) {
    throw "ZIP appeared during export; refusing to overwrite: $zipPath"
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
    Get-ChildItem -LiteralPath $destinationPath -Recurse -File -Force |
        Sort-Object FullName |
        ForEach-Object {
            $relative = $_.FullName.Substring((Split-Path -Parent $destinationPath).Length + 1).Replace("\", "/")
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
