#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "LuaHardware.h"
#include "platform.h"
#include "mem.h"
#include <stdio.h>
#include <string.h>

// ─────────────────────────────────────────────────────────────────────────────
// Windows implementation
// ─────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <intrin.h>
#include <Pdh.h>
#include <PdhMsg.h>
#include <dxgi.h>
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "dxgi.lib")

// ── PDH helper: expand a wildcard counter path into all instance paths ────────
// Returns an array of null-terminated strings in *out (caller must free).
// *count is set to the number of strings found.
static PDH_STATUS pdh_expand_wildcards(const char* path, char** out, DWORD* count)
{
    DWORD bufsz = 0;
    PDH_STATUS st = PdhExpandCounterPathA(path, NULL, &bufsz);
    if (st != PDH_MORE_DATA && st != ERROR_SUCCESS)
        return st;
    char* buf = (char*)kitsune_malloc(bufsz);
    if (!buf)
        return PDH_MEMORY_ALLOCATION_FAILURE;
    st = PdhExpandCounterPathA(path, buf, &bufsz);
    if (st != ERROR_SUCCESS) {
        kitsune_free(buf);
        return st;
    }
    *out = buf;
    // Count double-null-terminated list entries
    DWORD n = 0;
    const char* p = buf;
    while (*p) {
        n++;
        p += strlen(p) + 1;
    }
    *count = n;
    return ERROR_SUCCESS;
}

// ── PDH: collect one double value per instance for a wildcard counter ─────────
// Returns array of {name, value} pairs; caller must free.
// On failure returns 0.
typedef struct { char name[256]; double value; } PdhSample;

// Snapshot counter (e.g. Temperature): one collect, no sleep.
static int pdh_sample_snapshot(const char* wildcard, PdhSample** samplesOut, int* nOut)
{
    char* paths = NULL;
    DWORD count = 0;
    if (pdh_expand_wildcards(wildcard, &paths, &count) != ERROR_SUCCESS || count == 0)
        return 0;

    PdhSample* samples = (PdhSample*)kitsune_calloc(count, sizeof(PdhSample));
    if (!samples) {
        kitsune_free(paths);
        return 0;
    }

    PDH_HQUERY q = NULL;
    if (PdhOpenQuery(NULL, 0, &q) != ERROR_SUCCESS) {
        kitsune_free(paths);
        kitsune_free(samples);
        return 0;
    }

    PDH_HCOUNTER* ctrs = (PDH_HCOUNTER*)kitsune_calloc(count, sizeof(PDH_HCOUNTER));
    if (!ctrs) {
        PdhCloseQuery(q);
        kitsune_free(paths);
        kitsune_free(samples);
        return 0;
    }

    const char* p = paths;
    for (DWORD i = 0; i < count; i++) {
        const char* open  = strchr(p, '(');
        const char* close = open ? strchr(open, ')') : NULL;
        if (open && close && (close - open - 1) < 255) {
            strncpy(samples[i].name, open + 1, (size_t)(close - open - 1));
            samples[i].name[close - open - 1] = '\0';
        }
        else {
            strncpy(samples[i].name, p, 255);
        }
        PdhAddCounterA(q, p, 0, &ctrs[i]);
        p += strlen(p) + 1;
    }

    // Single collect — snapshot counters don't need a baseline interval.
    PdhCollectQueryData(q);

    int valid = 0;
    for (DWORD i = 0; i < count; i++) {
        PDH_FMT_COUNTERVALUE val = {};
        if (PdhGetFormattedCounterValue(ctrs[i], PDH_FMT_DOUBLE, NULL, &val) == ERROR_SUCCESS) {
            samples[valid] = samples[i];
            samples[valid].value = val.doubleValue;
            valid++;
        }
    }

    kitsune_free(ctrs);
    PdhCloseQuery(q);
    kitsune_free(paths);

    *samplesOut = samples;
    *nOut = valid;
    return valid > 0;
}

// Persistent per-core CPU load query — opened once, reused every call.
// Rate-based counters need two collects separated by time; by keeping the
// query alive we use the OS's own measurement window instead of sleeping.
typedef struct { char name[256]; PDH_HCOUNTER ctr; } PdhCoreEntry;

static PdhSample* pdh_collect_core_load(int* nOut)
{
    static PDH_HQUERY  s_query   = NULL;
    static PdhCoreEntry* s_cores = NULL;
    static int         s_count   = 0;
    static bool        s_ready   = false;

    // One-time initialisation: expand the wildcard and add all per-core counters.
    if (!s_ready) {
        char* paths = NULL;
        DWORD count = 0;
        if (pdh_expand_wildcards(
                "\\Processor Information(*)\\% Processor Time",
                &paths, &count) != ERROR_SUCCESS || count == 0)
            return NULL;

        if (PdhOpenQuery(NULL, 0, &s_query) != ERROR_SUCCESS) {
            kitsune_free(paths);
            return NULL;
        }

        s_cores = (PdhCoreEntry*)kitsune_calloc(count, sizeof(PdhCoreEntry));
        if (!s_cores) {
            PdhCloseQuery(s_query);
            s_query = NULL;
            kitsune_free(paths);
            return NULL;
        }

        const char* p = paths;
        int kept = 0;
        for (DWORD i = 0; i < count; i++) {
            // Skip _Total aggregates (AMD uses "0,_Total", Intel uses "_Total")
            if (strstr(p, "_Total")) {
                p += strlen(p) + 1;
                continue;
            }
            const char* open  = strchr(p, '(');
            const char* close = open ? strchr(open, ')') : NULL;
            if (open && close && (close - open - 1) < 255) {
                strncpy(s_cores[kept].name, open + 1, (size_t)(close - open - 1));
                s_cores[kept].name[close - open - 1] = '\0';
            }
            else {
                strncpy(s_cores[kept].name, p, 255);
            }
            PdhAddCounterA(s_query, p, 0, &s_cores[kept].ctr);
            kept++;
            p += strlen(p) + 1;
        }
        kitsune_free(paths);
        s_count = kept;

        // Prime the baseline
        PdhCollectQueryData(s_query);
        s_ready = true;

        // s_cores is a process-lifetime allocation (persistent query).
        // Mark it permanent so the leak checker doesn't flag it on engine teardown.
        kitsune_snapshot_permanent_allocs();
    }

    // Collect a fresh sample and read each counter's current value.
    PdhCollectQueryData(s_query);

    PdhSample* out = (PdhSample*)kitsune_calloc((size_t)s_count, sizeof(PdhSample));
    if (!out)
        return NULL;

    int valid = 0;
    for (int i = 0; i < s_count; i++) {
        PDH_FMT_COUNTERVALUE val = {};
        if (PdhGetFormattedCounterValue(s_cores[i].ctr, PDH_FMT_DOUBLE, NULL, &val) == ERROR_SUCCESS) {
            strncpy(out[valid].name, s_cores[i].name, 255);
            out[valid].value = val.doubleValue;
            valid++;
        }
    }

    *nOut = valid;
    return out;
}

// ── Convert a raw PDH thermal zone value (tenths of Kelvin) to Celsius ────────
// Both "Temperature" and "High Precision Temperature" counters use this unit.
// Returns false when the value is below 200 K (uninitialized / zero / garbage).
static bool pdh_kelvin_to_celsius(double raw, double* out)
{
    if (raw < 2000.0)
        return false;
    *out = (raw / 10.0) - 273.15;
    return true;
}

// ── Collect all valid thermal zone samples from PDH ───────────────────────────
// Tries "High Precision Temperature" first (wider hardware support), then falls
// back to "Temperature". Both counters are snapshots — one collect, no sleep.
// Returns heap-allocated samples that the caller must free(); *nOut is the
// number of valid (>= 200 K) entries already filtered and converted to Celsius.
static PdhSample* pdh_collect_thermal_zones(int* nOut)
{
    static const char* counters[] = {
        "\\Thermal Zone Information(*)\\High Precision Temperature",
        "\\Thermal Zone Information(*)\\Temperature",
        NULL
    };

    for (int c = 0; counters[c]; c++) {
        PdhSample* raw = NULL;
        int n = 0;
        if (!pdh_sample_snapshot(counters[c], &raw, &n) || n == 0) {
            free(raw);
            continue;
        }

        int valid = 0;
        for (int i = 0; i < n; i++) {
            double celsius;
            if (pdh_kelvin_to_celsius(raw[i].value, &celsius)) {
                raw[valid] = raw[i];
                raw[valid].value = celsius;
                valid++;
            }
        }

        if (valid > 0) {
            *nOut = valid;
            return raw;
        }
        kitsune_free(raw);
    }

    *nOut = 0;
    return NULL;
}

// ── CPU temperature ───────────────────────────────────────────────────────────
// Returns an array of tables: { {Name="TZ00", Value=46.8}, ... }
// Returns nil when no valid thermal zones are found.
int hardware_cpu_temp(lua_State* L)
{
    int n = 0;
    PdhSample* samples = pdh_collect_thermal_zones(&n);

    if (!samples || n == 0) {
        kitsune_free(samples);
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    for (int i = 0; i < n; i++) {
        lua_newtable(L);
        lua_pushstring(L, samples[i].name);
        lua_setfield(L, -2, "Name");
        lua_pushnumber(L, samples[i].value);
        lua_setfield(L, -2, "Value");
        lua_rawseti(L, -2, i + 1);
    }

    kitsune_free(samples);
    return 1;
}

// ── Per-hardware-thread CPU load
// Returns {["0,0"] = 12.5, ["0,1"] = 8.0, ...} — persistent PDH query,
// no sleep needed between calls.
int hardware_cpu_threads_load(lua_State* L)
{
    int n = 0;
    PdhSample* samples = pdh_collect_core_load(&n);

    lua_newtable(L);
    if (samples) {
        for (int i = 0; i < n; i++) {
            lua_pushstring(L, samples[i].name);
            lua_pushnumber(L, samples[i].value);
            lua_settable(L, -3);
        }
        kitsune_free(samples);
    }

    return 1;
}

// ── CPU load via PDH
int hardware_cpu_load(lua_State* L)
{
    static PDH_HQUERY query = NULL;
    static PDH_HCOUNTER counter = NULL;
    static bool ready = false;

    if (!ready) {
        if (PdhOpenQuery(NULL, 0, &query) != ERROR_SUCCESS) {
            lua_pushnil(L);
            return 1;
        }
        if (PdhAddEnglishCounterA(query, "\\Processor(_Total)\\% Processor Time", 0, &counter) != ERROR_SUCCESS) {
            PdhCloseQuery(query);
            query = NULL;
            lua_pushnil(L);
            return 1;
        }
        PdhCollectQueryData(query);
        ready = true;
    }

    PdhCollectQueryData(query);

    PDH_FMT_COUNTERVALUE val = {};
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, NULL, &val) != ERROR_SUCCESS) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushnumber(L, val.doubleValue);
    return 1;
}

// ── Memory info ───────────────────────────────────────────────────────────────
int hardware_memory(lua_State* L)
{
    MEMORYSTATUSEX ms = {};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);

    lua_pushinteger(L, (lua_Integer)(ms.ullTotalPhys / (1024 * 1024)));
    lua_setfield(L, -2, "TotalPhys");

    lua_pushinteger(L, (lua_Integer)(ms.ullAvailPhys / (1024 * 1024)));
    lua_setfield(L, -2, "AvailPhys");

    lua_pushinteger(L, (lua_Integer)(ms.ullTotalPageFile / (1024 * 1024)));
    lua_setfield(L, -2, "TotalSwap");

    lua_pushinteger(L, (lua_Integer)(ms.ullAvailPageFile / (1024 * 1024)));
    lua_setfield(L, -2, "AvailSwap");

    lua_pushinteger(L, (lua_Integer)ms.dwMemoryLoad);
    lua_setfield(L, -2, "LoadPercent");

    return 1;
}

// ── CPU brand string via CPUID ────────────────────────────────────────────────
int hardware_cpu_name(lua_State* L)
{
    char brand[49] = {};
    int info[4] = {};
    __cpuid(info, 0x80000000);
    if ((unsigned int)info[0] < 0x80000004U) {
        lua_pushnil(L);
        return 1;
    }
    __cpuid(info, 0x80000002);
    memcpy(brand,      info, 16);
    __cpuid(info, 0x80000003);
    memcpy(brand + 16, info, 16);
    __cpuid(info, 0x80000004);
    memcpy(brand + 32, info, 16);

    const char* p = brand;
    while (*p == ' ')
        p++;

    lua_pushstring(L, p);
    return 1;
}

// ── Battery info via GetSystemPowerStatus ─────────────────────────────────────
// Returns a table: { Percent, Charging, ACLine, TimeRemaining }
// or nil if no battery is present.
int hardware_battery(lua_State* L)
{
    SYSTEM_POWER_STATUS sps = {};
    if (!GetSystemPowerStatus(&sps)) {
        lua_pushnil(L);
        return 1;
    }

    // BatteryFlag 128 = no battery present
    if (sps.BatteryFlag == 128) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);

    // Percent: 255 = unknown
    if (sps.BatteryLifePercent != 255) {
        lua_pushinteger(L, sps.BatteryLifePercent);
        lua_setfield(L, -2, "Percent");
    }
    else {
        lua_pushnil(L);
        lua_setfield(L, -2, "Percent");
    }

    // ACLine: 0=battery, 1=AC, 255=unknown
    lua_pushboolean(L, sps.ACLineStatus == 1);
    lua_setfield(L, -2, "ACLine");

    // Charging flag (bit 8)
    lua_pushboolean(L, (sps.BatteryFlag & 8) != 0);
    lua_setfield(L, -2, "Charging");

    // Seconds remaining: -1 = unknown
    if (sps.BatteryLifeTime != (DWORD)-1) {
        lua_pushinteger(L, (lua_Integer)sps.BatteryLifeTime);
        lua_setfield(L, -2, "SecondsRemaining");
    }
    else {
        lua_pushnil(L);
        lua_setfield(L, -2, "SecondsRemaining");
    }

    return 1;
}

// ── GPU helpers ───────────────────────────────────────────────────────────────

// Parse the luid hex pair out of a PDH instance name like:
//   luid_0x00000000_0x000172E4_phys_0
//   pid_1234_luid_0x00000000_0x000172E4_phys_0_eng_0_engtype_3d
// Writes the canonical "0x00000000_0x000172E4" string into out (must be >= 32).
// Returns true on success.
static bool pdh_parse_luid(const char* instance, char* out, size_t outsz)
{
    const char* p = strstr(instance, "luid_");
    if (!p)
        return false;
    p += 5; // skip "luid_"
    // Expect two 0x... hex tokens separated by '_'
    const char* hi = p;
    const char* sep = strchr(hi, '_');
    if (!sep)
        return false;
    const char* lo = sep + 1;
    // Find end of lo token (next '_' or '\0')
    const char* end = strchr(lo, '_');
    size_t hilen = (size_t)(sep - hi);
    size_t lolen = end ? (size_t)(end - lo) : strlen(lo);
    if (hilen + 1 + lolen + 1 > outsz)
        return false;
    memcpy(out, hi, hilen);
    out[hilen] = '_';
    memcpy(out + hilen + 1, lo, lolen);
    out[hilen + 1 + lolen] = '\0';
    return true;
}

// Parse the engtype from a GPU Engine instance name, e.g.
//   pid_..._engtype_3d  →  "3d"
//   pid_..._engtype_copy  →  "copy"
// Writes into out (must be >= 64). Returns true on success.
static bool pdh_parse_engtype(const char* instance, char* out, size_t outsz)
{
    const char* p = strstr(instance, "engtype_");
    if (!p)
        return false;
    p += 8; // skip "engtype_"
    // engtype runs to ')' or end of string
    const char* end = strchr(p, ')');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len + 1 > outsz)
        return false;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

// Map a LUID string "0x00000000_0xNNNNNNNN" to a friendly adapter name via
// EnumDisplayDevices + DXGI.  Falls back to the luid string itself on failure.
// The LUID from PDH is the DXGI adapter LUID split into high/low 32-bit words.
static void pdh_luid_to_adapter_name(const char* luid_str, char* out, size_t outsz)
{
    // Parse the two hex words
    unsigned long hi = 0, lo = 0;
    if (sscanf(luid_str, "0x%lx_0x%lx", &hi, &lo) != 2) {
        strncpy(out, luid_str, outsz - 1);
        out[outsz - 1] = '\0';
        return;
    }

    LUID target;
    target.HighPart = (LONG)hi;
    target.LowPart  = (ULONG)lo;

    // Walk display devices and match via EnumDisplayDevices + registry key.
    // We use DXGI if available, otherwise fall back to the device description.
    DISPLAY_DEVICEA dd = {};
    dd.cb = sizeof(dd);
    for (DWORD idx = 0; EnumDisplayDevicesA(NULL, idx, &dd, 0); idx++) {
        if (!(dd.StateFlags & DISPLAY_DEVICE_ACTIVE)) {
            memset(&dd, 0, sizeof(dd));
            dd.cb = sizeof(dd);
            continue;
        }
        // Friendly name is in DeviceString (e.g. "NVIDIA GeForce RTX 4090")
        // We can't cheaply match LUID here without DXGI, so we use the adapter
        // index as a tiebreak: phys_N in the PDH instance matches adapter index N.
        // Instead, just enumerate adapters in order and match by LUID via DXGI.
        memset(&dd, 0, sizeof(dd));
        dd.cb = sizeof(dd);
    }

    // Use DXGI to resolve LUID → adapter description.
    typedef HRESULT(WINAPI* PFN_CreateFactory)(REFIID, void**);
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    if (!dxgi) {
        strncpy(out, luid_str, outsz - 1);
        out[outsz - 1] = '\0';
        return;
    }

    PFN_CreateFactory CreateFactory =
        (PFN_CreateFactory)GetProcAddress(dxgi, "CreateDXGIFactory1");
    if (!CreateFactory)
        CreateFactory = (PFN_CreateFactory)GetProcAddress(dxgi, "CreateDXGIFactory");

    bool found = false;
    if (CreateFactory) {
        IDXGIFactory1* factory = NULL;
        static const GUID IID_IDXGIFactory1_ =
            {0x770aae78,0xf26f,0x4dba,{0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87}};
        if (SUCCEEDED(CreateFactory(IID_IDXGIFactory1_, (void**)&factory)) && factory) {
            IDXGIAdapter1* adapter = NULL;
            static const GUID IID_IDXGIAdapter1_ =
                {0x29038f61,0x3839,0x4626,{0x91,0xfd,0x08,0x68,0x79,0x01,0x1a,0x05}};
            for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
                if (!adapter)
                    continue;
                DXGI_ADAPTER_DESC1 desc = {};
                if (SUCCEEDED(adapter->GetDesc1(&desc))) {
                    if (desc.AdapterLuid.HighPart == target.HighPart &&
                        desc.AdapterLuid.LowPart  == target.LowPart) {
                        // Convert wide description to narrow
                        WideCharToMultiByte(CP_UTF8, 0,
                            desc.Description, -1,
                            out, (int)outsz, NULL, NULL);
                        out[outsz - 1] = '\0';
                        found = true;
                        adapter->Release();
                        break;
                    }
                }
                adapter->Release();
            }
            factory->Release();
        }
    }

    FreeLibrary(dxgi);

    if (!found) {
        strncpy(out, luid_str, outsz - 1);
        out[outsz - 1] = '\0';
    }
}

// ── GPU memory per adapter ────────────────────────────────────────────────────
// Returns { ["NVIDIA GeForce RTX 4090"] = {
//     DedicatedUsageMB = 3468,
//     SharedUsageMB    = 348,
//     TotalCommittedMB = 3816,
// }, ... }
int hardware_gpu_memory(lua_State* L)
{
    // Three snapshot counters, all per adapter (luid_..._phys_0)
    static const char* const mem_counters[] = {
        "\\GPU Adapter Memory(*)\\Dedicated Usage",
        "\\GPU Adapter Memory(*)\\Shared Usage",
        "\\GPU Adapter Memory(*)\\Total Committed",
        NULL
    };
    static const char* const mem_fields[] = {
        "DedicatedUsageMB",
        "SharedUsageMB",
        "TotalCommittedMB",
        NULL
    };

    lua_newtable(L);  // result table

    for (int ci = 0; mem_counters[ci]; ci++) {
        PdhSample* samples = NULL;
        int n = 0;
        if (!pdh_sample_snapshot(mem_counters[ci], &samples, &n)) {
            kitsune_free(samples);
            continue;
        }
        for (int i = 0; i < n; i++) {
            char luid[64] = {};
            if (!pdh_parse_luid(samples[i].name, luid, sizeof(luid)))
                continue;

            // Get or create the adapter sub-table, keyed by friendly name
            char adapterName[256] = {};
            pdh_luid_to_adapter_name(luid, adapterName, sizeof(adapterName));

            // Check if key already exists in result table
            lua_getfield(L, -1, adapterName);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                lua_newtable(L);
                lua_pushvalue(L, -1);
                lua_setfield(L, -3, adapterName);
            }
            // stack: result, sub-table
            lua_pushinteger(L, (lua_Integer)(samples[i].value / (1024 * 1024)));
            lua_setfield(L, -2, mem_fields[ci]);
            lua_pop(L, 1);  // pop sub-table
        }
        kitsune_free(samples);
    }

    return 1;
}

// ── GPU engine load per adapter ───────────────────────────────────────────────
// Returns { ["NVIDIA GeForce RTX 4090"] = {
//     ["3d"]          = 12.5,
//     ["copy"]        = 1.0,
//     ["videoencode"] = 0.0,
//     ...
// }, ... }
// Uses a persistent PDH query so no sleep is needed between calls.
int hardware_gpu_load(lua_State* L)
{
    // --- persistent query state ---
    typedef struct {
        char         luid[64];
        char         engtype[64];
        PDH_HCOUNTER ctr;
    } GpuEngineEntry;

    static PDH_HQUERY      s_query   = NULL;
    static GpuEngineEntry* s_entries = NULL;
    static int             s_count   = 0;
    static bool            s_ready   = false;

    if (!s_ready) {
        char* paths = NULL;
        DWORD count = 0;
        if (pdh_expand_wildcards(
                "\\GPU Engine(*)\\Utilization Percentage",
                &paths, &count) != ERROR_SUCCESS || count == 0) {
            kitsune_free(paths);
            lua_newtable(L);
            return 1;
        }

        if (PdhOpenQuery(NULL, 0, &s_query) != ERROR_SUCCESS) {
            kitsune_free(paths);
            lua_newtable(L);
            return 1;
        }

        s_entries = (GpuEngineEntry*)kitsune_calloc(count, sizeof(GpuEngineEntry));
        if (!s_entries) {
            PdhCloseQuery(s_query);
            s_query = NULL;
            kitsune_free(paths);
            lua_newtable(L);
            return 1;
        }

        const char* p = paths;
        int kept = 0;
        for (DWORD i = 0; i < count; i++) {
            // Extract instance name from "\GPU Engine(instance)\Counter"
            const char* open  = strchr(p, '(');
            const char* close = open ? strchr(open, ')') : NULL;
            if (open && close) {
                size_t ilen = (size_t)(close - open - 1);
                char instance[512] = {};
                if (ilen < sizeof(instance)) {
                    memcpy(instance, open + 1, ilen);
                    char luid[64] = {};
                    char eng[64]  = {};
                    if (pdh_parse_luid(instance, luid, sizeof(luid)) &&
                        pdh_parse_engtype(instance, eng, sizeof(eng))) {
                        strncpy(s_entries[kept].luid,    luid, 63);
                        strncpy(s_entries[kept].engtype, eng,  63);
                        PdhAddCounterA(s_query, p, 0, &s_entries[kept].ctr);
                        kept++;
                    }
                }
            }
            p += strlen(p) + 1;
        }
        kitsune_free(paths);
        s_count = kept;

        // Prime baseline
        PdhCollectQueryData(s_query);
        s_ready = true;
        kitsune_snapshot_permanent_allocs();
    }

    PdhCollectQueryData(s_query);

    // Aggregate: sum utilisation per (luid, engtype), keyed by friendly name
    // We use a temporary flat array to accumulate, then push to Lua.
    typedef struct { char luid[64]; char engtype[64]; double sum; } EngAgg;
    EngAgg* agg = (EngAgg*)kitsune_calloc((size_t)s_count, sizeof(EngAgg));
    if (!agg) {
        lua_newtable(L);
        return 1;
    }
    int nagg = 0;

    for (int i = 0; i < s_count; i++) {
        PDH_FMT_COUNTERVALUE val = {};
        if (PdhGetFormattedCounterValue(s_entries[i].ctr, PDH_FMT_DOUBLE, NULL, &val) != ERROR_SUCCESS)
            continue;

        // Find or create aggregation slot
        int slot = -1;
        for (int j = 0; j < nagg; j++) {
            if (strcmp(agg[j].luid,    s_entries[i].luid)    == 0 &&
                strcmp(agg[j].engtype, s_entries[i].engtype) == 0) {
                slot = j;
                break;
            }
        }
        if (slot < 0) {
            slot = nagg++;
            strncpy(agg[slot].luid,    s_entries[i].luid,    63);
            strncpy(agg[slot].engtype, s_entries[i].engtype, 63);
            agg[slot].sum = 0.0;
        }
        agg[slot].sum += val.doubleValue;
    }

    // Build result table: { [adapterName] = { [engtype] = percent, ... } }
    lua_newtable(L);

    for (int j = 0; j < nagg; j++) {
        char adapterName[256] = {};
        pdh_luid_to_adapter_name(agg[j].luid, adapterName, sizeof(adapterName));

        lua_getfield(L, -1, adapterName);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, -3, adapterName);
        }
        double clamped = agg[j].sum > 100.0 ? 100.0 : agg[j].sum;
        lua_pushnumber(L, clamped);
        lua_setfield(L, -2, agg[j].engtype);
        lua_pop(L, 1);
    }

    kitsune_free(agg);
    return 1;
}

// ── Disk I/O throughput per physical disk ────────────────────────────────────
// Returns { ["0 C: D:"] = { ReadBytesPerSec=..., WriteBytesPerSec=..., ActivePercent=... }, ... }
// Uses a persistent PDH query — no sleep needed between calls.
int hardware_disk_io(lua_State* L)
{
    typedef struct {
        char         name[256];
        PDH_HCOUNTER readCtr;
        PDH_HCOUNTER writeCtr;
        PDH_HCOUNTER activeCtr;
    } DiskEntry;

    static PDH_HQUERY  s_query   = NULL;
    static DiskEntry*  s_disks   = NULL;
    static int         s_count   = 0;
    static bool        s_ready   = false;

    if (!s_ready) {
        char* paths = NULL;
        DWORD count = 0;
        if (pdh_expand_wildcards(
                "\\PhysicalDisk(*)\\Disk Read Bytes/sec",
                &paths, &count) != ERROR_SUCCESS || count == 0) {
            kitsune_free(paths);
            lua_newtable(L);
            return 1;
        }

        if (PdhOpenQuery(NULL, 0, &s_query) != ERROR_SUCCESS) {
            kitsune_free(paths);
            lua_newtable(L);
            return 1;
        }

        s_disks = (DiskEntry*)kitsune_calloc(count, sizeof(DiskEntry));
        if (!s_disks) {
            PdhCloseQuery(s_query);
            s_query = NULL;
            kitsune_free(paths);
            lua_newtable(L);
            return 1;
        }

        const char* p = paths;
        int kept = 0;
        for (DWORD i = 0; i < count; i++) {
            // Skip _Total
            if (strstr(p, "_Total")) {
                p += strlen(p) + 1;
                continue;
            }
            // Extract instance name from counter path
            const char* open  = strchr(p, '(');
            const char* close = open ? strchr(open, ')') : NULL;
            if (open && close && (close - open - 1) < 255) {
                strncpy(s_disks[kept].name, open + 1, (size_t)(close - open - 1));
                s_disks[kept].name[close - open - 1] = '\0';
            }
            else {
                strncpy(s_disks[kept].name, p, 255);
            }

            // Build the three counter paths for this instance
            char readPath[512], writePath[512], activePath[512];
            snprintf(readPath,   sizeof(readPath),   "\\PhysicalDisk(%s)\\Disk Read Bytes/sec",  s_disks[kept].name);
            snprintf(writePath,  sizeof(writePath),  "\\PhysicalDisk(%s)\\Disk Write Bytes/sec", s_disks[kept].name);
            snprintf(activePath, sizeof(activePath), "\\PhysicalDisk(%s)\\%% Disk Time",          s_disks[kept].name);

            PdhAddCounterA(s_query, readPath,   0, &s_disks[kept].readCtr);
            PdhAddCounterA(s_query, writePath,  0, &s_disks[kept].writeCtr);
            PdhAddCounterA(s_query, activePath, 0, &s_disks[kept].activeCtr);
            kept++;
            p += strlen(p) + 1;
        }
        kitsune_free(paths);
        s_count = kept;

        // Prime baseline
        PdhCollectQueryData(s_query);
        s_ready = true;
        kitsune_snapshot_permanent_allocs();
    }

    PdhCollectQueryData(s_query);

    lua_newtable(L);

    for (int i = 0; i < s_count; i++) {
        PDH_FMT_COUNTERVALUE readVal = {}, writeVal = {}, activeVal = {};
        bool hasRead   = PdhGetFormattedCounterValue(s_disks[i].readCtr,   PDH_FMT_DOUBLE, NULL, &readVal)   == ERROR_SUCCESS;
        bool hasWrite  = PdhGetFormattedCounterValue(s_disks[i].writeCtr,  PDH_FMT_DOUBLE, NULL, &writeVal)  == ERROR_SUCCESS;
        bool hasActive = PdhGetFormattedCounterValue(s_disks[i].activeCtr, PDH_FMT_DOUBLE, NULL, &activeVal) == ERROR_SUCCESS;

        lua_newtable(L);

        if (hasRead) {
            lua_pushnumber(L, readVal.doubleValue);
            lua_setfield(L, -2, "ReadBytesPerSec");
        }
        if (hasWrite) {
            lua_pushnumber(L, writeVal.doubleValue);
            lua_setfield(L, -2, "WriteBytesPerSec");
        }
        if (hasActive) {
            double clamped = activeVal.doubleValue > 100.0 ? 100.0 : activeVal.doubleValue;
            lua_pushnumber(L, clamped);
            lua_setfield(L, -2, "ActivePercent");
        }

        lua_setfield(L, -2, s_disks[i].name);
    }

    return 1;
}

// ── Network I/O throughput per adapter ───────────────────────────────────────
// Returns { ["Ethernet"] = { RecvBytesPerSec=..., SendBytesPerSec=..., TotalBytesPerSec=... }, ... }
// Uses a persistent PDH query — no sleep needed between calls.
int hardware_network_io(lua_State* L)
{
    typedef struct {
        char         name[256];
        PDH_HCOUNTER recvCtr;
        PDH_HCOUNTER sendCtr;
    } NetEntry;

    static PDH_HQUERY  s_query   = NULL;
    static NetEntry*   s_nics    = NULL;
    static int         s_count   = 0;
    static bool        s_ready   = false;

    if (!s_ready) {
        char* paths = NULL;
        DWORD count = 0;
        if (pdh_expand_wildcards(
                "\\Network Interface(*)\\Bytes Received/sec",
                &paths, &count) != ERROR_SUCCESS || count == 0) {
            kitsune_free(paths);
            lua_newtable(L);
            return 1;
        }

        if (PdhOpenQuery(NULL, 0, &s_query) != ERROR_SUCCESS) {
            kitsune_free(paths);
            lua_newtable(L);
            return 1;
        }

        s_nics = (NetEntry*)kitsune_calloc(count, sizeof(NetEntry));
        if (!s_nics) {
            PdhCloseQuery(s_query);
            s_query = NULL;
            kitsune_free(paths);
            lua_newtable(L);
            return 1;
        }

        const char* p = paths;
        int kept = 0;
        for (DWORD i = 0; i < count; i++) {
            const char* open  = strchr(p, '(');
            const char* close = open ? strchr(open, ')') : NULL;
            if (open && close && (close - open - 1) < 255) {
                strncpy(s_nics[kept].name, open + 1, (size_t)(close - open - 1));
                s_nics[kept].name[close - open - 1] = '\0';
            }
            else {
                strncpy(s_nics[kept].name, p, 255);
            }

            char recvPath[512], sendPath[512];
            snprintf(recvPath, sizeof(recvPath), "\\Network Interface(%s)\\Bytes Received/sec", s_nics[kept].name);
            snprintf(sendPath, sizeof(sendPath), "\\Network Interface(%s)\\Bytes Sent/sec",     s_nics[kept].name);

            PdhAddCounterA(s_query, recvPath, 0, &s_nics[kept].recvCtr);
            PdhAddCounterA(s_query, sendPath, 0, &s_nics[kept].sendCtr);
            kept++;
            p += strlen(p) + 1;
        }
        kitsune_free(paths);
        s_count = kept;

        // Prime baseline
        PdhCollectQueryData(s_query);
        s_ready = true;
        kitsune_snapshot_permanent_allocs();
    }

    PdhCollectQueryData(s_query);

    lua_newtable(L);

    for (int i = 0; i < s_count; i++) {
        PDH_FMT_COUNTERVALUE recvVal = {}, sendVal = {};
        bool hasRecv = PdhGetFormattedCounterValue(s_nics[i].recvCtr, PDH_FMT_DOUBLE, NULL, &recvVal) == ERROR_SUCCESS;
        bool hasSend = PdhGetFormattedCounterValue(s_nics[i].sendCtr, PDH_FMT_DOUBLE, NULL, &sendVal) == ERROR_SUCCESS;

        lua_newtable(L);

        if (hasRecv) {
            lua_pushnumber(L, recvVal.doubleValue);
            lua_setfield(L, -2, "RecvBytesPerSec");
        }
        if (hasSend) {
            lua_pushnumber(L, sendVal.doubleValue);
            lua_setfield(L, -2, "SendBytesPerSec");
        }
        if (hasRecv && hasSend) {
            lua_pushnumber(L, recvVal.doubleValue + sendVal.doubleValue);
            lua_setfield(L, -2, "TotalBytesPerSec");
        }

        lua_setfield(L, -2, s_nics[i].name);
    }

    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Linux implementation
// ─────────────────────────────────────────────────────────────────────────────
#else

// Helper: read a single integer from a sysfs file (e.g. /sys/class/hwmon/.../temp1_input)
static int sysfs_read_int(const char* path, long long* out)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return 0;
    int ok = fscanf(f, "%lld", out) == 1;
    fclose(f);
    return ok;
}

// Helper: read a string from a sysfs file (e.g. name, label)
static int sysfs_read_str(const char* path, char* buf, size_t bufsz)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return 0;
    if (!fgets(buf, (int)bufsz, f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    // Strip trailing newline
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';
    return 1;
}

// ── CPU temperature: first temperature sensor from hwmon that looks like CPU ─
int hardware_cpu_temp(lua_State* L)
{
    // Look through hwmon devices for a chip named "coretemp" or "k10temp" (AMD)
    // or "zenpower", then collect all temp*_input values.
    DIR* d = opendir("/sys/class/hwmon");
    if (!d) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);
    int idx = 1;

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;

        char namepath[512];
        snprintf(namepath, sizeof(namepath), "/sys/class/hwmon/%s/name", ent->d_name);
        char chipname[64] = {};
        if (!sysfs_read_str(namepath, chipname, sizeof(chipname)))
            continue;

        // Only CPU temperature chips
        if (strcmp(chipname, "coretemp") != 0 &&
            strcmp(chipname, "k10temp")  != 0 &&
            strcmp(chipname, "zenpower") != 0 &&
            strcmp(chipname, "cpu_thermal") != 0)
            continue;

        // Enumerate temp*_input files
        for (int i = 1; i <= 32; i++) {
            char inp[512];
            snprintf(inp, sizeof(inp), "/sys/class/hwmon/%s/temp%d_input", ent->d_name, i);
            long long raw = 0;
            if (!sysfs_read_int(inp, &raw))
                break;
            // sysfs reports millidegrees Celsius
            double celsius = raw / 1000.0;
            lua_pushnumber(L, celsius);
            lua_rawseti(L, -2, idx++);
        }
    }
    closedir(d);

    if (idx == 1) {
        lua_pop(L, 1);
        lua_pushnil(L);
    }

    return 1;
}

// Sensor type names matched by prefix in the hwmon attribute name
typedef struct {
    const char* prefix;
    const char* type;
    const char* unit;
    double scale; // multiply raw value by this to get the unit
} SensorKind;

static const SensorKind sensor_kinds[] = {
    { "temp",   "Temperature", "C",   0.001 },
    { "fan",    "Fan",         "RPM", 1.0   },
    { "in",     "Voltage",     "V",   0.001 },
    { "power",  "Power",       "W",   0.000001 },
    { "curr",   "Current",     "A",   0.001 },
    { "energy", "Energy",      "J",   0.000001 },
    { "humidity","Humidity",   "%",   0.001 },
    { NULL, NULL, NULL, 0.0 }
};

// ── Per-hardware-thread CPU load from /proc/stat
// Returns {["cpu0"] = 12.5, ["cpu1"] = 8.0, ...}
int hardware_cpu_threads_load(lua_State* L)
{
    // Static arrays for per-cpu delta computation.
    static long long prev_idle[512]  = {};
    static long long prev_total[512] = {};
    static int       initialized     = 0;

    FILE* f = fopen("/proc/stat", "r");
    if (!f) {
        lua_newtable(L);
        return 1;
    }

    lua_newtable(L);

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        // Match lines like "cpu0 user nice sys idle iowait irq softirq"
        if (strncmp(line, "cpu", 3) != 0 || line[3] == ' ')
            continue;

        int cpuidx = 0;
        if (sscanf(line + 3, "%d", &cpuidx) != 1)
            continue;
        if (cpuidx < 0 || cpuidx >= 512)
            continue;

        long long user = 0, nice = 0, sys = 0, idle = 0;
        long long iowait = 0, irq = 0, softirq = 0;
        // Skip past "cpuN " prefix
        const char* p = line + 3;
        while (*p && *p != ' ') p++;
        sscanf(p, "%lld %lld %lld %lld %lld %lld %lld",
            &user, &nice, &sys, &idle, &iowait, &irq, &softirq);

        long long total_idle  = idle + iowait;
        long long total       = user + nice + sys + idle + iowait + irq + softirq;
        long long delta_idle  = total_idle - prev_idle[cpuidx];
        long long delta_total = total      - prev_total[cpuidx];

        prev_idle[cpuidx]  = total_idle;
        prev_total[cpuidx] = total;

        double load = 0.0;
        if (initialized && delta_total > 0)
            load = 100.0 * (1.0 - (double)delta_idle / (double)delta_total);

        char key[16];
        snprintf(key, sizeof(key), "cpu%d", cpuidx);
        lua_pushstring(L, key);
        lua_pushnumber(L, load);
        lua_settable(L, -3);
    }
    fclose(f);
    initialized = 1;

    return 1;
}



// ── CPU load via /proc/stat ───────────────────────────────────────────────────
int hardware_cpu_load(lua_State* L)
{
    // We need two samples to compute a delta; use static state.
    static long long prev_idle = 0, prev_total = 0;

    FILE* f = fopen("/proc/stat", "r");
    if (!f) {
        lua_pushnil(L);
        return 1;
    }

    char line[256];
    long long user = 0, nice = 0, sys = 0, idle = 0, iowait = 0, irq = 0, softirq = 0;
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "cpu ", 4) == 0) {
            sscanf(line, "cpu %lld %lld %lld %lld %lld %lld %lld",
                &user, &nice, &sys, &idle, &iowait, &irq, &softirq);
            found = 1;
            break;
        }
    }
    fclose(f);

    if (!found) {
        lua_pushnil(L);
        return 1;
    }

    long long total_idle  = idle + iowait;
    long long total       = user + nice + sys + idle + iowait + irq + softirq;
    long long delta_idle  = total_idle - prev_idle;
    long long delta_total = total      - prev_total;

    prev_idle  = total_idle;
    prev_total = total;

    double load = 0.0;
    if (delta_total > 0)
        load = 100.0 * (1.0 - (double)delta_idle / (double)delta_total);

    lua_pushnumber(L, load);
    return 1;
}

// ── Memory info via /proc/meminfo ─────────────────────────────────────────────
int hardware_memory(lua_State* L)
{
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) {
        lua_pushnil(L);
        return 1;
    }

    long long memTotal = 0, memAvail = 0, swapTotal = 0, swapFree = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        long long val = 0;
        if (sscanf(line, "MemTotal: %lld", &val) == 1)
            memTotal  = val;
        else if (sscanf(line, "MemAvailable: %lld", &val) == 1)
            memAvail  = val;
        else if (sscanf(line, "SwapTotal: %lld", &val) == 1)
            swapTotal = val;
        else if (sscanf(line, "SwapFree: %lld", &val) == 1)
            swapFree  = val;
    }
    fclose(f);

    long long memUsed = memTotal - memAvail;
    int load = (memTotal > 0) ? (int)((memUsed * 100) / memTotal) : 0;

    lua_newtable(L);

    lua_pushinteger(L, (lua_Integer)(memTotal  / 1024));
    lua_setfield(L, -2, "TotalPhys");

    lua_pushinteger(L, (lua_Integer)(memAvail  / 1024));
    lua_setfield(L, -2, "AvailPhys");

    lua_pushinteger(L, (lua_Integer)(swapTotal / 1024));
    lua_setfield(L, -2, "TotalSwap");

    lua_pushinteger(L, (lua_Integer)(swapFree  / 1024));
    lua_setfield(L, -2, "AvailSwap");

    lua_pushinteger(L, (lua_Integer)load);
    lua_setfield(L, -2, "LoadPercent");

    return 1;
}

// ── CPU brand string via /proc/cpuinfo ───────────────────────────────────────
int hardware_cpu_name(lua_State* L)
{
    FILE* f = fopen("/proc/cpuinfo", "r");
    if (!f) {
        lua_pushnil(L);
        return 1;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "model name", 10) == 0) {
            char* colon = strchr(line, ':');
            if (colon) {
                colon++;
                while (*colon == ' ')
                    colon++;
                // Strip trailing newline
                size_t len = strlen(colon);
                if (len > 0 && colon[len - 1] == '\n')
                    colon[len - 1] = '\0';
                lua_pushstring(L, colon);
                fclose(f);
                return 1;
            }
        }
    }
    fclose(f);
    lua_pushnil(L);
    return 1;
}

// ── Battery info via /sys/class/power_supply/ ─────────────────────────────────
int hardware_battery(lua_State* L)
{
    // Find the first battery device (type == "Battery")
    DIR* d = opendir("/sys/class/power_supply");
    if (!d) {
        lua_pushnil(L);
        return 1;
    }

    char batpath[256] = {};
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;
        char typepath[512];
        snprintf(typepath, sizeof(typepath), "/sys/class/power_supply/%s/type", ent->d_name);
        char type[32] = {};
        if (sysfs_read_str(typepath, type, sizeof(type)) && strcmp(type, "Battery") == 0) {
            snprintf(batpath, sizeof(batpath), "/sys/class/power_supply/%s", ent->d_name);
            break;
        }
    }
    closedir(d);

    if (batpath[0] == '\0') {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);

    // Capacity percent
    char cappath[512];
    snprintf(cappath, sizeof(cappath), "%s/capacity", batpath);
    long long cap = 0;
    if (sysfs_read_int(cappath, &cap)) {
        lua_pushinteger(L, (lua_Integer)cap);
        lua_setfield(L, -2, "Percent");
    }
    else {
        lua_pushnil(L);
        lua_setfield(L, -2, "Percent");
    }

    // Status: "Charging", "Discharging", "Full", "Not charging", "Unknown"
    char statuspath[512];
    snprintf(statuspath, sizeof(statuspath), "%s/status", batpath);
    char status[64] = {};
    sysfs_read_str(statuspath, status, sizeof(status));

    lua_pushboolean(L, strcmp(status, "Charging") == 0 || strcmp(status, "Full") == 0);
    lua_setfield(L, -2, "ACLine");

    lua_pushboolean(L, strcmp(status, "Charging") == 0);
    lua_setfield(L, -2, "Charging");

    // Time remaining in seconds via energy_now / power_now (may not exist)
    char enpath[512], pwpath[512];
    snprintf(enpath, sizeof(enpath), "%s/energy_now", batpath);
    snprintf(pwpath, sizeof(pwpath), "%s/power_now", batpath);
    long long energy_now = 0, power_now = 0;
    if (sysfs_read_int(enpath, &energy_now) && sysfs_read_int(pwpath, &power_now) && power_now > 0) {
        long long secs = (energy_now * 3600LL) / power_now;
        lua_pushinteger(L, (lua_Integer)secs);
        lua_setfield(L, -2, "SecondsRemaining");
    }
    else {
        lua_pushnil(L);
        lua_setfield(L, -2, "SecondsRemaining");
    }

    return 1;
}

// GPU functions are Windows-only (PDH + DXGI).
// Return nil on Linux rather than omitting the symbols entirely.
int hardware_gpu_memory(lua_State* L)
{
    lua_pushnil(L);
    return 1;
}

int hardware_gpu_load(lua_State* L)
{
    lua_pushnil(L);
    return 1;
}

// ── Disk I/O throughput per physical disk via /proc/diskstats ─────────────────
// Returns { ["sda"] = { ReadBytesPerSec=..., WriteBytesPerSec=... }, ... }
// Rate is computed from the delta between two successive calls.
int hardware_disk_io(lua_State* L)
{
    typedef struct {
        char name[64];
        unsigned long long rsect;
        unsigned long long wsect;
    } DiskStat;

    static DiskStat prev[64];
    static int      prev_count = 0;
    static bool     initialized = false;

    FILE* f = fopen("/proc/diskstats", "r");
    if (!f) {
        lua_newtable(L);
        return 1;
    }

    DiskStat cur[64];
    int cur_count = 0;

    char line[256];
    while (fgets(line, sizeof(line), f) && cur_count < 64) {
        int major, minor;
        char name[64];
        unsigned long long rd_ios, rd_merges, rd_sectors, rd_ticks;
        unsigned long long wr_ios, wr_merges, wr_sectors;
        if (sscanf(line, "%d %d %63s %llu %llu %llu %llu %llu %llu %llu",
                &major, &minor, name,
                &rd_ios, &rd_merges, &rd_sectors, &rd_ticks,
                &wr_ios, &wr_merges, &wr_sectors) < 10)
            continue;
        // Skip partitions (sda1, sdb2 etc.) — only keep whole disks
        if (minor != 0 && (name[strlen(name)-1] >= '0' && name[strlen(name)-1] <= '9'))
            continue;
        strncpy(cur[cur_count].name, name, 63);
        cur[cur_count].rsect = rd_sectors;
        cur[cur_count].wsect = wr_sectors;
        cur_count++;
    }
    fclose(f);

    lua_newtable(L);

    if (initialized) {
        for (int i = 0; i < cur_count; i++) {
            for (int j = 0; j < prev_count; j++) {
                if (strcmp(cur[i].name, prev[j].name) != 0)
                    continue;
                // sectors are 512 bytes; rate is bytes since last call (not per-second
                // without a timer, but consistent with single-call delta semantics)
                unsigned long long rdelta = (cur[i].rsect - prev[j].rsect) * 512ULL;
                unsigned long long wdelta = (cur[i].wsect - prev[j].wsect) * 512ULL;
                lua_newtable(L);
                lua_pushnumber(L, (lua_Number)rdelta);
                lua_setfield(L, -2, "ReadBytesPerSec");
                lua_pushnumber(L, (lua_Number)wdelta);
                lua_setfield(L, -2, "WriteBytesPerSec");
                lua_setfield(L, -2, cur[i].name);
                break;
            }
        }
    }

    for (int i = 0; i < cur_count && i < 64; i++)
        prev[i] = cur[i];
    prev_count  = cur_count;
    initialized = true;

    return 1;
}

// ── Network I/O throughput per adapter via /proc/net/dev ─────────────────────
// Returns { ["eth0"] = { RecvBytesPerSec=..., SendBytesPerSec=..., TotalBytesPerSec=... }, ... }
// Rate is computed from the delta between two successive calls.
int hardware_network_io(lua_State* L)
{
    typedef struct {
        char               name[64];
        unsigned long long rx;
        unsigned long long tx;
    } NetStat;

    static NetStat prev[64];
    static int     prev_count = 0;
    static bool    initialized = false;

    FILE* f = fopen("/proc/net/dev", "r");
    if (!f) {
        lua_newtable(L);
        return 1;
    }

    NetStat cur[64];
    int cur_count = 0;

    char line[256];
    // Skip first two header lines
    fgets(line, sizeof(line), f);
    fgets(line, sizeof(line), f);

    while (fgets(line, sizeof(line), f) && cur_count < 64) {
        char* colon = strchr(line, ':');
        if (!colon)
            continue;
        *colon = '\0';
        const char* nameptr = line;
        while (*nameptr == ' ') nameptr++;
        strncpy(cur[cur_count].name, nameptr, 63);

        unsigned long long rx = 0, tx = 0, dummy = 0;
        // Fields after ':': rx_bytes rx_packets rx_errs rx_drop rx_fifo rx_frame rx_compressed rx_multicast
        //                    tx_bytes tx_packets ...
        sscanf(colon + 1,
            "%llu %llu %llu %llu %llu %llu %llu %llu %llu",
            &rx, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &tx);
        cur[cur_count].rx = rx;
        cur[cur_count].tx = tx;
        cur_count++;
    }
    fclose(f);

    lua_newtable(L);

    if (initialized) {
        for (int i = 0; i < cur_count; i++) {
            for (int j = 0; j < prev_count; j++) {
                if (strcmp(cur[i].name, prev[j].name) != 0)
                    continue;
                unsigned long long rdelta = cur[i].rx - prev[j].rx;
                unsigned long long tdelta = cur[i].tx - prev[j].tx;
                lua_newtable(L);
                lua_pushnumber(L, (lua_Number)rdelta);
                lua_setfield(L, -2, "RecvBytesPerSec");
                lua_pushnumber(L, (lua_Number)tdelta);
                lua_setfield(L, -2, "SendBytesPerSec");
                lua_pushnumber(L, (lua_Number)(rdelta + tdelta));
                lua_setfield(L, -2, "TotalBytesPerSec");
                lua_setfield(L, -2, cur[i].name);
                break;
            }
        }
    }

    for (int i = 0; i < cur_count && i < 64; i++)
        prev[i] = cur[i];
    prev_count  = cur_count;
    initialized = true;

    return 1;
}

#endif // _WIN32

