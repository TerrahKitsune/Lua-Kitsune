<#
.SYNOPSIS
    fetch-llama.ps1 -- Downloads CUDA runtime DLLs for llama.cpp into llama\

.DESCRIPTION
    The committed llama\ folder already contains all small DLLs, import libs,
    and headers. This script only downloads the large CUDA-specific DLLs that
    are too big to commit to git:
        ggml-cuda.dll, cublas64_12.dll, cublasLt64_12.dll, cudart64_12.dll

    Run this once after cloning, or when switching CUDA version.

.PARAMETER Release
    llama.cpp release tag (default: b9037).

.PARAMETER CudaVersion
    CUDA toolkit version.  "12.4" (default) for RTX 20/30/40, "13.1" for RTX 50xx.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File vendor\fetch-llama.ps1
    powershell -ExecutionPolicy Bypass -File vendor\fetch-llama.ps1 -CudaVersion 13.1
    powershell -ExecutionPolicy Bypass -File vendor\fetch-llama.ps1 -Release b9037 -CudaVersion 12.4
#>
param(
    [string]$Release     = "b9037",
    [string]$CudaVersion = "12.4"
)

$ErrorActionPreference = "Stop"

$baseUrl   = "https://github.com/ggml-org/llama.cpp/releases/download/$Release"
$mainZip   = "llama-$Release-bin-win-cuda-$CudaVersion-x64.zip"
$cudartZip = "cudart-llama-bin-win-cuda-$CudaVersion-x64.zip"

$repoRoot  = Split-Path -Parent $PSScriptRoot
$llamaDir  = Join-Path $repoRoot "llama"
$tempDir   = Join-Path $repoRoot "vendor\.tmp"

# CUDA DLLs that are too large to commit to git
$cudaDlls = @("ggml-cuda.dll", "cublas64_12.dll", "cublasLt64_12.dll", "cudart64_12.dll")

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
Write-Host "Downloading CUDA DLLs for $Release (CUDA $CudaVersion) ..." -ForegroundColor White

$mainZipPath   = Join-Path $tempDir $mainZip
$cudartZipPath = Join-Path $tempDir $cudartZip
Download-File "$baseUrl/$mainZip"   $mainZipPath
Download-File "$baseUrl/$cudartZip" $cudartZipPath

Write-Host "  Extracting ..." -ForegroundColor Cyan
$mainExtract   = Join-Path $tempDir "main"
$cudartExtract = Join-Path $tempDir "cudart"
Expand-Archive -Path $mainZipPath   -DestinationPath $mainExtract   -Force
Expand-Archive -Path $cudartZipPath -DestinationPath $cudartExtract -Force

$allDlls = @(Get-ChildItem $mainExtract   -Filter "*.dll" -Recurse) +
           @(Get-ChildItem $cudartExtract -Filter "*.dll" -Recurse)

$copied = 0
foreach ($d in $allDlls) {
    if ($d.Name -in $cudaDlls) {
        Copy-Item $d.FullName (Join-Path $llamaDir $d.Name) -Force
        Write-Host "  OK  $($d.Name)" -ForegroundColor Green
        $copied++
    }
}

Remove-Item -Recurse -Force $tempDir

Write-Host ""
if ($copied -eq 0) {
    Write-Warning "No CUDA DLLs were found in the release ZIP. Check the Release/CudaVersion parameters."
}
else {
    Write-Host "Done. $copied CUDA DLL(s) copied to llama\" -ForegroundColor Green
    Write-Host "IMPORTANT: Copy llama\*.dll alongside KitsuneEngine.dll at runtime." -ForegroundColor Yellow
}
