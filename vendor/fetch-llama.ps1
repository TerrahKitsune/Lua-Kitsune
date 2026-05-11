<#
.SYNOPSIS
    fetch-llama.ps1 -- Downloads a GPU (CUDA) build of llama.cpp into llama\

.DESCRIPTION
    Downloads the full CUDA release ZIP from llama.cpp and copies ALL DLLs into
    llama\, replacing any previously installed CPU-only binaries.  Also downloads
    the matching cudart ZIP for the CUDA runtime DLLs (cublas, cudart, etc.).

    Run this once after cloning, or when upgrading the llama.cpp release.

.PARAMETER Release
    llama.cpp release tag (default: b9103).

.PARAMETER CudaVersion
    CUDA toolkit version.  "12.4" (default) for RTX 20/30/40, "13.1" for RTX 50xx.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File vendor\fetch-llama.ps1
    powershell -ExecutionPolicy Bypass -File vendor\fetch-llama.ps1 -CudaVersion 12.4
    powershell -ExecutionPolicy Bypass -File vendor\fetch-llama.ps1 -Release b9102 -CudaVersion 13.1
#>
param(
    [string]$Release     = "b9103",
    [string]$CudaVersion = "13.1"
)

$ErrorActionPreference = "Stop"

$baseUrl   = "https://github.com/ggml-org/llama.cpp/releases/download/$Release"
$mainZip   = "llama-$Release-bin-win-cuda-$CudaVersion-x64.zip"
$cudartZip = "cudart-llama-bin-win-cuda-$CudaVersion-x64.zip"

$repoRoot  = Split-Path -Parent $PSScriptRoot
$llamaDir  = Join-Path $repoRoot "llama"
$tempDir   = Join-Path $repoRoot "vendor\.tmp"

function Download-File([string]$url, [string]$dest) {
    $dir = Split-Path $dest
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    Write-Host "  GET $url" -ForegroundColor DarkGray
    $ProgressPreference = "SilentlyContinue"
    Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
    $ProgressPreference = "Continue"
}

if (-not (Test-Path $llamaDir)) {
    Write-Error "llama\ folder not found at $llamaDir. Make sure you are running from the repo root."
}

if (Test-Path $tempDir) { Remove-Item -Recurse -Force $tempDir }
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null

Write-Host ""
Write-Host "Downloading CUDA build for $Release (CUDA $CudaVersion) ..." -ForegroundColor White

$mainZipPath   = Join-Path $tempDir $mainZip
$cudartZipPath = Join-Path $tempDir $cudartZip
Download-File "$baseUrl/$mainZip"   $mainZipPath
Download-File "$baseUrl/$cudartZip" $cudartZipPath

Write-Host "  Extracting ..." -ForegroundColor Cyan
$mainExtract   = Join-Path $tempDir "main"
$cudartExtract = Join-Path $tempDir "cudart"
Expand-Archive -Path $mainZipPath   -DestinationPath $mainExtract   -Force
Expand-Archive -Path $cudartZipPath -DestinationPath $cudartExtract -Force

# Copy ALL DLLs from both ZIPs into llama\
# This ensures llama.dll, ggml.dll, ggml-base.dll etc. are the CUDA build variants,
# not a leftover CPU-only build.
$allDlls = @(Get-ChildItem $mainExtract   -Filter "*.dll" -Recurse) +
           @(Get-ChildItem $cudartExtract -Filter "*.dll" -Recurse)

$copied = 0
foreach ($d in $allDlls) {
    Copy-Item $d.FullName (Join-Path $llamaDir $d.Name) -Force
    Write-Host "  OK  $($d.Name)" -ForegroundColor Green
    $copied++
}

# Fetch llama.h from the tagged source on GitHub.
# The release ZIPs don't include headers, so we pull directly from the tag.
# NOTE: the packaged binaries are sometimes compiled from a commit slightly before
# the tag, meaning a handful of fields added in the last few commits of a release
# cycle may be in the header but not the DLL.  We detect that here and strip them.
Write-Host ""
Write-Host "Fetching llama.h for $Release ..." -ForegroundColor White
$headerUrl  = "https://raw.githubusercontent.com/ggml-org/llama.cpp/$Release/include/llama.h"
$headerDest = Join-Path $llamaDir "include\llama.h"
if (-not (Test-Path (Split-Path $headerDest))) {
    New-Item -ItemType Directory -Path (Split-Path $headerDest) -Force | Out-Null
}
$ProgressPreference = "SilentlyContinue"
Invoke-WebRequest -Uri $headerUrl -OutFile $headerDest -UseBasicParsing
$ProgressPreference = "Continue"

# Probe the DLL for fields that appear in the tag's header but may not be in the binary.
# If a field string is absent from the DLL we strip it from the struct so the layout matches.
$llamaDll  = Join-Path $llamaDir "llama.dll"
$dllBytes  = [System.IO.File]::ReadAllBytes($llamaDll)
function DllContains([string]$s) {
    $b = [System.Text.Encoding]::ASCII.GetBytes($s)
    $data = $dllBytes
    for ($i = 0; $i -le $data.Length - $b.Length; $i++) {
        $match = $true
        for ($j = 0; $j -lt $b.Length; $j++) {
            if ($data[$i+$j] -ne $b[$j]) { $match = $false; break }
        }
        if ($match) { return $true }
    }
    return $false
}

$header = Get-Content $headerDest -Raw

# Fields introduced after the binary was compiled but before the tag source was cut.
# Extend this list if future releases have the same issue.
$stripFields = @(
    @{ marker = "op_offload";  pattern = '\r?\n\s+bool\s+op_offload;[^\n]*' },
    @{ marker = "swa_full";    pattern = '\r?\n\s+bool\s+swa_full;.*?(?=\r?\n\s+bool\s+kv_unified)' },
    @{ marker = "n_samplers";  pattern = '\r?\n\s+struct\s+llama_sampler_seq_config[^\n]*\r?\n\s+size_t\s+n_samplers;[^\n]*' }
)

$patched = $false
foreach ($f in $stripFields) {
    if (-not (DllContains $f.marker)) {
        $header = [regex]::Replace($header, $f.pattern, '', [System.Text.RegularExpressions.RegexOptions]::Singleline)
        Write-Host "  patched: removed '$($f.marker)' (not in DLL)" -ForegroundColor Yellow
        $patched = $true
    }
}

if ($patched) {
    [System.IO.File]::WriteAllText($headerDest, $header)
    Write-Host "  llama.h patched to match DLL layout" -ForegroundColor Yellow
} else {
    Write-Host "  llama.h matches DLL (no patching needed)" -ForegroundColor Green
}

Remove-Item -Recurse -Force $tempDir

Write-Host ""
if ($copied -eq 0) {
    Write-Warning "No DLLs were found in the release ZIPs. Check the Release/CudaVersion parameters."
}
else {
    Write-Host "Done. $copied DLL(s) copied to llama\ (GPU/CUDA build)" -ForegroundColor Green
    Write-Host "IMPORTANT: Copy llama\*.dll alongside KitsuneEngine.dll at runtime." -ForegroundColor Yellow
}
