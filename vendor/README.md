# vendor/

This directory contains third-party prebuilt binaries fetched at build time.
Contents are excluded from version control via `.gitignore`.

---

## llama.cpp

**Location:** `vendor\llama.cpp\`  
**Source:** https://github.com/ggml-org/llama.cpp/releases

### Setup

Run the fetch script from the repo root before building with `KITSUNE_LLAMA=ON`:

```powershell
powershell -ExecutionPolicy Bypass -File vendor\fetch-llama.ps1
```

This downloads the prebuilt CUDA 12.4 package (RTX 20/30/40xx). For RTX 50xx (Blackwell):

```powershell
powershell -ExecutionPolicy Bypass -File vendor\fetch-llama.ps1 -CudaVersion 13.1
```

To pin a specific release:

```powershell
powershell -ExecutionPolicy Bypass -File vendor\fetch-llama.ps1 -Release b9037 -CudaVersion 12.4
```

### Output layout

```
vendor\llama.cpp\
  include\    <- llama.h, ggml.h, ... (included at compile time)
  lib\        <- llama.lib, ggml.lib  (linked at compile time)
  bin\        <- llama.dll, ggml.dll, ggml-cuda.dll, cudart64_12.dll, ...
```

### Runtime requirement

All DLLs in `vendor\llama.cpp\bin\` must be present alongside `KitsuneEngine.dll`
at runtime. Copy or symlink them to your output directory as part of your build process.

### CUDA version guide

| Version | Cards |
|---|---|
| `12.4` (default) | RTX 20xx, 30xx, 40xx |
| `13.1` | RTX 50xx (Blackwell) |

Swapping CUDA versions is a DLL-only change — no recompilation needed.
