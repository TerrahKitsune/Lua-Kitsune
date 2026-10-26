#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "hardware/LuaHardware.h"
#include "Mem.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

// Minimal NVML ABI
// Declared here so the build needs no CUDA toolkit; NVML itself ships with the
// NVIDIA driver and is loaded at runtime (the same library nvidia-smi uses).
typedef int   nvmlReturn_t;
typedef void* nvmlDevice_t;

#define NVML_SUCCESS                 0
#define NVML_ERROR_INSUFFICIENT_SIZE 7
#define NVML_VALUE_NOT_AVAILABLE     (~0ULL)

typedef struct {
    char         busIdLegacy[16];
    unsigned int domain, bus, device, pciDeviceId, pciSubSystemId;
    char         busId[32];
} NvmlPciInfo;

typedef struct { unsigned long long total, free, used; } NvmlMemory;
typedef struct { unsigned int version; unsigned long long total, reserved, free, used; } NvmlMemoryV2;
typedef struct { unsigned long long total, free, used; } NvmlBar1Memory;
typedef struct { unsigned int gpu, memory; } NvmlUtilization;
typedef struct {
    unsigned int       pid;
    unsigned long long usedGpuMemory;
    unsigned int       gpuInstanceId, computeInstanceId;
} NvmlProcessInfo;

#define NVML_MEMORY_V2_VERSION ((unsigned int)(sizeof(NvmlMemoryV2) | (2u << 24)))

typedef nvmlReturn_t (*NvmlGetProcessesFn)(nvmlDevice_t, unsigned int*, NvmlProcessInfo*);

#define NVML_FUNCTIONS(X)                                                                         \
    X(nvmlInit_v2,                                (void))                                         \
    X(nvmlSystemGetDriverVersion,                 (char*, unsigned int))                          \
    X(nvmlSystemGetNVMLVersion,                   (char*, unsigned int))                          \
    X(nvmlSystemGetCudaDriverVersion_v2,          (int*))                                         \
    X(nvmlSystemGetProcessName,                   (unsigned int, char*, unsigned int))            \
    X(nvmlDeviceGetCount_v2,                      (unsigned int*))                                \
    X(nvmlDeviceGetHandleByIndex_v2,              (unsigned int, nvmlDevice_t*))                  \
    X(nvmlDeviceGetName,                          (nvmlDevice_t, char*, unsigned int))            \
    X(nvmlDeviceGetUUID,                          (nvmlDevice_t, char*, unsigned int))            \
    X(nvmlDeviceGetSerial,                        (nvmlDevice_t, char*, unsigned int))            \
    X(nvmlDeviceGetVbiosVersion,                  (nvmlDevice_t, char*, unsigned int))            \
    X(nvmlDeviceGetPciInfo_v3,                    (nvmlDevice_t, NvmlPciInfo*))                   \
    X(nvmlDeviceGetCudaComputeCapability,         (nvmlDevice_t, int*, int*))                     \
    X(nvmlDeviceGetTemperature,                   (nvmlDevice_t, int, unsigned int*))             \
    X(nvmlDeviceGetTemperatureThreshold,          (nvmlDevice_t, int, unsigned int*))             \
    X(nvmlDeviceGetFanSpeed,                      (nvmlDevice_t, unsigned int*))                  \
    X(nvmlDeviceGetPowerUsage,                    (nvmlDevice_t, unsigned int*))                  \
    X(nvmlDeviceGetEnforcedPowerLimit,            (nvmlDevice_t, unsigned int*))                  \
    X(nvmlDeviceGetPowerManagementLimit,          (nvmlDevice_t, unsigned int*))                  \
    X(nvmlDeviceSetPowerManagementLimit,          (nvmlDevice_t, unsigned int))                   \
    X(nvmlDeviceGetPowerManagementDefaultLimit,   (nvmlDevice_t, unsigned int*))                  \
    X(nvmlDeviceGetPowerManagementLimitConstraints, (nvmlDevice_t, unsigned int*, unsigned int*)) \
    X(nvmlDeviceGetTotalEnergyConsumption,        (nvmlDevice_t, unsigned long long*))            \
    X(nvmlDeviceGetPerformanceState,              (nvmlDevice_t, int*))                           \
    X(nvmlDeviceGetMemoryInfo,                    (nvmlDevice_t, NvmlMemory*))                    \
    X(nvmlDeviceGetMemoryInfo_v2,                 (nvmlDevice_t, NvmlMemoryV2*))                  \
    X(nvmlDeviceGetBAR1MemoryInfo,                (nvmlDevice_t, NvmlBar1Memory*))                \
    X(nvmlDeviceGetUtilizationRates,              (nvmlDevice_t, NvmlUtilization*))               \
    X(nvmlDeviceGetEncoderUtilization,            (nvmlDevice_t, unsigned int*, unsigned int*))   \
    X(nvmlDeviceGetDecoderUtilization,            (nvmlDevice_t, unsigned int*, unsigned int*))   \
    X(nvmlDeviceGetClockInfo,                     (nvmlDevice_t, int, unsigned int*))             \
    X(nvmlDeviceGetMaxClockInfo,                  (nvmlDevice_t, int, unsigned int*))             \
    X(nvmlDeviceGetCurrPcieLinkGeneration,        (nvmlDevice_t, unsigned int*))                  \
    X(nvmlDeviceGetMaxPcieLinkGeneration,         (nvmlDevice_t, unsigned int*))                  \
    X(nvmlDeviceGetCurrPcieLinkWidth,             (nvmlDevice_t, unsigned int*))                  \
    X(nvmlDeviceGetMaxPcieLinkWidth,              (nvmlDevice_t, unsigned int*))                  \
    X(nvmlDeviceGetPcieThroughput,                (nvmlDevice_t, int, unsigned int*))             \
    X(nvmlDeviceGetCurrentClocksEventReasons,     (nvmlDevice_t, unsigned long long*))            \
    X(nvmlDeviceGetCurrentClocksThrottleReasons,  (nvmlDevice_t, unsigned long long*))            \
    X(nvmlDeviceGetComputeMode,                   (nvmlDevice_t, int*))                           \
    X(nvmlDeviceGetDriverModel,                   (nvmlDevice_t, int*, int*))                     \
    X(nvmlDeviceGetPersistenceMode,               (nvmlDevice_t, int*))                           \
    X(nvmlDeviceGetDisplayActive,                 (nvmlDevice_t, int*))                           \
    X(nvmlDeviceGetEccMode,                       (nvmlDevice_t, int*, int*))                     \
    X(nvmlDeviceGetComputeRunningProcesses_v3,    (nvmlDevice_t, unsigned int*, NvmlProcessInfo*)) \
    X(nvmlDeviceGetComputeRunningProcesses_v2,    (nvmlDevice_t, unsigned int*, NvmlProcessInfo*)) \
    X(nvmlDeviceGetGraphicsRunningProcesses_v3,   (nvmlDevice_t, unsigned int*, NvmlProcessInfo*)) \
    X(nvmlDeviceGetGraphicsRunningProcesses_v2,   (nvmlDevice_t, unsigned int*, NvmlProcessInfo*))

typedef struct {
#define NVML_DECLARE(name, args) nvmlReturn_t (*name) args;
    NVML_FUNCTIONS(NVML_DECLARE)
#undef NVML_DECLARE
    const char* (*nvmlErrorString)(nvmlReturn_t);
} NvmlApi;

// Every entry point except init/count/handle is optional: older drivers lack
// some symbols and many queries are unsupported on GeForce or under WDDM.
#define NVML_OK(nv, fn, ...) ((nv)->fn && (nv)->fn(__VA_ARGS__) == NVML_SUCCESS)

// Library loading
static void* NvmlOpenLibrary()
{
#ifdef _WIN32
    HMODULE lib = LoadLibraryExA("nvml.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!lib) {
        // Pre-DCH drivers install NVML next to nvidia-smi instead of System32.
        char path[MAX_PATH];
        DWORD n = ExpandEnvironmentStringsA(
            "%ProgramW6432%\\NVIDIA Corporation\\NVSMI\\nvml.dll", path, MAX_PATH);
        if (n > 0 && n <= MAX_PATH)
            lib = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    return (void*)lib;
#else
    return dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
}

static void* NvmlSymbol(void* lib, const char* name)
{
#ifdef _WIN32
    return (void*)GetProcAddress((HMODULE)lib, name);
#else
    return dlsym(lib, name);
#endif
}

static void NvmlCloseLibrary(void* lib)
{
#ifdef _WIN32
    FreeLibrary((HMODULE)lib);
#else
    dlclose(lib);
#endif
}

static NvmlApi g_nvml;

static const NvmlApi* NvmlLoad()
{
    void* lib = NvmlOpenLibrary();
    if (!lib)
        return NULL;

#define NVML_RESOLVE(name, args) g_nvml.name = (decltype(g_nvml.name))NvmlSymbol(lib, #name);
    NVML_FUNCTIONS(NVML_RESOLVE)
#undef NVML_RESOLVE
    g_nvml.nvmlErrorString = (decltype(g_nvml.nvmlErrorString))NvmlSymbol(lib, "nvmlErrorString");

    if (!g_nvml.nvmlInit_v2 || !g_nvml.nvmlDeviceGetCount_v2 ||
        !g_nvml.nvmlDeviceGetHandleByIndex_v2 || g_nvml.nvmlInit_v2() != NVML_SUCCESS) {
        NvmlCloseLibrary(lib);
        return NULL;
    }
    return &g_nvml;
}

// Loaded and initialised once per process and never shut down, like the
// persistent PDH queries: nvmlInit costs far more than any single query.
static const NvmlApi* GetNvmlApi()
{
    static const NvmlApi* api = NvmlLoad();
    return api;
}

// Lua table helpers (set a field on the table at the top of the stack)
static void SetInt(lua_State* L, const char* key, long long v)
{
    lua_pushinteger(L, (lua_Integer)v);
    lua_setfield(L, -2, key);
}

static void SetNum(lua_State* L, const char* key, double v)
{
    lua_pushnumber(L, v);
    lua_setfield(L, -2, key);
}

static void SetStr(lua_State* L, const char* key, const char* v)
{
    lua_pushstring(L, v);
    lua_setfield(L, -2, key);
}

static void SetBool(lua_State* L, const char* key, bool v)
{
    lua_pushboolean(L, v);
    lua_setfield(L, -2, key);
}

static long long BytesToMb(unsigned long long bytes)
{
    return (long long)(bytes / (1024 * 1024));
}

// Per-GPU sections
static void PushIdentity(lua_State* L, const NvmlApi* nv, nvmlDevice_t dev)
{
    char buf[128];
    if (NVML_OK(nv, nvmlDeviceGetName, dev, buf, sizeof(buf)))
        SetStr(L, "Name", buf);
    if (NVML_OK(nv, nvmlDeviceGetUUID, dev, buf, sizeof(buf)))
        SetStr(L, "Uuid", buf);
    if (NVML_OK(nv, nvmlDeviceGetSerial, dev, buf, sizeof(buf)))
        SetStr(L, "Serial", buf);
    if (NVML_OK(nv, nvmlDeviceGetVbiosVersion, dev, buf, sizeof(buf)))
        SetStr(L, "VbiosVersion", buf);

    NvmlPciInfo pci = {};
    if (NVML_OK(nv, nvmlDeviceGetPciInfo_v3, dev, &pci))
        SetStr(L, "PciBusId", pci.busId);

    int major = 0, minor = 0;
    if (NVML_OK(nv, nvmlDeviceGetCudaComputeCapability, dev, &major, &minor)) {
        snprintf(buf, sizeof(buf), "%d.%d", major, minor);
        SetStr(L, "ComputeCapability", buf);
    }
}

static void PushThermal(lua_State* L, const NvmlApi* nv, nvmlDevice_t dev)
{
    enum { SENSOR_GPU = 0 };
    enum { THRESHOLD_SHUTDOWN = 0, THRESHOLD_SLOWDOWN = 1, THRESHOLD_GPU_MAX = 3 };

    unsigned int v = 0;
    if (NVML_OK(nv, nvmlDeviceGetTemperature, dev, SENSOR_GPU, &v))
        SetInt(L, "TemperatureC", v);
    if (NVML_OK(nv, nvmlDeviceGetTemperatureThreshold, dev, THRESHOLD_SLOWDOWN, &v))
        SetInt(L, "TemperatureSlowdownC", v);
    if (NVML_OK(nv, nvmlDeviceGetTemperatureThreshold, dev, THRESHOLD_SHUTDOWN, &v))
        SetInt(L, "TemperatureShutdownC", v);
    if (NVML_OK(nv, nvmlDeviceGetTemperatureThreshold, dev, THRESHOLD_GPU_MAX, &v))
        SetInt(L, "TemperatureMaxOperatingC", v);
    if (NVML_OK(nv, nvmlDeviceGetFanSpeed, dev, &v))
        SetInt(L, "FanPercent", v);
}

static void PushPower(lua_State* L, const NvmlApi* nv, nvmlDevice_t dev)
{
    unsigned int mw = 0, minMw = 0, maxMw = 0;
    if (NVML_OK(nv, nvmlDeviceGetPowerUsage, dev, &mw))
        SetNum(L, "PowerDrawW", mw / 1000.0);
    if (NVML_OK(nv, nvmlDeviceGetEnforcedPowerLimit, dev, &mw))
        SetNum(L, "PowerLimitW", mw / 1000.0);
    if (NVML_OK(nv, nvmlDeviceGetPowerManagementLimit, dev, &mw))
        SetNum(L, "PowerManagementLimitW", mw / 1000.0);
    if (NVML_OK(nv, nvmlDeviceGetPowerManagementDefaultLimit, dev, &mw))
        SetNum(L, "PowerDefaultLimitW", mw / 1000.0);
    if (NVML_OK(nv, nvmlDeviceGetPowerManagementLimitConstraints, dev, &minMw, &maxMw)) {
        SetNum(L, "PowerMinLimitW", minMw / 1000.0);
        SetNum(L, "PowerMaxLimitW", maxMw / 1000.0);
    }

    unsigned long long mj = 0;
    if (NVML_OK(nv, nvmlDeviceGetTotalEnergyConsumption, dev, &mj))
        SetNum(L, "EnergyJ", mj / 1000.0);

    enum { PSTATE_UNKNOWN = 32 };
    int pstate = PSTATE_UNKNOWN;
    if (NVML_OK(nv, nvmlDeviceGetPerformanceState, dev, &pstate) && pstate != PSTATE_UNKNOWN) {
        char buf[8];
        snprintf(buf, sizeof(buf), "P%d", pstate);
        SetStr(L, "PerformanceState", buf);
    }
}

static void PushMemory(lua_State* L, const NvmlApi* nv, nvmlDevice_t dev)
{
    NvmlMemoryV2 mem2 = {};
    mem2.version = NVML_MEMORY_V2_VERSION;
    NvmlMemory mem = {};
    if (NVML_OK(nv, nvmlDeviceGetMemoryInfo_v2, dev, &mem2)) {
        SetInt(L, "MemoryTotalMB",    BytesToMb(mem2.total));
        SetInt(L, "MemoryReservedMB", BytesToMb(mem2.reserved));
        SetInt(L, "MemoryUsedMB",     BytesToMb(mem2.used));
        SetInt(L, "MemoryFreeMB",     BytesToMb(mem2.free));
    }
    else if (NVML_OK(nv, nvmlDeviceGetMemoryInfo, dev, &mem)) {
        SetInt(L, "MemoryTotalMB", BytesToMb(mem.total));
        SetInt(L, "MemoryUsedMB",  BytesToMb(mem.used));
        SetInt(L, "MemoryFreeMB",  BytesToMb(mem.free));
    }

    NvmlBar1Memory bar1 = {};
    if (NVML_OK(nv, nvmlDeviceGetBAR1MemoryInfo, dev, &bar1)) {
        SetInt(L, "Bar1TotalMB", BytesToMb(bar1.total));
        SetInt(L, "Bar1UsedMB",  BytesToMb(bar1.used));
    }
}

static void PushUtilization(lua_State* L, const NvmlApi* nv, nvmlDevice_t dev)
{
    NvmlUtilization util = {};
    if (NVML_OK(nv, nvmlDeviceGetUtilizationRates, dev, &util)) {
        SetInt(L, "GpuUtilPercent",    util.gpu);
        SetInt(L, "MemoryUtilPercent", util.memory);
    }

    unsigned int pct = 0, periodUs = 0;
    if (NVML_OK(nv, nvmlDeviceGetEncoderUtilization, dev, &pct, &periodUs))
        SetInt(L, "EncoderUtilPercent", pct);
    if (NVML_OK(nv, nvmlDeviceGetDecoderUtilization, dev, &pct, &periodUs))
        SetInt(L, "DecoderUtilPercent", pct);
}

typedef nvmlReturn_t (*NvmlGetClockFn)(nvmlDevice_t, int, unsigned int*);

// Pushes a { Graphics, SM, Memory, Video } table of MHz values as `key`.
static void PushClockTable(lua_State* L, const char* key, NvmlGetClockFn fn, nvmlDevice_t dev)
{
    if (!fn)
        return;

    static const char* const names[] = { "Graphics", "SM", "Memory", "Video" };
    lua_newtable(L);
    for (int type = 0; type < 4; type++) {
        unsigned int mhz = 0;
        if (fn(dev, type, &mhz) == NVML_SUCCESS)
            SetInt(L, names[type], mhz);
    }
    lua_setfield(L, -2, key);
}

static void PushPcie(lua_State* L, const NvmlApi* nv, nvmlDevice_t dev)
{
    enum { PCIE_TX = 0, PCIE_RX = 1 };

    unsigned int v = 0;
    if (NVML_OK(nv, nvmlDeviceGetCurrPcieLinkGeneration, dev, &v))
        SetInt(L, "PcieGen", v);
    if (NVML_OK(nv, nvmlDeviceGetMaxPcieLinkGeneration, dev, &v))
        SetInt(L, "PcieGenMax", v);
    if (NVML_OK(nv, nvmlDeviceGetCurrPcieLinkWidth, dev, &v))
        SetInt(L, "PcieWidth", v);
    if (NVML_OK(nv, nvmlDeviceGetMaxPcieLinkWidth, dev, &v))
        SetInt(L, "PcieWidthMax", v);
    if (NVML_OK(nv, nvmlDeviceGetPcieThroughput, dev, PCIE_TX, &v))
        SetInt(L, "PcieTxKBps", v);
    if (NVML_OK(nv, nvmlDeviceGetPcieThroughput, dev, PCIE_RX, &v))
        SetInt(L, "PcieRxKBps", v);
}

static void PushThrottleReasons(lua_State* L, const NvmlApi* nv, nvmlDevice_t dev)
{
    static const struct { unsigned long long bit; const char* name; } reasons[] = {
        { 0x001, "GpuIdle"                   },
        { 0x002, "ApplicationsClocksSetting" },
        { 0x004, "SwPowerCap"                },
        { 0x008, "HwSlowdown"                },
        { 0x010, "SyncBoost"                 },
        { 0x020, "SwThermalSlowdown"         },
        { 0x040, "HwThermalSlowdown"         },
        { 0x080, "HwPowerBrakeSlowdown"      },
        { 0x100, "DisplayClockSetting"       },
        { 0x400, "Reliability"               },
    };

    unsigned long long mask = 0;
    if (!NVML_OK(nv, nvmlDeviceGetCurrentClocksEventReasons, dev, &mask) &&
        !NVML_OK(nv, nvmlDeviceGetCurrentClocksThrottleReasons, dev, &mask))
        return;

    lua_newtable(L);
    int n = 0;
    for (size_t i = 0; i < sizeof(reasons) / sizeof(reasons[0]); i++) {
        if (mask & reasons[i].bit) {
            lua_pushstring(L, reasons[i].name);
            lua_rawseti(L, -2, ++n);
            mask &= ~reasons[i].bit;
        }
    }
    // Newer drivers add reasons; report them by bit rather than dropping them.
    for (int bit = 0; bit < 64; bit++) {
        if (mask & (1ULL << bit)) {
            char hex[24];
            snprintf(hex, sizeof(hex), "0x%llx", 1ULL << bit);
            lua_pushstring(L, hex);
            lua_rawseti(L, -2, ++n);
        }
    }
    lua_setfield(L, -2, "ThrottleReasons");
}

static void PushModes(lua_State* L, const NvmlApi* nv, nvmlDevice_t dev)
{
    static const char* const computeModes[] = {
        "Default", "Exclusive_Thread", "Prohibited", "Exclusive_Process"
    };
    static const char* const driverModels[] = { "WDDM", "TCC", "MCDM" };

    int mode = 0, pending = 0;
    if (NVML_OK(nv, nvmlDeviceGetComputeMode, dev, &mode) && mode >= 0 && mode < 4)
        SetStr(L, "ComputeMode", computeModes[mode]);
    if (NVML_OK(nv, nvmlDeviceGetDriverModel, dev, &mode, &pending) && mode >= 0 && mode < 3)
        SetStr(L, "DriverModel", driverModels[mode]);
    if (NVML_OK(nv, nvmlDeviceGetPersistenceMode, dev, &mode))
        SetBool(L, "PersistenceMode", mode != 0);
    if (NVML_OK(nv, nvmlDeviceGetDisplayActive, dev, &mode))
        SetBool(L, "DisplayActive", mode != 0);
    if (NVML_OK(nv, nvmlDeviceGetEccMode, dev, &mode, &pending))
        SetBool(L, "EccEnabled", mode != 0);
}

// Processes

// Returns a kitsune_calloc'd list (caller frees) or NULL when there are none.
static NvmlProcessInfo* NvmlGetProcesses(NvmlGetProcessesFn fn, nvmlDevice_t dev, unsigned int* count)
{
    *count = 0;
    if (!fn)
        return NULL;

    // Retry: processes can start between the sizing call and the fetch.
    for (int attempt = 0; attempt < 3; attempt++) {
        unsigned int n = 0;
        if (fn(dev, &n, NULL) != NVML_ERROR_INSUFFICIENT_SIZE)
            return NULL;
        n += 8;
        NvmlProcessInfo* list = (NvmlProcessInfo*)kitsune_calloc(n, sizeof(NvmlProcessInfo));
        if (!list)
            return NULL;
        nvmlReturn_t r = fn(dev, &n, list);
        if (r == NVML_SUCCESS) {
            *count = n;
            return list;
        }
        kitsune_free(list);
        if (r != NVML_ERROR_INSUFFICIENT_SIZE)
            return NULL;
    }
    return NULL;
}

typedef struct {
    unsigned int       pid;
    unsigned long long usedMemory;
    bool               compute;
    bool               graphics;
} GpuProcess;

static void MergeProcesses(GpuProcess* merged, int* nMerged,
                            const NvmlProcessInfo* list, unsigned int n, bool compute)
{
    for (unsigned int i = 0; i < n; i++) {
        GpuProcess* p = NULL;
        for (int j = 0; j < *nMerged; j++) {
            if (merged[j].pid == list[i].pid) {
                p = &merged[j];
                break;
            }
        }
        if (!p) {
            p = &merged[(*nMerged)++];
            p->pid = list[i].pid;
            p->usedMemory = NVML_VALUE_NOT_AVAILABLE;
        }
        if (list[i].usedGpuMemory != NVML_VALUE_NOT_AVAILABLE &&
            (p->usedMemory == NVML_VALUE_NOT_AVAILABLE || list[i].usedGpuMemory > p->usedMemory))
            p->usedMemory = list[i].usedGpuMemory;
        if (compute)
            p->compute = true;
        else
            p->graphics = true;
    }
}

// Pushes Processes = { {Pid, Name, Type = "C"|"G"|"C+G", UsedMemoryMB}, ... },
// the same merged view as the process table at the bottom of nvidia-smi.
static void PushProcesses(lua_State* L, const NvmlApi* nv, nvmlDevice_t dev)
{
    NvmlGetProcessesFn computeFn = nv->nvmlDeviceGetComputeRunningProcesses_v3
        ? nv->nvmlDeviceGetComputeRunningProcesses_v3 : nv->nvmlDeviceGetComputeRunningProcesses_v2;
    NvmlGetProcessesFn graphicsFn = nv->nvmlDeviceGetGraphicsRunningProcesses_v3
        ? nv->nvmlDeviceGetGraphicsRunningProcesses_v3 : nv->nvmlDeviceGetGraphicsRunningProcesses_v2;

    unsigned int nCompute = 0, nGraphics = 0;
    NvmlProcessInfo* compute  = NvmlGetProcesses(computeFn,  dev, &nCompute);
    NvmlProcessInfo* graphics = NvmlGetProcesses(graphicsFn, dev, &nGraphics);

    lua_newtable(L);
    GpuProcess* merged = (GpuProcess*)kitsune_calloc((size_t)nCompute + nGraphics + 1, sizeof(GpuProcess));
    if (merged) {
        int n = 0;
        MergeProcesses(merged, &n, compute,  nCompute,  true);
        MergeProcesses(merged, &n, graphics, nGraphics, false);

        for (int i = 0; i < n; i++) {
            lua_newtable(L);
            SetInt(L, "Pid", merged[i].pid);

            char name[512];
            if (NVML_OK(nv, nvmlSystemGetProcessName, merged[i].pid, name, sizeof(name)))
                SetStr(L, "Name", name);

            SetStr(L, "Type", merged[i].compute && merged[i].graphics ? "C+G"
                             : merged[i].compute ? "C" : "G");
            if (merged[i].usedMemory != NVML_VALUE_NOT_AVAILABLE)
                SetInt(L, "UsedMemoryMB", BytesToMb(merged[i].usedMemory));

            lua_rawseti(L, -2, i + 1);
        }
        kitsune_free(merged);
    }
    lua_setfield(L, -2, "Processes");

    kitsune_free(compute);
    kitsune_free(graphics);
}

static void PushGpu(lua_State* L, const NvmlApi* nv, unsigned int index, nvmlDevice_t dev)
{
    lua_newtable(L);
    SetInt(L, "Index", index);
    PushIdentity(L, nv, dev);
    PushThermal(L, nv, dev);
    PushPower(L, nv, dev);
    PushMemory(L, nv, dev);
    PushUtilization(L, nv, dev);
    PushClockTable(L, "Clocks",    nv->nvmlDeviceGetClockInfo,    dev);
    PushClockTable(L, "MaxClocks", nv->nvmlDeviceGetMaxClockInfo, dev);
    PushPcie(L, nv, dev);
    PushThrottleReasons(L, nv, dev);
    PushModes(L, nv, dev);
    PushProcesses(L, nv, dev);
}

int Hardware_NvidiaSmi(lua_State* L)
{
    const NvmlApi* nv = GetNvmlApi();
    unsigned int count = 0;
    if (!nv || nv->nvmlDeviceGetCount_v2(&count) != NVML_SUCCESS) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);

    char buf[96];
    if (NVML_OK(nv, nvmlSystemGetDriverVersion, buf, sizeof(buf)))
        SetStr(L, "DriverVersion", buf);
    if (NVML_OK(nv, nvmlSystemGetNVMLVersion, buf, sizeof(buf)))
        SetStr(L, "NvmlVersion", buf);
    int cuda = 0;
    if (NVML_OK(nv, nvmlSystemGetCudaDriverVersion_v2, &cuda)) {
        snprintf(buf, sizeof(buf), "%d.%d", cuda / 1000, (cuda % 1000) / 10);
        SetStr(L, "CudaVersion", buf);
    }

    lua_createtable(L, (int)count, 0);
    int n = 0;
    for (unsigned int i = 0; i < count; i++) {
        nvmlDevice_t dev = NULL;
        if (nv->nvmlDeviceGetHandleByIndex_v2(i, &dev) != NVML_SUCCESS)
            continue;
        PushGpu(L, nv, i, dev);
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, "Gpus");

    return 1;
}

// Equivalent of `nvidia-smi -i <index> -pl <watts>`. Returns true, or nil plus
// NVML's error message. Needs admin/root; the limit lasts until reboot or driver reload.
int Hardware_NvidiaSetPowerLimit(lua_State* L)
{
    lua_Integer index = luaL_checkinteger(L, 1);
    lua_Number watts = luaL_checknumber(L, 2);
    luaL_argcheck(L, index >= 0, 1, "GPU index must be >= 0");
    luaL_argcheck(L, watts > 0 && watts < 100000, 2, "watts out of range");

    const NvmlApi* nv = GetNvmlApi();
    if (!nv || !nv->nvmlDeviceSetPowerManagementLimit) {
        lua_pushnil(L);
        lua_pushstring(L, "NVML not available");
        return 2;
    }

    nvmlDevice_t dev = NULL;
    nvmlReturn_t rc = nv->nvmlDeviceGetHandleByIndex_v2((unsigned int)index, &dev);
    if (rc == NVML_SUCCESS)
        rc = nv->nvmlDeviceSetPowerManagementLimit(dev, (unsigned int)(watts * 1000.0 + 0.5));

    if (rc != NVML_SUCCESS) {
        lua_pushnil(L);
        if (nv->nvmlErrorString)
            lua_pushstring(L, nv->nvmlErrorString(rc));
        else
            lua_pushfstring(L, "NVML error %d", rc);
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}
