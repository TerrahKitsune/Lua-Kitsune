# fetch-test-model.ps1
# Downloads the Qwen3-0.6B Q8_0 GGUF test model from HuggingFace (official Qwen repo).
# Output: tests/models/qwen3-0.6b-q8_0.gguf
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tests/fetch-test-model.ps1
#
# The model (~630 MB) supports reasoning (<think> tags) and tool calling.
# No authentication required — hosted on the official Qwen org repo.
# The tests/models/ directory is git-ignored.

$ModelDir  = Join-Path $PSScriptRoot "models"
$ModelFile = Join-Path $ModelDir "qwen3-0.6b-q8_0.gguf"
$ModelUrl  = "https://huggingface.co/Qwen/Qwen3-0.6B-GGUF/resolve/main/Qwen3-0.6B-Q8_0.gguf"

if (Test-Path $ModelFile) {
    $size = (Get-Item $ModelFile).Length / 1MB
    Write-Host ("Model already present: $ModelFile ({0:F1} MB)" -f $size)
    exit 0
}

if (-not (Test-Path $ModelDir)) {
    New-Item -ItemType Directory -Path $ModelDir | Out-Null
}

Write-Host "Downloading Qwen3-0.6B Q8_0 (~630 MB)..."
Write-Host "  From: $ModelUrl"
Write-Host "  To:   $ModelFile"

# curl.exe (built into Windows 10+) handles HuggingFace CDN redirects correctly.
$curlCmd = Get-Command curl.exe -ErrorAction SilentlyContinue
$curlPath = if ($curlCmd) { $curlCmd.Source } else { "curl.exe" }

& $curlPath -L --progress-bar -o $ModelFile $ModelUrl

if (-not (Test-Path $ModelFile) -or (Get-Item $ModelFile).Length -lt 1MB) {
    Write-Error "Download failed or file is too small. Check your network connection."
    Remove-Item $ModelFile -ErrorAction SilentlyContinue
    exit 1
}

$size = (Get-Item $ModelFile).Length / 1MB
Write-Host ("Downloaded: {0:F1} MB" -f $size)
Write-Host "Model ready at: $ModelFile"
