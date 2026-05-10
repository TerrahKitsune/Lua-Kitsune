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

# Resolve the WSL home directory as an absolute path so we can use it safely
# inside single-quoted bash strings (tilde is not expanded inside single quotes).
$wslHome = (wsl bash -c 'echo $HOME' 2>&1).Trim()
Write-Info "WSL home: $wslHome"

# ── Build bundled libevent 2.2 static libs (Linux) ───────────────────────────
# WebSocket support requires libevent 2.2 (event2/ws.h).  Ubuntu 24.04 ships
# only 2.1, so we build from source and cache the static libs in libevent/linux.
Write-Header 'Ensuring bundled libevent 2.2 (Linux static libs)'

$libeventMarker = Join-Path $repoRoot 'libevent\linux\lib\libevent.a'
if ($Reconfigure -and (Test-Path (Join-Path $repoRoot 'libevent\linux'))) {
    Write-Info '-Reconfigure: removing cached libevent/linux...'
    Remove-Item -Recurse -Force (Join-Path $repoRoot 'libevent\linux')
}
if (-not (Test-Path $libeventMarker)) {
    Write-Info 'libevent/linux/lib/libevent.a not found — building from source...'
    $wslLibeventOut = "$wslRoot/libevent/linux"
    # Write the build commands to a temp shell script using LF line endings and
    # no BOM so bash inside WSL can execute it without issues.
    $buildScript = Join-Path $repoRoot 'build-linux\build-libevent-tmp.sh'
    $null = New-Item -ItemType Directory -Force -Path (Join-Path $repoRoot 'build-linux')
    $scriptLines = @(
        '#!/usr/bin/env bash',
        'set -e',
        'apt-get install -y --no-install-recommends cmake make gcc g++ git 2>/dev/null || true',
        'tmp=$(mktemp -d)',
        'git clone --depth 1 https://github.com/libevent/libevent.git "$tmp/libevent-src"',
        'mkdir -p "$tmp/libevent-build"',
        'cd "$tmp/libevent-build"',
        'cmake "$tmp/libevent-src" -DCMAKE_BUILD_TYPE=Release -DEVENT__DISABLE_OPENSSL=ON -DEVENT__DISABLE_SAMPLES=ON -DEVENT__DISABLE_TESTS=ON -DEVENT__LIBRARY_TYPE=STATIC -DCMAKE_POSITION_INDEPENDENT_CODE=ON',
        'cmake --build . --parallel',
        "mkdir -p '$wslLibeventOut/lib'",
        "find . -name '*.a' -exec cp {} '$wslLibeventOut/lib/' ';'",
        # Copy the Linux-generated event-config.h so it replaces the Windows one when building on Linux
        "mkdir -p '$wslLibeventOut/include/event2'",
        "cp include/event2/event-config.h '$wslLibeventOut/include/event2/event-config.h'",
        'rm -rf "$tmp"'
    )
    [System.IO.File]::WriteAllText($buildScript, ($scriptLines -join "`n") + "`n", [System.Text.UTF8Encoding]::new($false))
    $wslScript = "$wslRoot/build-linux/build-libevent-tmp.sh"
    wsl bash $wslScript
    if ($LASTEXITCODE -ne 0) {
        Write-Failure 'ERROR: Failed to build libevent 2.2 from source.'
        exit $LASTEXITCODE
    }
    Remove-Item $buildScript -Force
    Write-Success 'libevent 2.2 built and cached in libevent/linux/lib.'
}
else {
    Write-Success 'Bundled libevent 2.2 already built — skipping.'
}

# ── CMake configure + build ───────────────────────────────────────────────────
Write-Header "Building libKitsuneEngine.so  ($Configuration)"

# Build in the WSL-native home directory to avoid DrvFs large-file write
# issues (/mnt/c/... paths cannot reliably hold large shared libraries).
# The finished .so is copied back to the Windows build-linux dir for reference.
$wslNativeBuildDir = "$wslHome/kitsune-build"
$cmakeConfigure = "cmake '$wslRoot' -DCMAKE_BUILD_TYPE=$Configuration -DKITSUNE_ALL=ON"
$cmakeBuild     = 'cmake --build . --parallel'

# Reconfigure: wipe the cmake cache so cmake re-configures from scratch.
$reconfigureCmd = if ($Reconfigure) { "rm -f '$wslNativeBuildDir/CMakeCache.txt'; " } else { '' }

$bashCmd = "set -e; mkdir -p $wslNativeBuildDir; cd $wslNativeBuildDir; " +
           $reconfigureCmd +
           "if [ ! -f CMakeCache.txt ]; then $cmakeConfigure; fi; $cmakeBuild; " +
           "cp -f $wslNativeBuildDir/libKitsuneEngine.so '$wslRoot/build-linux/libKitsuneEngine.so'"

Write-Info "WSL command: $bashCmd"
wsl bash -c $bashCmd
if ($LASTEXITCODE -ne 0) {
    Write-Failure "ERROR: CMake build failed (exit $LASTEXITCODE)."
    exit $LASTEXITCODE
}

$soPath = Join-Path $buildDir 'libKitsuneEngine.so'
if (-not (Test-Path $soPath) -or (Get-Item $soPath).Length -eq 0) {
    Write-Failure "ERROR: libKitsuneEngine.so was not produced at: $soPath"
    exit 1
}


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
$wslNativeSoDir = $wslNativeBuildDir

# Run dotnet test inside WSL. Use the Linux dotnet (must be installed in WSL).
# LD_LIBRARY_PATH points to the WSL-native build dir (not the DrvFs copy) so
# .NET P/Invoke reliably finds and loads libKitsuneEngine.so.
# --blame-hang-timeout 60s aborts the run if any single test hangs that long,
# producing a dump and a non-zero exit code so the script fails fast.
$wslRunSettings = "$wslRoot/KitsuneNet.Tests/kitsune.runsettings"
$wslLogFile = "$wslRoot/build-linux/test-results/test-output.txt"
$dotnetCmd  = "set -e; mkdir -p '$wslTrxDir'; " +
              "LD_LIBRARY_PATH='$wslNativeSoDir' " +
              "dotnet test '$testDll' " +
              "--logger 'trx;LogFileName=$wslTrxFile' " +
              "--no-build " +
              "--blame-hang-timeout 60s " +
              "--settings '$wslRunSettings' " +
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
