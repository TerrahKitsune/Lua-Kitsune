# ─────────────────────────────────────────────────────────────────────────────
# build-linux-and-test.ps1
# Builds KitsuneEngine for Linux via WSL, deploys the .so, then runs all
# .NET tests with both MySQL and Postgres credentials injected.
# ─────────────────────────────────────────────────────────────────────────────

# ── Configurable variables ────────────────────────────────────────────────────

# CMake build directory (relative to repo root)
$BuildDir         = "build-linux"

# CMake feature flags  (space-separated -D options)
$CMakeFlags       = "-DKITSUNE_ALL=ON"

# .NET test project (relative to repo root)
$TestProject      = "KitsuneNet.Tests/KitsuneNet.Tests.csproj"

# Test configuration (Debug / Release)
$Configuration    = "Debug"

# dotnet TFM output folder
$Tfm              = "net10.0"

# Database credentials passed as env vars to dotnet test
$MySqlConnStr     = "10.9.23.252:3306:kitsune:testTest123!:kitsune"
$PostgresConnStr  = "host=10.9.23.252 port=5432 user=kitsune password=testTest123! dbname=kitsune"

# Destinations that need the fresh .so (relative to repo root)
$SoDeploys = @(
	"KitsuneNet/bin/$Configuration/$Tfm"
	"KitsuneNet.Tests/bin/$Configuration/$Tfm"
)

# Parallelism passed to cmake --build
$BuildJobs        = 0   # 0 = --parallel (let CMake choose)

# ── Derived paths (do not edit below this line) ───────────────────────────────

$RepoRoot  = $PSScriptRoot
$WslRoot   = "/mnt/" + ($RepoRoot -replace "\\", "/" -replace ":", "").ToLower().TrimStart("/")
$SoSource  = "$BuildDir/libKitsuneEngine.so"

# ─────────────────────────────────────────────────────────────────────────────

function Invoke-Wsl {
	param([string]$Command)
	wsl -e bash -c $Command
	if ($LASTEXITCODE -ne 0) {
		Write-Error "WSL command failed (exit $LASTEXITCODE): $Command"
		exit $LASTEXITCODE
	}
}

function Write-Step {
	param([string]$Message)
	Write-Host ""
	Write-Host "── $Message" -ForegroundColor Cyan
}

# ── 1. Configure ──────────────────────────────────────────────────────────────
Write-Step "Configuring ($CMakeFlags)"
Invoke-Wsl "cd '$WslRoot' && cmake $CMakeFlags -B $BuildDir . 2>&1"

# ── 2. Build ──────────────────────────────────────────────────────────────────
Write-Step "Building"
$parallelArg = if ($BuildJobs -eq 0) { "--parallel" } else { "--parallel $BuildJobs" }
Invoke-Wsl "cd '$WslRoot' && cmake --build $BuildDir $parallelArg 2>&1"

# ── 3. Deploy .so ─────────────────────────────────────────────────────────────
Write-Step "Deploying libKitsuneEngine.so"
foreach ($dest in $SoDeploys) {
	$destWsl = "$WslRoot/$dest"
	Invoke-Wsl "mkdir -p '$destWsl' && cp '$WslRoot/$SoSource' '$destWsl/libKitsuneEngine.so' && echo '  -> $dest'"
}

# ── 4. Run tests ──────────────────────────────────────────────────────────────
Write-Step "Running tests"
$testCmd = "cd '$WslRoot' && " +
           "KITSUNE_MYSQL_TEST='$MySqlConnStr' " +
           "KITSUNE_POSTGRES_TEST='$PostgresConnStr' " +
           "dotnet test '$TestProject' -v m 2>&1"
Invoke-Wsl $testCmd

Write-Host ""
Write-Host "Done." -ForegroundColor Green
