#Requires -Version 5.1
<#
.SYNOPSIS
    Downloads the matching llama.cpp Windows CUDA release (DLLs, import libs,
    and headers) and installs them into the repo.

.DESCRIPTION
    1. Queries the GitHub Releases API for ggml-org/llama.cpp.
    2. Finds the CUDA 12 / Windows asset (llama-<tag>-bin-win-cuda-12-x64.zip).
    3. Extracts DLLs    -> x64\Debug\
                libs    -> llama\lib\
                headers -> llama\include\

    Run this script whenever you update the llama.cpp binaries so that the
    headers and import libs are always in sync with the DLLs.

.PARAMETER Tag
    Specific release tag to fetch (e.g. "b5614"). Defaults to the latest release.

.PARAMETER Force
    Re-download and overwrite even if the tag has not changed.
#>
param(
    [string] $Tag   = '',
    [switch] $Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# -- Helpers ------------------------------------------------------------------
function Write-Header  { param([string]$m) Write-Host "`n=== $m ===" -ForegroundColor Cyan   }
function Write-Success { param([string]$m) Write-Host $m               -ForegroundColor Green  }
function Write-Info    { param([string]$m) Write-Host $m               -ForegroundColor Yellow }
function Write-Failure { param([string]$m) Write-Host $m               -ForegroundColor Red    }

# -- Paths --------------------------------------------------------------------
$repoRoot   = Split-Path -Parent $PSCommandPath
$binDir     = Join-Path $repoRoot 'x64\Debug'
$libDir     = Join-Path $repoRoot 'llama\lib'
$includeDir = Join-Path $repoRoot 'llama\include'
$tmpDir     = Join-Path $repoRoot '.llama-fetch-tmp'
$tagFile    = Join-Path $repoRoot '.llama-fetch-tag'

# -- Resolve release ----------------------------------------------------------
Write-Header 'Resolving llama.cpp release'

$apiBase = 'https://api.github.com/repos/ggml-org/llama.cpp/releases'
$headers = @{ 'User-Agent' = 'Lua-Kitsune-fetch-script' }

if ($Tag -eq '') {
    Write-Info 'Searching for latest release with full Windows binaries...'
    $releases = Invoke-RestMethod -Uri "$apiBase`?per_page=30" -Headers $headers
    $release = $releases | Where-Object {
        $_.assets | Where-Object { $_.name -like 'llama-*-bin-win-cuda-*-x64.zip' }
    } | Select-Object -First 1
    if (-not $release) {
        Write-Failure 'ERROR: Could not find a recent release with Windows CUDA binaries.'
        exit 1
    }
}
else {
    Write-Info "Fetching release tag: $Tag"
    $release = Invoke-RestMethod -Uri "$apiBase/tags/$Tag" -Headers $headers
}

$resolvedTag = $release.tag_name
Write-Success "Release: $resolvedTag"

# Skip if already up-to-date
if (!$Force -and (Test-Path $tagFile) -and ((Get-Content $tagFile -Raw).Trim() -eq $resolvedTag)) {
    Write-Success "Already at $resolvedTag -- nothing to do. Use -Force to re-install."
    exit 0
}

# -- Find asset ---------------------------------------------------------------
Write-Header 'Locating Windows x64 CUDA asset'

# Full llama Windows zips are named: llama-<tag>-bin-win-cuda-<ver>-x64.zip
$winAssets = $release.assets | Where-Object { $_.name -like 'llama-*-bin-win-cuda-*-x64.zip' }

# Pick the highest CUDA version (sort alphabetically, last = highest version string)
$asset = $winAssets | Sort-Object { $_.name } | Select-Object -Last 1

if (-not $asset) {
    Write-Failure "ERROR: Could not find a Windows CUDA x64 asset in release $resolvedTag."
    Write-Info 'Available assets:'
    $release.assets | ForEach-Object { Write-Info "  $($_.name)" }
    exit 1
}

Write-Success "Asset: $($asset.name)  ($([math]::Round($asset.size/1MB,1)) MB)"

# -- Download -----------------------------------------------------------------
Write-Header 'Downloading'

$null = New-Item -ItemType Directory -Force -Path $tmpDir
$zipPath = Join-Path $tmpDir $asset.name

Write-Info "URL: $($asset.browser_download_url)"
Write-Info "Dest: $zipPath"
Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zipPath -UseBasicParsing
Write-Success 'Download complete.'

# -- Extract ------------------------------------------------------------------
Write-Header 'Extracting'

$extractDir = Join-Path $tmpDir 'extracted'
if (Test-Path $extractDir) { Remove-Item $extractDir -Recurse -Force }
Expand-Archive -Path $zipPath -DestinationPath $extractDir

# The zip may place files in a subdirectory; find the actual content root.
$contentRoot = $extractDir
$subdirs = @(Get-ChildItem $extractDir -Directory)
$files   = @(Get-ChildItem $extractDir -File)
if ($subdirs.Count -eq 1 -and $files.Count -eq 0) {
    $contentRoot = $subdirs[0].FullName
}

Write-Info "Content root: $contentRoot"
Get-ChildItem $contentRoot | ForEach-Object { Write-Info "  $($_.Name)" }

# -- Install DLLs -------------------------------------------------------------
Write-Header "Installing DLLs -> $binDir"

$null = New-Item -ItemType Directory -Force -Path $binDir
$dlls = @(Get-ChildItem $contentRoot -Filter '*.dll' -Recurse)
if ($dlls.Count -eq 0) {
    Write-Failure 'ERROR: No DLLs found in the extracted archive.'
    exit 1
}
foreach ($dll in $dlls) {
    Copy-Item $dll.FullName -Destination $binDir -Force
    Write-Info "  $($dll.Name)"
}
Write-Success "$($dlls.Count) DLL(s) installed."

# -- Install import libs ------------------------------------------------------
Write-Header "Installing import libs -> $libDir"

$null = New-Item -ItemType Directory -Force -Path $libDir
$libs = @(Get-ChildItem $contentRoot -Filter '*.lib' -Recurse)
foreach ($lib in $libs) {
    Copy-Item $lib.FullName -Destination $libDir -Force
    Write-Info "  $($lib.Name)"
}
Write-Success "$($libs.Count) lib(s) installed."

# -- Install headers ----------------------------------------------------------
Write-Header "Installing headers -> $includeDir"

$null = New-Item -ItemType Directory -Force -Path $includeDir

# Headers may be under include/ or directly in the content root
$headerSources = @(
    (Join-Path $contentRoot 'include'),
    $contentRoot
)
$copiedHeaders = 0
foreach ($src in $headerSources) {
    if (Test-Path $src) {
        $hdrs = Get-ChildItem $src -Filter '*.h' -File
        foreach ($h in $hdrs) {
            Copy-Item $h.FullName -Destination $includeDir -Force
            Write-Info "  $($h.Name)"
            $copiedHeaders++
        }
    }
}
Write-Success "$copiedHeaders header(s) installed."

# -- Record installed tag -----------------------------------------------------
$resolvedTag | Set-Content $tagFile -NoNewline
Write-Success "Tag file written: $tagFile"

# -- Cleanup ------------------------------------------------------------------
Remove-Item $tmpDir -Recurse -Force
Write-Success 'Temp files cleaned up.'

Write-Header 'Done'
Write-Success "llama.cpp $resolvedTag installed successfully."
Write-Info 'Rebuild KitsuneEngine.dll to pick up the updated headers and import libs.'
