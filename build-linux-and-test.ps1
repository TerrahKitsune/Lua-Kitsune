#Requires -Version 5.1
<#
.SYNOPSIS
    Builds libKitsuneEngine.so inside WSL and then runs the xUnit test suite,
    printing a clean summary of passed / failed / skipped tests.

.DESCRIPTION
    1. Converts the Windows repo root to a WSL /mnt/... path.
    2. Runs cmake (configure if no cache) + cmake --build inside WSL.
    3. Runs `dotnet test` on KitsuneNet.Tests and captures the TRX output.
    4. Parses and prints the results, then exits with a non-zero code if any
       tests failed so CI pipelines can gate on this script.

.PARAMETER Configuration
    CMake / dotnet build configuration.  Defaults to Release.

.PARAMETER Reconfigure
    When set, deletes the existing CMakeCache.txt so cmake re-configures from
    scratch before building.
#>
param(
    [string] $Configuration = 'Release',
    [switch] $Reconfigure
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ── Helper: coloured output ───────────────────────────────────────────────────
function Write-Header  { param([string]$msg) Write-Host "`n=== $msg ===" -ForegroundColor Cyan   }
function Write-Success { param([string]$msg) Write-Host $msg               -ForegroundColor Green  }
function Write-Failure { param([string]$msg) Write-Host $msg               -ForegroundColor Red    }
function Write-Info    { param([string]$msg) Write-Host $msg               -ForegroundColor Yellow }

# ── Resolve paths ─────────────────────────────────────────────────────────────
$repoRoot   = Split-Path -Parent $PSCommandPath          # directory this script lives in
$buildDir   = Join-Path $repoRoot 'build-linux'
$testProject = Join-Path $repoRoot 'KitsuneNet.Tests\KitsuneNet.Tests.csproj'
$trxDir     = Join-Path $repoRoot 'build-linux\test-results'

# Convert Windows path  C:\Users\...  →  /mnt/c/Users/...
$drive      = $repoRoot.Substring(0, 1).ToLower()
$rest       = $repoRoot.Substring(3).Replace('\', '/')
$wslRoot    = "/mnt/$drive/$rest"

# ── Verify WSL is available ───────────────────────────────────────────────────
Write-Header 'Checking WSL'
if (-not (Get-Command wsl -ErrorAction SilentlyContinue)) {
    Write-Failure 'ERROR: wsl.exe not found. Install WSL2 and a Linux distro first.'
    exit 1
}
$wslCheck = wsl bash -c 'echo ok' 2>&1
if ($wslCheck -ne 'ok') {
    Write-Failure "ERROR: WSL is not responding correctly ($wslCheck)."
    exit 1
}
Write-Success 'WSL is available.'

# ── CMake configure + build ───────────────────────────────────────────────────
Write-Header "Building libKitsuneEngine.so  ($Configuration)"

# Build the bash command as a single line — no here-strings, no newlines inside
# the wsl argument (PowerShell passes those literally and bash rejects them).
$cacheFile = "$wslRoot/build-linux/CMakeCache.txt"

$cmakeConfigure = "cmake .. -DCMAKE_BUILD_TYPE=$Configuration -DKITSUNE_ALL=ON"
$cmakeBuild     = 'cmake --build . --parallel'

# Reconfigure: wipe the cache so cmake reruns from scratch.
$reconfigureCmd = if ($Reconfigure) { "rm -f '$wslRoot/build-linux/CMakeCache.txt'; " } else { '' }

$bashCmd = "set -e; mkdir -p '$wslRoot/build-linux'; cd '$wslRoot/build-linux'; " +
           $reconfigureCmd +
           "if [ ! -f CMakeCache.txt ]; then $cmakeConfigure; fi; $cmakeBuild"

Write-Info "WSL command: $bashCmd"
wsl bash -c $bashCmd
if ($LASTEXITCODE -ne 0) {
    Write-Failure "ERROR: CMake build failed (exit $LASTEXITCODE)."
    exit $LASTEXITCODE
}

$soPath = Join-Path $buildDir 'libKitsuneEngine.so'
if (-not (Test-Path $soPath)) {
    Write-Failure "ERROR: libKitsuneEngine.so was not produced at: $soPath"
    exit 1
}
Write-Success "Build succeeded: $soPath"

# ── Rebuild C# test assembly ──────────────────────────────────────────────────
# The test DLL must be rebuilt from Windows so that any C# source changes are
# picked up before running tests inside WSL.  Pass BuildingInsideVisualStudio=true
# to suppress the C++ vcxproj target (which requires VS MSBuild, not dotnet CLI).
Write-Header 'Building C# test assembly'
& dotnet build $testProject --configuration $Configuration -p:BuildingInsideVisualStudio=true
if ($LASTEXITCODE -ne 0) {
    Write-Failure "ERROR: dotnet build failed (exit $LASTEXITCODE)."
    exit $LASTEXITCODE
}
Write-Success 'C# build succeeded.'

# ── Run tests inside WSL ──────────────────────────────────────────────────────
# dotnet test must run inside WSL so the Linux runtime loads libKitsuneEngine.so.
# Running it from Windows PowerShell would load KitsuneEngine.dll instead.
Write-Header 'Running xUnit tests (inside WSL)'

$null = New-Item -ItemType Directory -Force -Path $trxDir
$wslTrxDir  = "$wslRoot/build-linux/test-results"
$wslTrxFile = "$wslTrxDir/results.trx"
$trxFile    = Join-Path $trxDir 'results.trx'
if (Test-Path $trxFile) { Remove-Item $trxFile -Force }

# The test assembly is built by the Windows MSBuild. Locate it.
$testOutDir    = Join-Path $repoRoot "KitsuneNet.Tests\bin\$Configuration\net10.0"
$wslTestOutDir = "$wslRoot/KitsuneNet.Tests/bin/$Configuration/net10.0"
$testDll       = "$wslTestOutDir/KitsuneNet.Tests.dll"

# Copy libKitsuneEngine.so into the test output dir so .NET P/Invoke finds it
# alongside the test assembly (same-dir lookup before LD_LIBRARY_PATH).
# Also create a bare KitsuneEngine.so symlink: .NET tries "KitsuneEngine" → 
# "libKitsuneEngine.so" on Linux, but some runtimes also probe without the lib prefix.
$wslSo = "$wslRoot/build-linux/libKitsuneEngine.so"
$copyAndLink = "cp -f '$wslSo' '$wslTestOutDir/libKitsuneEngine.so'; " +
               "ln -sf '$wslTestOutDir/libKitsuneEngine.so' '$wslTestOutDir/KitsuneEngine.so'"

Write-Info "Copying .so into test output dir..."
wsl bash -c $copyAndLink
if ($LASTEXITCODE -ne 0) {
    Write-Failure "ERROR: Could not copy .so into test output dir."
    exit $LASTEXITCODE
}

# Run dotnet test inside WSL. Use the Linux dotnet (must be installed in WSL).
# Pipe stdout to a log file inside the WSL path so we can read it back.
# --blame-hang-timeout 60s aborts the run if any single test hangs that long,
# producing a dump and a non-zero exit code so the script fails fast.
$wslLogFile = "$wslRoot/build-linux/test-results/test-output.txt"
$dotnetCmd  = "set -e; mkdir -p '$wslTrxDir'; " +
              "dotnet test '$testDll' " +
              "--logger 'trx;LogFileName=$wslTrxFile' " +
              "--no-build " +
              "--blame-hang-timeout 60s " +
              "2>&1 | tee '$wslLogFile'"

Write-Info "Running: wsl bash -c `"$dotnetCmd`""
$ErrorActionPreference = 'Continue'
wsl bash -c $dotnetCmd
$testExitCode = $LASTEXITCODE
$ErrorActionPreference = 'Stop'

# Print the captured output so the caller sees it live (already tee'd in WSL).
$logFile = Join-Path $trxDir 'test-output.txt'
if (Test-Path $logFile) {
    Write-Host (Get-Content $logFile -Raw)
}

# ── Parse and print TRX results ───────────────────────────────────────────────
Write-Header 'Test Results'

if (-not (Test-Path $trxFile)) {
    Write-Failure 'ERROR: TRX result file was not created — dotnet test may have crashed before running any tests.'
    exit 1
}

[xml]$trx = Get-Content $trxFile -Raw

$counters  = $trx.TestRun.ResultSummary.Counters
$total     = [int]$counters.total
$passed    = [int]$counters.passed
$failed    = [int]$counters.failed
$skipped   = [int]($counters.notExecuted)
$outcome   = $trx.TestRun.ResultSummary.outcome

# ── Per-test detail ───────────────────────────────────────────────────────────
$results = $trx.TestRun.Results.UnitTestResult

foreach ($r in $results) {
    $icon = switch ($r.outcome) {
        'Passed'      { '[PASS]' }
        'Failed'      { '[FAIL]' }
        'NotExecuted' { '[SKIP]' }
        default       { '[????]' }
    }
    $color = switch ($r.outcome) {
        'Passed'      { 'Green'  }
        'Failed'      { 'Red'    }
        'NotExecuted' { 'Yellow' }
        default       { 'Gray'   }
    }
    Write-Host ("  {0} {1}  ({2})" -f $icon, $r.testName, $r.duration) -ForegroundColor $color

    if ($r.outcome -eq 'Failed') {
        $msg = $r.Output.ErrorInfo.Message
        $stk = $r.Output.ErrorInfo.StackTrace
        if ($msg) { Write-Host "       Message: $msg"    -ForegroundColor Red }
        if ($stk) { Write-Host "       Stack:   $stk"   -ForegroundColor DarkRed }
    }
}

# ── Summary line ──────────────────────────────────────────────────────────────
Write-Host ''
$summaryColor = if ($outcome -eq 'Passed') { 'Green' } else { 'Red' }
Write-Host ("Result: {0}   Total: {1}   Passed: {2}   Failed: {3}   Skipped: {4}" -f
    $outcome, $total, $passed, $failed, $skipped) -ForegroundColor $summaryColor

exit $testExitCode
