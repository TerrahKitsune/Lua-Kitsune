# Vendor Dependencies

This directory contains download scripts for third-party prebuilt binaries that are **not** committed to version control.

## llama.cpp

Run `fetch-llama.ps1` to download the prebuilt llama.cpp CUDA binaries:

```powershell
# Default: b9037, CUDA 12.4 (RTX 20/30/40 series)
.\fetch-llama.ps1

# RTX 50 series (Blackwell)
.\fetch-llama.ps1 -CudaVersion 13.1

# Specific release
.\fetch-llama.ps1 -Release b9037 -CudaVersion 12.4
```

After running, copy `vendor\llama.cpp\bin\*.dll` alongside `KitsuneEngine.dll` at runtime.
