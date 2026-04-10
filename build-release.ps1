# ─────────────────────────────────────────────────────────────────────────────
# build-release.ps1
# Builds KitsuneEngine for Windows (Release x64) and Linux (Release via WSL),
# then assembles distributable files into:
#   build/windows  – KitsuneEngine.dll, KitsuneNet.dll (+ PDBs)
#   build/linux    – libKitsuneEngine.so
# The build/ folder is added to .gitignore automatically.
# ─────────────────────────────────────────────────────────────────────────────
$ErrorActionPreference = 'Stop'

# ── Configurable variables ────────────────────────────────────────────────────

# Relative path where MSVC outputs the Release DLL
$WinBuildDir   = "x64\Release"

# CMake build directory used exclusively for the Linux release build
# (kept separate from the debug build-linux used by build-linux-and-test.ps1)
$LinuxBuildDir = "build-linux-release"

# C# project and target framework
$CSharpProject = "KitsuneNet\KitsuneNet.csproj"
$CSharpTfm     = "net10.0"

# CMake flags for the Linux release
$LinuxCMakeFlags = "-DKITSUNE_ALL=ON -DCMAKE_BUILD_TYPE=Release"

# ── Derived paths (do not edit below this line) ───────────────────────────────

$RepoRoot = $PSScriptRoot
$WslRoot  = "/mnt/" + ($RepoRoot -replace "\\", "/" -replace ":", "").ToLower().TrimStart("/")

$WinOutDir = Join-Path $RepoRoot $WinBuildDir
$WinDest   = Join-Path $RepoRoot "build\windows"
$LinDest   = Join-Path $RepoRoot "build\linux"

# ─────────────────────────────────────────────────────────────────────────────

function Write-Step {
	param([string]$Message)
	Write-Host ""
	Write-Host "── $Message" -ForegroundColor Cyan
}

function Invoke-Wsl {
	param([string]$Command)
	wsl -e bash -c $Command
	if ($LASTEXITCODE -ne 0) {
		Write-Error "WSL command failed (exit $LASTEXITCODE): $Command"
		exit $LASTEXITCODE
	}
}

function Add-GitIgnoreEntry {
	param([string]$Entry)
	$path = Join-Path $RepoRoot ".gitignore"
	if (Test-Path $path) {
		$existing = Get-Content $path -Raw
		# Exact line match
		if ($existing -match ('(?m)^' + [regex]::Escape($Entry) + '$')) {
			Write-Host "  '$Entry' already present (exact)"
			return
		}
		# Wildcard coverage: check if any existing glob covers this entry
		# e.g. build-*/ covers build-linux-release/
		$lines = Get-Content $path
		foreach ($line in $lines) {
			$trimmed = $line.Trim()
			if ($trimmed -like '' -or $trimmed.StartsWith('#')) { continue }
			$pattern = '^' + [regex]::Escape($trimmed).Replace('\*', '.*').Replace('\?', '.') + '$'
			if ($Entry -match $pattern) {
				Write-Host "  '$Entry' already covered by '$trimmed'"
				return
			}
		}
		Add-Content $path "`n$Entry"
	}
	else {
		Set-Content $path $Entry -Encoding UTF8
	}
	Write-Host "  Added '$Entry'"
}

# ── 1. Locate MSBuild ─────────────────────────────────────────────────────────
Write-Step "Locating MSBuild"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
	Write-Error "vswhere.exe not found — is Visual Studio installed?"
	exit 1
}
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild `
	-find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) {
	Write-Error "MSBuild.exe not found via vswhere"
	exit 1
}
Write-Host "  $msbuild"

# ── 2. Build Windows native DLL ───────────────────────────────────────────────
Write-Step "Building Windows: KitsuneEngine.dll (Release|x64)"
& $msbuild (Join-Path $RepoRoot "KitsuneEngine.vcxproj") `
	/p:Configuration=Release `
	/p:Platform=x64 `
	/p:SolutionDir="$RepoRoot\" `
	/m /nologo /v:m
if ($LASTEXITCODE -ne 0) { Write-Error "MSBuild failed"; exit $LASTEXITCODE }

# ── 3. Build C# wrapper ───────────────────────────────────────────────────────
Write-Step "Building Windows: KitsuneNet.dll (Release)"
# Use MSBuild.exe (not dotnet) so the BuildNativeEngine target inside
# KitsuneNet.csproj can evaluate Kitsune.vcxproj without VCTargetsPath errors.
& $msbuild (Join-Path $RepoRoot $CSharpProject) `
	/p:Configuration=Release `
	/p:Platform=x64 `
	/p:SolutionDir="$RepoRoot\" `
	/nologo /v:m
if ($LASTEXITCODE -ne 0) { Write-Error "MSBuild (KitsuneNet) failed"; exit $LASTEXITCODE }

# ── 4. Build Linux shared library ─────────────────────────────────────────────
Write-Step "Installing Linux prerequisites"
Invoke-Wsl "dpkg -s libarchive-dev > /dev/null 2>&1 || (sudo apt-get update -qq && sudo apt-get install -y libarchive-dev)"

Write-Step "Configuring Linux: CMake Release"
Invoke-Wsl "cmake $LinuxCMakeFlags -B '$WslRoot/$LinuxBuildDir' '$WslRoot' 2>&1"

Write-Step "Building Linux: libKitsuneEngine.so"
Invoke-Wsl "cmake --build '$WslRoot/$LinuxBuildDir' --parallel 2>&1"

# ── 5. Assemble build/windows ─────────────────────────────────────────────────
Write-Step "Assembling build\windows"
New-Item -ItemType Directory -Force -Path $WinDest | Out-Null

# ── Locate dumpbin alongside the MSBuild we already found ────────────────────
$dumpbin = & $vswhere -latest `
	-find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' | Select-Object -First 1

# ── Build a name→path index of every DLL in the repo (depth 5, skip output)
#    Priority: x64\Release > x64\Debug > repo root > elsewhere
Write-Host "  Building repo DLL index..."
$dllIndex = @{}
Get-ChildItem $RepoRoot -Filter '*.dll' -Recurse -Depth 5 |
	Where-Object { $_.FullName -notmatch [regex]::Escape("$RepoRoot\build\") } |
	ForEach-Object {
		$key  = $_.Name.ToLower()
		$prio = if     ($_.DirectoryName -like '*x64*Release*') { 0 }
				elseif ($_.DirectoryName -like '*x64*Debug*')   { 1 }
				elseif ($_.DirectoryName -eq   $RepoRoot)       { 2 }
				else                                            { 3 }
		if (-not $dllIndex.ContainsKey($key) -or $prio -lt $dllIndex[$key].Prio) {
			$dllIndex[$key] = [PSCustomObject]@{ Path = $_.FullName; Prio = $prio }
		}
	}
Write-Host "  Indexed $($dllIndex.Count) DLLs"

# ── Windows system / MSVC DLLs that ship with Windows or the VC++ Redist ─────
$sysMatch = '^(KERNEL32|USER32|GDI32|ADVAPI32|SHELL32|OLE32|OLEAUT32|WINSPOOL|' +
			'COMDLG32|WINMM|WS2_32|NTDLL|BCRYPT|CRYPT32|PSAPI|IPHLPAPI|SECUR32|' +
			'NETAPI32|COMCTL32|MSVCP|VCRUNTIME|MSVCRT|UCRTBASE|CONCRT|' +
			'api-ms-win|ext-ms-win)'

# ── Resolve KitsuneEngine.dll imports via dumpbin and copy each dep ───────────
$staged = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

if ($dumpbin) {
	Write-Host "  Resolving KitsuneEngine.dll dependencies..."
	$imports = & $dumpbin /DEPENDENTS (Join-Path $WinOutDir 'KitsuneEngine.dll') |
		Where-Object { $_ -match '^\s+\S+\.dll\s*$' } |
		ForEach-Object { $_.Trim() }

	foreach ($dll in $imports) {
		if ($dll -imatch $sysMatch)   { continue }
		if (-not $staged.Add($dll))   { continue }   # already staged

		$entry = $dllIndex[$dll.ToLower()]
		if ($entry) {
			Copy-Item $entry.Path $WinDest -Force
			Write-Host "  [dep] $dll"
		} else {
			Write-Warning "  MISSING: $dll — not found in repo (install Visual C++ Redistributable or the relevant SDK)"
		}
	}
} else {
	Write-Warning "  dumpbin.exe not found — skipping automatic dependency resolution"
}

# ── All built outputs from x64\Release (engine DLL, SQLiteKitsune.dll, PDBs) ─
Get-ChildItem $WinOutDir -Filter '*.dll' | Copy-Item -Destination $WinDest -Force
Get-ChildItem $WinOutDir -Filter '*.pdb' | Copy-Item -Destination $WinDest -ErrorAction SilentlyContinue

# ── C# wrapper ────────────────────────────────────────────────────────────────
$csharpBin = Join-Path $RepoRoot "KitsuneNet\bin\Release\$CSharpTfm"
Copy-Item (Join-Path $csharpBin 'KitsuneNet.dll') $WinDest -Force
Copy-Item (Join-Path $csharpBin 'KitsuneNet.pdb') $WinDest -ErrorAction SilentlyContinue

Write-Host ""
Get-ChildItem $WinDest | Sort-Object Name | ForEach-Object { Write-Host "  $($_.Name)" }

# ── 6. Assemble build/linux ───────────────────────────────────────────────────
Write-Step "Assembling build\linux"
New-Item -ItemType Directory -Force -Path $LinDest | Out-Null

$soPath  = "$WslRoot/$LinuxBuildDir/libKitsuneEngine.so"
$linDest = "$WslRoot/build/linux"

# Engine .so
Invoke-Wsl "cp '$soPath' '$linDest/'"

# Resolve shared-library dependencies via ldd.
# SKIP: OS-core (glibc, libpthread, libgcc, libstdc++, ld-linux),
#       libresolv (glibc resolver),
#       and the deep Kerberos / LDAP / SASL / GnuTLS infrastructure chain
#       that is present on any modern Linux server distribution.
# COPY: application-level libraries that are NOT guaranteed to be installed
#       everywhere: MySQL client, PostgreSQL client, Kafka, OpenSSL,
#       zlib, Zstandard, LZ4.
$skipPat = 'libc\.so\.|libm\.so\.|libpthread\.so|libdl\.so\.' +
		   '|librt\.so\.|libgcc_s\.so|libstdc\+\+\.so|libresolv\.so\.' +
		   '|linux-vdso|ld-linux-' +
		   '|libgssapi|libkrb5|libk5crypto|libkrb5support|libcom_err|libkeyutils' +
		   '|libldap|liblber|libsasl2' +
		   '|libgnutls|libp11-kit|libtasn1|libnettle|libhogweed|libgmp\.so|libffi\.so' +
		   '|libidn2|libunistring'

Write-Host "  Resolving libKitsuneEngine.so dependencies via ldd..."
Invoke-Wsl "ldd '$soPath' | awk '/=>/{print `$3}' | grep -Ev '$skipPat' | xargs -rI{} cp --update=none -v {} '$linDest/' 2>&1 || true"

Write-Host ""
Invoke-Wsl "ls -1 '$linDest/'"

# ── 7. Update .gitignore ──────────────────────────────────────────────────────
Write-Step "Updating .gitignore"
Add-GitIgnoreEntry "build/"
Add-GitIgnoreEntry "build-linux-release/"

Write-Host ""
Write-Host "Done.  Release artefacts are in build\windows and build\linux." -ForegroundColor Green
