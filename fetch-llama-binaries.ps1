#Requires -Version 5.1
<#
.SYNOPSIS
    Downloads the matching llama.cpp Windows CUDA release, CUDA runtime DLLs,
    generates import libs, and fetches headers from source.

.DESCRIPTION
    1. Queries the GitHub Releases API for ggml-org/llama.cpp.
    2. Downloads llama-<tag>-bin-win-cuda-<ver>-x64.zip  -> DLLs  -> llama\
    3. Downloads cudart-llama-bin-win-cuda-<ver>-x64.zip -> CUDA runtime DLLs -> llama\
    4. Generates llama.lib and ggml.lib from the DLLs using MSVC lib.exe.
    5. Downloads headers straight from the llama.cpp source at the matching tag.

    The binary zips no longer ship .lib or .h files, so steps 4-5 replace the
    old copy-from-zip approach.  Run this script and then rebuild KitsuneEngine.

.PARAMETER Tag
    Specific release tag to fetch (e.g. "b9611"). Defaults to the latest release.

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
$binDir     = Join-Path $repoRoot 'llama'
$libDir     = Join-Path $repoRoot 'llama\lib'
$includeDir = Join-Path $repoRoot 'llama\include'
$ggmlSubDir = Join-Path $includeDir 'ggml'
$tmpDir     = Join-Path $env:TEMP 'llama-fetch-tmp'
$tagFile    = Join-Path $repoRoot '.llama-fetch-tag'

# -- Find MSVC tools ----------------------------------------------------------
Write-Header 'Locating MSVC tools'

function Find-MsvcTool {
    param([string]$name)
    $candidates = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio' -Recurse -Filter $name -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $candidates) {
        Write-Failure "ERROR: Could not find $name. Make sure Visual Studio with C++ tools is installed."
        exit 1
    }
    Write-Info "  $name -> $($candidates.FullName)"
    return $candidates.FullName
}

$tool_lib     = Find-MsvcTool 'lib.exe'
$tool_dumpbin = Find-MsvcTool 'dumpbin.exe'

# -- Resolve release ----------------------------------------------------------
Write-Header 'Resolving llama.cpp release'

$apiBase    = 'https://api.github.com/repos/ggml-org/llama.cpp/releases'
$ghHeaders  = @{ 'User-Agent' = 'Lua-Kitsune-fetch-script' }

if ($Tag -eq '') {
    Write-Info 'Searching for latest release with Windows CUDA binaries...'
    $releases = Invoke-RestMethod -Uri "$apiBase`?per_page=30" -Headers $ghHeaders
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
    $release = Invoke-RestMethod -Uri "$apiBase/tags/$Tag" -Headers $ghHeaders
}

$resolvedTag = $release.tag_name
Write-Success "Release: $resolvedTag"

# Skip if already up-to-date
if (!$Force -and (Test-Path $tagFile) -and ((Get-Content $tagFile -Raw).Trim() -eq $resolvedTag)) {
    Write-Success "Already at $resolvedTag -- nothing to do. Use -Force to re-install."
    exit 0
}

# -- Find assets --------------------------------------------------------------
Write-Header 'Locating Windows x64 CUDA assets'

# Main llama DLL zip: llama-<tag>-bin-win-cuda-<ver>-x64.zip
$llamaAssets = $release.assets | Where-Object { $_.name -like 'llama-*-bin-win-cuda-*-x64.zip' }
$llamaAsset  = $llamaAssets | Sort-Object { $_.name } | Select-Object -Last 1

if (-not $llamaAsset) {
    Write-Failure "ERROR: No Windows CUDA asset found in release $resolvedTag."
    $release.assets | ForEach-Object { Write-Info "  $($_.name)" }
    exit 1
}
Write-Success "llama DLL asset : $($llamaAsset.name)  ($([math]::Round($llamaAsset.size/1MB,1)) MB)"

# CUDA runtime zip: cudart-llama-bin-win-cuda-<ver>-x64.zip
$cudartAssets = $release.assets | Where-Object { $_.name -like 'cudart-llama-bin-win-cuda-*-x64.zip' }

# Match the same CUDA version as the llama asset
$cudaVer = if ($llamaAsset.name -match 'cuda-([\d.]+)-x64') { $Matches[1] } else { '' }
$cudartAsset = $null
if ($cudaVer) {
    $cudartAsset = $cudartAssets | Where-Object { $_.name -like "*-cuda-$cudaVer-*" } | Select-Object -First 1
}
if (-not $cudartAsset) {
    # Fall back to highest version
    $cudartAsset = $cudartAssets | Sort-Object { $_.name } | Select-Object -Last 1
}

if (-not $cudartAsset) {
    Write-Info 'WARNING: No cudart asset found. CUDA GPU acceleration will not be available.'
} else {
    Write-Success "cudart asset    : $($cudartAsset.name)  ($([math]::Round($cudartAsset.size/1MB,1)) MB)"
}

# -- Clean old install --------------------------------------------------------
Write-Header 'Cleaning previous install'

if (Test-Path $binDir) {
    Get-ChildItem $binDir -Filter '*.dll' -File | Remove-Item -Force
    Write-Info '  Removed old DLLs from llama\'
}
if (Test-Path $libDir) {
    Remove-Item $libDir -Recurse -Force
    Write-Info '  Removed old llama\lib\'
}
if (Test-Path $includeDir) {
    Remove-Item $includeDir -Recurse -Force
    Write-Info '  Removed old llama\include\'
}
Write-Success 'Clean complete.'

# -- Download helper ----------------------------------------------------------
function Download-And-Extract {
    param([string]$Url, [string]$ZipPath, [string]$ExtractPath)
    Write-Info "URL : $Url"
    Write-Info "Dest: $ZipPath"
    Invoke-WebRequest -Uri $Url -OutFile $ZipPath -UseBasicParsing
    Write-Success 'Download complete.'
    if (Test-Path $ExtractPath) { Remove-Item $ExtractPath -Recurse -Force }
    Expand-Archive -Path $ZipPath -DestinationPath $ExtractPath
    # Unwrap single top-level subdirectory if present
    $subdirs = @(Get-ChildItem $ExtractPath -Directory)
    $files   = @(Get-ChildItem $ExtractPath -File)
    if ($subdirs.Count -eq 1 -and $files.Count -eq 0) {
        return $subdirs[0].FullName
    }
    return $ExtractPath
}

$null = New-Item -ItemType Directory -Force -Path $tmpDir
$null = New-Item -ItemType Directory -Force -Path $binDir

# -- Download and install llama DLLs ------------------------------------------
Write-Header 'Downloading llama DLLs'

$llamaZip     = Join-Path $tmpDir $llamaAsset.name
$llamaExtract = Join-Path $tmpDir 'llama-extracted'
$llamaRoot    = Download-And-Extract -Url $llamaAsset.browser_download_url -ZipPath $llamaZip -ExtractPath $llamaExtract

Write-Header "Installing llama DLLs -> $binDir"
$dlls = @(Get-ChildItem $llamaRoot -Filter '*.dll' -Recurse)
if ($dlls.Count -eq 0) {
    Write-Failure 'ERROR: No DLLs found in the llama archive.'
    exit 1
}
foreach ($dll in $dlls) {
    Copy-Item $dll.FullName -Destination $binDir -Force
    Write-Info "  $($dll.Name)"
}
Write-Success "$($dlls.Count) llama DLL(s) installed."

# -- Download and install CUDA runtime DLLs -----------------------------------
if ($cudartAsset) {
    Write-Header 'Downloading CUDA runtime DLLs'
    $cudartZip     = Join-Path $tmpDir $cudartAsset.name
    $cudartExtract = Join-Path $tmpDir 'cudart-extracted'
    $cudartRoot    = Download-And-Extract -Url $cudartAsset.browser_download_url -ZipPath $cudartZip -ExtractPath $cudartExtract

    Write-Header "Installing CUDA runtime DLLs -> $binDir"
    $cudaDlls = @(Get-ChildItem $cudartRoot -Filter '*.dll' -Recurse)
    foreach ($dll in $cudaDlls) {
        Copy-Item $dll.FullName -Destination $binDir -Force
        Write-Info "  $($dll.Name)"
    }
    Write-Success "$($cudaDlls.Count) CUDA runtime DLL(s) installed."
}

# -- Generate import libs from DLLs -------------------------------------------
Write-Header "Generating import libs -> $libDir"

$null = New-Item -ItemType Directory -Force -Path $libDir

function New-ImportLib {
    param([string]$DllName)
    $dllPath = Join-Path $binDir $DllName
    if (-not (Test-Path $dllPath)) {
        Write-Info "  SKIP: $DllName not found in $binDir"
        return
    }
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($DllName)
    $defPath  = Join-Path $tmpDir "$baseName.def"
    $libPath  = Join-Path $libDir "$baseName.lib"

    # Collect exported function names (skip ordinal-only / forwarded entries)
    $exportLines = & $tool_dumpbin /exports $dllPath 2>&1 |
        Where-Object { $_ -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)' }

    if (-not $exportLines) {
        Write-Info "  SKIP: no named exports found in $DllName"
        return
    }

    $def = "LIBRARY `"$DllName`"`r`nEXPORTS`r`n"
    foreach ($line in $exportLines) {
        if ($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)') {
            $def += "    $($Matches[1])`r`n"
        }
    }
    $def | Set-Content -Path $defPath -Encoding ASCII

    & $tool_lib /nologo /def:$defPath /machine:x64 /out:$libPath 2>&1 | Out-Null
    Remove-Item $defPath -ErrorAction SilentlyContinue

    if (Test-Path $libPath) {
        Write-Info "  $baseName.lib"
    } else {
        Write-Failure "  ERROR: Failed to generate $baseName.lib"
    }
}

New-ImportLib 'llama.dll'
New-ImportLib 'ggml.dll'
Write-Success 'Import libs generated.'

# -- Fetch headers from source ------------------------------------------------
Write-Header "Fetching headers from github.com/ggml-org/llama.cpp @ $resolvedTag"

$null = New-Item -ItemType Directory -Force -Path $includeDir
$null = New-Item -ItemType Directory -Force -Path $ggmlSubDir

# Map: source path in repo -> local destinations (list, because some headers
# are placed in two spots so both include paths in the vcxproj work)
$headerMap = [ordered]@{
    'include/llama.h'          = @('llama.h')
    'include/llama-cpp.h'      = @('llama-cpp.h')
    'ggml/include/ggml.h'      = @('ggml.h',        'ggml\ggml.h')
    'ggml/include/ggml-alloc.h'= @('ggml-alloc.h',  'ggml\ggml-alloc.h')
    'ggml/include/ggml-backend.h'=@('ggml-backend.h','ggml\ggml-backend.h')
    'ggml/include/ggml-cpp.h'  = @('ggml-cpp.h',    'ggml\ggml-cpp.h')
    'ggml/include/ggml-cpu.h'  = @('ggml-cpu.h',    'ggml\ggml-cpu.h')
    'ggml/include/ggml-cuda.h' = @('ggml-cuda.h',   'ggml\ggml-cuda.h')
    'ggml/include/ggml-opt.h'  = @('ggml-opt.h',    'ggml\ggml-opt.h')
    'ggml/include/gguf.h'      = @('gguf.h',         'ggml\gguf.h')
}

$rawBase = "https://raw.githubusercontent.com/ggml-org/llama.cpp/$resolvedTag"
$fetchedHeaders = 0

$tmpHeader = Join-Path $tmpDir 'tmp_header.h'

foreach ($srcPath in $headerMap.Keys) {
    $url   = "$rawBase/$srcPath"
    $dests = $headerMap[$srcPath]
    try {
        # Download directly to a temp file (avoids string/byte[] conversion issues)
        Invoke-WebRequest -Uri $url -OutFile $tmpHeader -UseBasicParsing -ErrorAction Stop
        foreach ($dest in $dests) {
            $destPath = Join-Path $includeDir $dest
            $destDir  = Split-Path $destPath -Parent
            if (-not (Test-Path $destDir)) { $null = New-Item -ItemType Directory -Force -Path $destDir }
            Copy-Item $tmpHeader -Destination $destPath -Force
            Write-Info "  $dest"
            $fetchedHeaders++
        }
        Remove-Item $tmpHeader -ErrorAction SilentlyContinue
    }
    catch {
        Write-Failure "  ERROR fetching $srcPath : $_"
    }
}

if ($fetchedHeaders -eq 0) {
    Write-Failure 'ERROR: No headers were downloaded. Check your internet connection.'
    exit 1
}
Write-Success "$fetchedHeaders header destination(s) written."

# -- Record installed tag -----------------------------------------------------
$resolvedTag | Set-Content $tagFile -NoNewline
Write-Success "Tag file written: $tagFile  ($resolvedTag)"

# -- Cleanup ------------------------------------------------------------------
Remove-Item $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
Write-Success 'Temp files cleaned up.'

Write-Header 'Done'
Write-Success "llama.cpp $resolvedTag installed successfully."
Write-Host ''
Write-Info 'Next steps:'
Write-Info '  1. Rebuild KitsuneEngine (Release + Debug) in Visual Studio.'
Write-Info '  2. The CUDA runtime DLLs are now in llama\ and will be copied by the PostBuildEvent.'
Write-Info '  3. GPU offloading should now work (n_gpu_layers > 0 in Lua settings).'
