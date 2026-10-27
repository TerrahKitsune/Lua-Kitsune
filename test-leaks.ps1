#Requires -Version 5.1
<#
.SYNOPSIS
    Runs the Windows test suite one test at a time so the Debug native leak check
    covers every test and a leak fails the test that caused it.

.DESCRIPTION
    The leak check in KitsuneEngine.Dispose runs only when the last engine in the
    process is disposed. In the normal parallel run that rarely happens and a leak
    is blamed on whichever test disposed last. This script:
    1. Sets KITSUNE_LEAK_RUN=1, which turns on LeakRunGuardAttribute: a test fails if
       an engine is alive before it starts or after it ends.
    2. Runs `dotnet test` with xUnit parallelisation off and a TRX logger.
    3. Lists leak failures and guard failures separately, then exits non-zero if
       any test failed.

    Needs the Debug engine (the leak counter is compiled only in Debug) and a built
    test project; it runs with --no-build.

.PARAMETER Filter
    Optional `dotnet test --filter` expression to limit the run.
#>
param(
    [string] $Filter = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot    = Split-Path -Parent $PSCommandPath
$testProject = Join-Path $repoRoot 'KitsuneNet.Tests\KitsuneNet.Tests.csproj'
$trxDir      = Join-Path $repoRoot 'KitsuneNet.Tests\TestResults\leaks'
$trxFile     = Join-Path $trxDir 'leaks.trx'

if (Test-Path $trxFile) { Remove-Item $trxFile -Force }

$dotnetArgs = @('test', $testProject, '-c', 'Debug', '-p:BuildProjectReferences=false', '--no-build',
                '--logger', "trx;LogFileName=$trxFile")
if ($Filter) { $dotnetArgs += @('--filter', $Filter) }
$dotnetArgs += @('--', 'xUnit.ParallelizeTestCollections=false', 'xUnit.MaxParallelThreads=1')

$env:KITSUNE_LEAK_RUN = '1'
try {
    & dotnet @dotnetArgs | Out-Host
}
finally {
    Remove-Item Env:KITSUNE_LEAK_RUN
}

if (-not (Test-Path $trxFile)) {
    Write-Host 'ERROR: no TRX file; dotnet test may have crashed before running any tests.' -ForegroundColor Red
    exit 1
}

# XPath rather than property access: StrictMode turns a missing element (no Results in an aborted run,
# no Message on some failures) into a terminating error.
[xml]$trx = Get-Content $trxFile -Raw
$summary  = $trx.SelectSingleNode("//*[local-name()='ResultSummary']")
$counters = $summary.SelectSingleNode("*[local-name()='Counters']")
$failed   = @($trx.SelectNodes("//*[local-name()='UnitTestResult'][@outcome='Failed']") | ForEach-Object {
    $message = $_.SelectSingleNode(".//*[local-name()='Message']")
    [pscustomobject]@{ Name = $_.GetAttribute('testName'); Message = if ($message) { $message.InnerText } else { '' } }
})

$leaks  = @($failed | Where-Object { $_.Message -match 'Native memory leak' })
$guards = @($failed | Where-Object { $_.Message -match 'engine\(s\) (alive before|still alive after)' })
$other  = @($failed | Where-Object { $leaks -notcontains $_ -and $guards -notcontains $_ })

Write-Host ''
Write-Host "Total $($counters.GetAttribute('total')), passed $($counters.GetAttribute('passed')), failed $($counters.GetAttribute('failed')), not run $($counters.GetAttribute('notExecuted'))"
foreach ($group in @(@('Leaks', $leaks), @('Engine left alive', $guards), @('Other failures', $other))) {
    if ($group[1].Count -eq 0) { continue }
    Write-Host "`n$($group[0]) ($($group[1].Count)):" -ForegroundColor Red
    foreach ($result in $group[1]) {
        Write-Host "  $($result.Name)"
        Write-Host "    $(($result.Message -split "`n")[0])"
    }
}

if ($failed.Count -gt 0 -or $summary.GetAttribute('outcome') -ne 'Completed') { exit 1 }
Write-Host "`nNo leaks." -ForegroundColor Green
