<#
.SYNOPSIS
    fetch-llama.ps1 -- Downloads prebuilt llama.cpp binaries into vendor\llama.cpp\

.DESCRIPTION
    1. Downloads the prebuilt DLLs from the GitHub release
    2. Downloads matching public headers from the source tree at the same tag
    3. Generates import .lib files from the DLLs using MSVC lib.exe + dumpbin.exe

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
$rawUrl    = "https://raw.githubusercontent.com/ggerganov/llama.cpp/$Release"
$mainZip   = "llama-$Release-bin-win-cuda-$CudaVersion-x64.zip"
$cudartZip = "cudart-llama-bin-win-cuda-$CudaVersion-x64.zip"

$repoRoot  = Split-Path -Parent $PSScriptRoot
$vendorDir = Join-Path $repoRoot "vendor\llama.cpp"
$tempDir   = Join-Path $repoRoot "vendor\.tmp"

function Download-File([string]$url, [string]$dest) {
    $dir = Split-Path $dest
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    Write-Host "  GET $url" -ForegroundColor DarkGray
    $ProgressPreference = "SilentlyContinue"
    Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
    $ProgressPreference = "Continue"
}

# Clean previous
if (Test-Path $vendorDir) { Remove-Item -Recurse -Force $vendorDir }
if (Test-Path $tempDir)   { Remove-Item -Recurse -Force $tempDir }
New-Item -ItemType Directory -Path "$vendorDir\include\ggml" -Force | Out-Null
New-Item -ItemType Directory -Path "$vendorDir\lib"          -Force | Out-Null
New-Item -ItemType Directory -Path "$vendorDir\bin"          -Force | Out-Null
New-Item -ItemType Directory -Path $tempDir                  -Force | Out-Null

# Step 1: Download and extract DLLs
Write-Host ""
Write-Host "Step 1: Downloading prebuilt DLLs for $Release (CUDA $CudaVersion) ..." -ForegroundColor White
$mainZipPath   = Join-Path $tempDir $mainZip
$cudartZipPath = Join-Path $tempDir $cudartZip
Download-File "$baseUrl/$mainZip"   $mainZipPath
Download-File "$baseUrl/$cudartZip" $cudartZipPath

Write-Host "  Extracting $mainZip ..." -ForegroundColor Cyan
$mainExtract = Join-Path $tempDir "main"
Expand-Archive -Path $mainZipPath -DestinationPath $mainExtract -Force

Write-Host "  Extracting $cudartZip ..." -ForegroundColor Cyan
$cudartExtract = Join-Path $tempDir "cudart"
Expand-Archive -Path $cudartZipPath -DestinationPath $cudartExtract -Force

$allDlls = @(Get-ChildItem $mainExtract   -Filter "*.dll" -Recurse) +
           @(Get-ChildItem $cudartExtract -Recurse -Filter "*.dll")
foreach ($d in $allDlls) { Copy-Item $d.FullName "$vendorDir\bin\" -Force }
Write-Host "  DLLs copied: $($allDlls.Count)"

# Step 2: Download public headers from source at matching tag
Write-Host ""
Write-Host "Step 2: Downloading public headers from tag $Release ..." -ForegroundColor White
$headers = @(
    @{ url = "$rawUrl/include/llama.h";             dest = "$vendorDir\include\llama.h" },
    @{ url = "$rawUrl/include/llama-cpp.h";         dest = "$vendorDir\include\llama-cpp.h" },
    @{ url = "$rawUrl/ggml/include/ggml.h";         dest = "$vendorDir\include\ggml\ggml.h" },
    @{ url = "$rawUrl/ggml/include/gguf.h";         dest = "$vendorDir\include\ggml\gguf.h" },
    @{ url = "$rawUrl/ggml/include/ggml-alloc.h";   dest = "$vendorDir\include\ggml\ggml-alloc.h" },
    @{ url = "$rawUrl/ggml/include/ggml-backend.h"; dest = "$vendorDir\include\ggml\ggml-backend.h" },
    @{ url = "$rawUrl/ggml/include/ggml-cpp.h";     dest = "$vendorDir\include\ggml\ggml-cpp.h" },
    @{ url = "$rawUrl/ggml/include/ggml-cpu.h";     dest = "$vendorDir\include\ggml\ggml-cpu.h" },
    @{ url = "$rawUrl/ggml/include/ggml-cuda.h";    dest = "$vendorDir\include\ggml\ggml-cuda.h" },
    @{ url = "$rawUrl/ggml/include/ggml-opt.h";     dest = "$vendorDir\include\ggml\ggml-opt.h" }
)
foreach ($h in $headers) { Download-File $h.url $h.dest }
# Also copy flat so both #include "ggml.h" and #include "ggml/ggml.h" work
Get-ChildItem "$vendorDir\include\ggml" -Filter "*.h" | ForEach-Object {
    Copy-Item $_.FullName "$vendorDir\include\" -Force
}
Write-Host "  Headers: $($headers.Count) files"

# Step 3: Generate import .lib files from DLLs
Write-Host ""
Write-Host "Step 3: Generating import libraries ..." -ForegroundColor White

$vsWhere   = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsInstall = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
if (-not $vsInstall) { $vsInstall = & $vsWhere -latest -products * -property installationPath 2>$null }
$vcToolsVer = (Get-ChildItem (Join-Path $vsInstall "VC\Tools\MSVC") | Sort-Object Name | Select-Object -Last 1).Name
$toolsDir  = Join-Path $vsInstall "VC\Tools\MSVC\$vcToolsVer\bin\HostX64\x64"
$dumpbin   = Join-Path $toolsDir "dumpbin.exe"
$libExe    = Join-Path $toolsDir "lib.exe"

function Make-ImportLib([string]$dllName) {
    $dll = "$vendorDir\bin\$dllName.dll"
    $def = "$tempDir\$dllName.def"
    $lib = "$vendorDir\lib\$dllName.lib"
    if (-not (Test-Path $dll)) {
        Write-Host "  SKIP $dllName.dll (not present)" -ForegroundColor DarkGray
        return
    }
    $exports   = & $dumpbin /exports $dll 2>$null
    $defLines  = @("LIBRARY $dllName", "EXPORTS")
    $inSection = $false
    foreach ($line in $exports) {
        if ($line -match "^\s+ordinal") { $inSection = $true; continue }
        if ($inSection -and $line -match "^\s+\d+\s+[\dA-Fa-f]+\s+[\dA-Fa-f]+\s+(\S+)") {
            $defLines += "    $($Matches[1])"
        }
    }
    New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
    $defLines | Set-Content $def -Encoding ASCII
    & $libExe /def:$def /out:$lib /machine:x64 2>&1 | Out-Null
    if (Test-Path $lib) {
        Write-Host "  OK  $dllName.lib ($([math]::Round((Get-Item $lib).Length/1kb)) KB)" -ForegroundColor Green
    }
    else {
        Write-Host "  FAIL $dllName.lib" -ForegroundColor Red
    }
}

Make-ImportLib "llama"
Make-ImportLib "ggml"

if (Test-Path $tempDir) { Remove-Item -Recurse -Force $tempDir }

# Verify
Write-Host ""
Write-Host "Verifying ..." -ForegroundColor Cyan
$checks = @(
    "$vendorDir\include\llama.h",
    "$vendorDir\include\ggml.h",
    "$vendorDir\lib\llama.lib",
    "$vendorDir\lib\ggml.lib",
    "$vendorDir\bin\llama.dll",
    "$vendorDir\bin\ggml.dll",
    "$vendorDir\bin\ggml-cuda.dll"
)
$ok = $true
foreach ($f in $checks) {
    if (Test-Path $f) { Write-Host "  OK  $f" -ForegroundColor Green }
    else              { Write-Host "  MISSING  $f" -ForegroundColor Red; $ok = $false }
}
if (-not $ok) { Write-Error "One or more required files are missing." }

Write-Host ""
Write-Host "Done. vendor\llama.cpp is ready." -ForegroundColor Green
Write-Host "IMPORTANT: Copy vendor\llama.cpp\bin\*.dll alongside KitsuneEngine.dll at runtime." -ForegroundColor Yellow
