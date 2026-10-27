#Requires -Version 5.1
<#
.SYNOPSIS
    Builds libKitsuneEngine.so with AddressSanitizer + LeakSanitizer inside WSL,
    runs the xUnit suite against it and reports memory errors and leaks.

.DESCRIPTION
    1. Configures and builds into ~/kitsune-build-asan inside WSL with
       -DKITSUNE_SANITIZE=ON (Debug, all modules). The normal build is untouched.
    2. Builds the C# test assembly.
    3. Runs `dotnet test` inside WSL with the ASan runtime LD_PRELOADed (dotnet
       itself is not instrumented) and LeakSanitizer suppressions from
       lsan-suppressions.txt.
    4. Scans the sanitizer logs. Exits non-zero if any test failed, any ASan
       error was reported, or any leak survived the suppressions.

    A memory error aborts the test host; --blame names the test that was running.
    Needs the bundled libevent built by build-linux-and-test.ps1 first.

.PARAMETER Filter
    Optional `dotnet test --filter` expression to limit the run.

.PARAMETER Reconfigure
    Deletes the sanitizer build's CMakeCache.txt before building.
#>
param(
    [string] $Filter = '',
    [switch] $Reconfigure
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Header  { param([string]$msg) Write-Host "`n=== $msg ===" -ForegroundColor Cyan   }
function Write-Success { param([string]$msg) Write-Host $msg               -ForegroundColor Green  }
function Write-Failure { param([string]$msg) Write-Host $msg               -ForegroundColor Red    }
function Write-Info    { param([string]$msg) Write-Host $msg               -ForegroundColor Yellow }

$repoRoot    = Split-Path -Parent $PSCommandPath
$outDir      = Join-Path $repoRoot 'build-linux-asan'
$resultDir   = Join-Path $outDir 'test-results'
$testProject = Join-Path $repoRoot 'KitsuneNet.Tests\KitsuneNet.Tests.csproj'

$drive   = $repoRoot.Substring(0, 1).ToLower()
$rest    = $repoRoot.Substring(3).Replace('\', '/')
$wslRoot = "/mnt/$drive/$rest"

if (-not (Test-Path (Join-Path $repoRoot 'libevent\linux\lib\libevent.a'))) {
    Write-Failure 'ERROR: libevent/linux is missing. Run build-linux-and-test.ps1 once first.'
    exit 1
}

$wslHome = (wsl bash -c 'echo $HOME' 2>&1).Trim()

Write-Header 'Building libKitsuneEngine.so (Debug, AddressSanitizer)'
$wslBuildDir    = "$wslHome/kitsune-build-asan"
$cmakeConfigure = "cmake '$wslRoot' -DCMAKE_BUILD_TYPE=Debug -DKITSUNE_ALL=ON -DKITSUNE_SANITIZE=ON"
$reconfigureCmd = if ($Reconfigure) { "rm -f '$wslBuildDir/CMakeCache.txt'; " } else { '' }
$bashCmd = "set -e; mkdir -p $wslBuildDir; cd $wslBuildDir; " +
           $reconfigureCmd +
           "if [ ! -f CMakeCache.txt ]; then $cmakeConfigure; fi; cmake --build . --parallel; " +
           "mkdir -p '$wslRoot/build-linux-asan'; cp -f $wslBuildDir/libKitsuneEngine.so '$wslRoot/build-linux-asan/libKitsuneEngine.so'"
wsl bash -c $bashCmd
if ($LASTEXITCODE -ne 0) {
    Write-Failure "ERROR: CMake build failed (exit $LASTEXITCODE)."
    exit $LASTEXITCODE
}

Write-Header 'Building C# test assembly'
& dotnet build $testProject --configuration Release -p:BuildingInsideVisualStudio=true
if ($LASTEXITCODE -ne 0) {
    Write-Failure "ERROR: dotnet build failed (exit $LASTEXITCODE)."
    exit $LASTEXITCODE
}

Write-Header 'Running xUnit tests under AddressSanitizer (inside WSL)'
if (Test-Path $resultDir) { Remove-Item -Recurse -Force $resultDir }
$null = New-Item -ItemType Directory -Force -Path $resultDir

$wslResultDir = "$wslRoot/build-linux-asan/test-results"
$wslTrxFile   = "$wslResultDir/results.trx"

# .NET loads native libraries from the app directory before LD_LIBRARY_PATH, and the
# normal build's libKitsuneEngine.so sits in bin/Release. Run from a copy with the
# sanitizer build in its place so the normal output stays untouched.
$wslTestDir = "$wslHome/kitsune-asan-tests"
wsl bash -c "set -e; rm -rf '$wslTestDir'; cp -r '$wslRoot/KitsuneNet.Tests/bin/Release/net10.0' '$wslTestDir'; cp -f '$wslBuildDir/libKitsuneEngine.so' '$wslTestDir/libKitsuneEngine.so'"
if ($LASTEXITCODE -ne 0) {
    Write-Failure 'ERROR: could not prepare the sanitizer test directory.'
    exit $LASTEXITCODE
}
$testDll = "$wslTestDir/KitsuneNet.Tests.dll"
$runSettings  = "$wslRoot/KitsuneNet.Tests/kitsune.runsettings"
$suppressions = "$wslRoot/lsan-suppressions.txt"

# handle_segv/allow_user_segv_handler: .NET turns SIGSEGV into NullReferenceException,
# so ASan must leave that signal to the runtime.
# leak_check_at_exit=0: the test runner ends the test host without a normal exit, so the exit-time check
# would only cover the runner itself. LeakSanitizerCheck (KITSUNE_LSAN=1) runs it in the test host instead.
# fast_unwind_on_malloc=0: the bundled libevent is built without frame pointers, so the fast unwinder
# stops inside it and a leak's stack never reaches our code.
$asanOptions = "detect_leaks=1:leak_check_at_exit=0:fast_unwind_on_malloc=0:handle_segv=0:allow_user_segv_handler=1:detect_stack_use_after_return=0:log_path=$wslResultDir/asan"
$lsanOptions = "suppressions=${suppressions}:print_suppressions=0"
$filterArg   = if ($Filter) { "--filter '$Filter' " } else { '' }

$dotnetCmd = "LD_PRELOAD=`$(gcc -print-file-name=libasan.so) " +
             "ASAN_OPTIONS='$asanOptions' LSAN_OPTIONS='$lsanOptions' KITSUNE_LSAN=1 " +
             "dotnet test '$testDll' $filterArg" +
             "--logger 'trx;LogFileName=$wslTrxFile' " +
             "--blame --blame-hang-timeout 300s " +
             "--settings '$runSettings' " +
             "2>&1 | tee '$wslResultDir/test-output.txt'"

$ErrorActionPreference = 'Continue'
wsl bash -c $dotnetCmd
$ErrorActionPreference = 'Stop'

Write-Header 'Results'
$exitCode = 0

$trxFile = Join-Path $resultDir 'results.trx'
if (Test-Path $trxFile) {
    # XPath rather than property access: an aborted run's TRX has no Results element, which
    # StrictMode would turn into a terminating error before the sanitizer logs are printed.
    [xml]$trx = Get-Content $trxFile -Raw
    $summary = $trx.SelectSingleNode("//*[local-name()='ResultSummary']")
    $c = $summary.SelectSingleNode("*[local-name()='Counters']")
    Write-Info "Total $($c.GetAttribute('total')), passed $($c.GetAttribute('passed')), failed $($c.GetAttribute('failed')), not run $($c.GetAttribute('notExecuted'))"
    $failed = @($trx.SelectNodes("//*[local-name()='UnitTestResult'][@outcome='Failed']"))
    foreach ($r in $failed) { Write-Failure "  FAILED $($r.GetAttribute('testName'))" }
    if ($failed.Count -gt 0 -or $summary.GetAttribute('outcome') -ne 'Completed') {
        Write-Failure "  Run outcome: $($summary.GetAttribute('outcome'))"
        $exitCode = 1
    }
}
else {
    Write-Failure 'No TRX file: the test host probably aborted on a sanitizer error. See test-output.txt and the Sequence_*.xml blame file.'
    $exitCode = 1
}

$logs = @(Get-ChildItem $resultDir -Filter 'asan.*' -ErrorAction SilentlyContinue)
$errorLogs = @($logs | Where-Object { Select-String -Path $_.FullName -Pattern 'ERROR: AddressSanitizer' -Quiet })
$leakLogs  = @($logs | Where-Object { Select-String -Path $_.FullName -Pattern 'ERROR: LeakSanitizer' -Quiet })

# The sanitizer writes a log only when it has something to say; anything else (a bad option, an
# unreadable suppressions file) means leak checking may not have run, so it fails the run too.
foreach ($log in @($logs | Where-Object { $errorLogs -notcontains $_ -and $leakLogs -notcontains $_ })) {
    Write-Failure "`nSanitizer message ($($log.Name)):"
    Get-Content $log.FullName -TotalCount 20 | ForEach-Object { Write-Host "  $_" }
    $exitCode = 1
}

foreach ($log in $errorLogs) {
    Write-Failure "`nAddressSanitizer error ($($log.Name)):"
    Get-Content $log.FullName -TotalCount 40 | ForEach-Object { Write-Host "  $_" }
    $exitCode = 1
}
# LeakSanitizer cannot see references held by .NET objects (the GC heap is not scanned), so native memory
# allocated by .NET code itself, whose stack has no named frame, is reported but cannot be judged. Those are
# listed for information only; every leak with a named native frame fails the run.
foreach ($log in $leakLogs) {
    $blocks = [regex]::Split((Get-Content $log.FullName -Raw), '(?m)^(?=(?:Direct|Indirect) leak)') |
        Where-Object { $_ -match '^(Direct|Indirect) leak' }
    foreach ($block in $blocks) {
        $frames = @([regex]::Matches($block, '(?m)^\s+#(\d+) .*$') | Where-Object { $_.Groups[1].Value -ne '0' } | ForEach-Object { $_.Value })
        $fromDotNet = $frames.Count -gt 0 -and @($frames | Where-Object { $_ -notmatch '<unknown module>' }).Count -eq 0
        $lines = $block -split "`n" | Where-Object { $_ -match '^(Direct|Indirect) leak|^\s+#\d+ ' } | Select-Object -First 16
        if ($fromDotNet) {
            Write-Info "`nNot attributable, allocated from .NET code ($($log.Name)): $($lines[0])"
        }
        else {
            Write-Failure "`nLeakSanitizer report ($($log.Name)):"
            $lines | ForEach-Object { Write-Host "  $_" }
            $exitCode = 1
        }
    }
}

if ($exitCode -eq 0) { Write-Success "`nNo test failures, no ASan errors, no attributable leaks." }
exit $exitCode
