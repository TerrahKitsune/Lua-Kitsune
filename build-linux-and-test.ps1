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

# ── Run tests ─────────────────────────────────────────────────────────────────
Write-Header 'Running xUnit tests'

$null = New-Item -ItemType Directory -Force -Path $trxDir
$trxFile = Join-Path $trxDir 'results.trx'

# Remove stale result file so we never accidentally read last run's data.
if (Test-Path $trxFile) { Remove-Item $trxFile -Force }

$dotnetArgs = @(
    'test', $testProject,
    '--configuration', $Configuration,
    '--logger', "trx;LogFileName=$trxFile",
    '--no-build'         # native .so is already built; skip MSBuild CMake target
)

Write-Info "dotnet $($dotnetArgs -join ' ')"
# dotnet test returns non-zero for test failures; capture it instead of stopping.
$ErrorActionPreference = 'Continue'
dotnet @dotnetArgs
$testExitCode = $LASTEXITCODE
$ErrorActionPreference = 'Stop'

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
