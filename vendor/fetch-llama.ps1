# fetch-llama.ps1 -- Downloads prebuilt llama.cpp binaries into vendor\llama.cpp\
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File vendor\fetch-llama.ps1
#   powershell -ExecutionPolicy Bypass -File vendor\fetch-llama.ps1 -CudaVersion 13.1
#   powershell -ExecutionPolicy Bypass -File vendor\fetch-llama.ps1 -Release b9037 -CudaVersion 12.4
#
# CUDA version guide:
#   12.4 -- RTX 20xx, 30xx, 40xx (default)
#   13.1 -- RTX 50xx (Blackwell)
#
# What this script does:
#   1. Downloads the prebuilt DLLs from the GitHub release
#   2. Downloads matching public headers from the source tree at the same tag
#   3. Generates import .lib files from the DLLs using MSVC lib.exe + dumpbin.exe
#
# Output: vendor\llama.cpp\{include, lib, bin}
# The bin\ DLLs must be placed alongside KitsuneEngine.dll at runtime.

param(
    [string]$Release     = "b9037",
    [string]$CudaVersion = "12.4"
)

$ErrorActionPreference = "Stop"

$baseUrl   = "https://github.com/ggml-org/llama.cpp/releases/download/$Release"
$rawUrl    = "https://raw.githubusercontent.com/ggerganov/llama.cpp/$Release"
$mainZip   = "llama-$Release-bin-win-cuda-$CudaVersion-x64.zip"
$cudartZip = "cudart-llama-bin-win-cuda-$CudaVersion-x64.zip"
$vendorDir = "$PSScriptRoot\llama.cpp"
$tempDir   = "$env:TEMP\fetch-llama-$Release"

Write-Host "=== fetch-llama.ps1 ===" -ForegroundColor Cyan
Write-Host "Release:      $Release"
Write-Host "CUDA version: $CudaVersion"
Write-Host "Output:       $vendorDir"
Write-Host ""

# Find MSVC tools
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vsWhere)) { $vsWhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe" }
if (-not (Test-Path $vsWhere)) { Write-Error "vswhere.exe not found. Install Visual Studio with the C++ workload." }
$vsPath  = & $vsWhere -latest -property installationPath
$msvcDir = Get-ChildItem "$vsPath\VC\Tools\MSVC" | Sort-Object Name -Descending | Select-Object -First 1
$libExe  = "$($msvcDir.FullName)\bin\Hostx64\x64\lib.exe"
$dumpbin = "$($msvcDir.FullName)\bin\Hostx64\x64\dumpbin.exe"
if (-not (Test-Path $libExe)) { Write-Error "lib.exe not found at: $libExe" }
Write-Host "MSVC:  $($msvcDir.Name)" -ForegroundColor DarkGray
Write-Host ""

# Clean and create directories
if (Test-Path $vendorDir) {
    Write-Host "Removing existing vendor\llama.cpp ..." -ForegroundColor Yellow
    Remove-Item $vendorDir -Recurse -Force
}
New-Item -ItemType Directory -Path "$vendorDir\include\ggml" | Out-Null
New-Item -ItemType Directory -Path "$vendorDir\lib"          | Out-Null
New-Item -ItemType Directory -Path "$vendorDir\bin"          | Out-Null
New-Item -ItemType Directory -Path $tempDir -Force           | Out-Null

function Download-File($url, $dest) {
    if (Test-Path $dest) {
        Write-Host "  Cached: $(Split-Path $dest -Leaf)" -ForegroundColor DarkGray
    } else {
        Write-Host "  GET $(Split-Path $url -Leaf)" -ForegroundColor Cyan
        Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
    }
}

# Step 1: Download and extract DLL packages
Write-Host "Step 1: Downloading prebuilt DLLs ..." -ForegroundColor White
$mainZipPath   = "$tempDir\$mainZip"
$cudartZipPath = "$tempDir\$cudartZip"
Download-File "$baseUrl/$mainZip"   $mainZipPath
Download-File "$baseUrl/$cudartZip" $cudartZipPath

$mainExtract   = "$tempDir\main"
$cudartExtract = "$tempDir\cudart"
Write-Host "  Extracting $mainZip ..." -ForegroundColor Cyan
Expand-Archive -Path $mainZipPath   -DestinationPath $mainExtract   -Force
Write-Host "  Extracting $cudartZip ..." -ForegroundColor Cyan
Expand-Archive -Path $cudartZipPath -DestinationPath $cudartExtract -Force

$allDlls = @(Get-ChildItem $mainExtract   -Filter "*.dll") +
           @(Get-ChildItem $cudartExtract -Recurse -Filter "*.dll")
foreach ($d in $allDlls) { Copy-Item $d.FullName "$vendorDir\bin\" -Force }
Write-Host "  DLLs copied: $($allDlls.Count)"

# Step 2: Download public headers from source at matching tag
Write-Host ""
Write-Host "Step 2: Downloading public headers from tag $Release ..." -ForegroundColor White
$headers = @(
    @{ url = "$rawUrl/include/llama.h";            dest = "$vendorDir\include\llama.h" },
    @{ url = "$rawUrl/include/llama-cpp.h";         dest = "$vendorDir\include\llama-cpp.h" },
    @{ url = "$rawUrl/ggml/include/ggml.h";         dest = "$vendorDir\include\ggml\ggml.h" },
    @{ url = "$rawUrl/ggml/include/gguf.h";         dest = "$vendorDir\include\ggml\gguf.h" },
    @{ url = "$rawUrl/ggml/include/ggml-alloc.h";   dest = "$vendorDir\include\ggml\ggml-alloc.h" },
    @{ url = "$rawUrl/ggml/include/ggml-backend.h"; dest = "$vendorDir\include\ggml\ggml-backend.h" },
    @{ url = "$rawUrl/ggml/include/ggml-cpp.h";     dest = "$vendorDir\include\ggml\ggml-cpp.h" },
    @{ url = "$rawUrl/ggml/include/ggml-cpu.h";     dest = "$vendorDir\include\ggml\ggml-cpu.h" },
    @{ url = "$rawUrl/ggml/include/ggml-cuda.h";    dest = "$vendorDir\include\ggml\ggml-cuda.h" }
)
foreach ($h in $headers) { Download-File $h.url $h.dest }
# Also copy ggml headers flat so #include "ggml.h" works without subdir
Get-ChildItem "$vendorDir\include\ggml" -Filter "*.h" | ForEach-Object {
    Copy-Item $_.FullName "$vendorDir\include\" -Force
}
Write-Host "  Headers: $($headers.Count) files"

# Step 3: Generate import .lib files from DLLs
Write-Host ""
Write-Host "Step 3: Generating import libraries ..." -ForegroundColor White

function Make-ImportLib($dllName) {
    $dll = "$vendorDir\bin\$dllName.dll"
    $def = "$tempDir\$dllName.def"
    $lib = "$vendorDir\lib\$dllName.lib"
    if (-not (Test-Path $dll)) {
        Write-Host "  SKIP $dllName.dll (not present)" -ForegroundColor DarkGray
        return
    }
    $exports = & $dumpbin /exports $dll 2>$null
    $defLines = @("LIBRARY $dllName", "EXPORTS")
    $inSection = $false
    foreach ($line in $exports) {
        if ($line -match "^\s+ordinal") { $inSection = $true; continue }
        if ($inSection -and $line -match "^\s+\d+\s+[\dA-Fa-f]+\s+[\dA-Fa-f]+\s+(\S+)") {
            $defLines += "    $($Matches[1])"
        }
    }
    $defLines | Set-Content $def -Encoding ASCII
    & $libExe /def:$def /out:$lib /machine:x64 2>&1 | Out-Null
    if (Test-Path $lib) {
        Write-Host "  OK  $dllName.lib ($([math]::Round((Get-Item $lib).Length/1kb)) KB)" -ForegroundColor Green
    } else {
        Write-Host "  FAIL $dllName.lib" -ForegroundColor Red
    }
}

Make-ImportLib "llama"
Make-ImportLib "ggml"

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
    if (Test-Path $f) {
        Write-Host "  OK  $f" -ForegroundColor Green
    } else {
        Write-Host "  MISSING  $f" -ForegroundColor Red
        $ok = $false
    }
}
if (-not $ok) { Write-Error "One or more required files are missing." }

Write-Host ""
Write-Host "Done. vendor\llama.cpp is ready." -ForegroundColor Green
Write-Host ""
Write-Host "IMPORTANT: Copy all DLLs from vendor\llama.cpp\bin\ alongside KitsuneEngine.dll at runtime." -ForegroundColor Yellow
