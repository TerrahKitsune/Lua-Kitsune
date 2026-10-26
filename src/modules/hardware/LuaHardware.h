#pragma once
#include "LuaMainInclude.h"

// CPU temperature — returns an array of {Name, Value} tables (one per thermal
// zone found), or nil if unavailable.
int Hardware_CpuTemp(lua_State* L);

// Per-hardware-thread CPU load as a flat {[ThreadKey] = percent} table.
// Windows key format: "socket,thread" (e.g. "0,0", "0,1").
// Linux key format: "cpu0", "cpu1", etc.
int Hardware_CpuThreadsLoad(lua_State* L);

// Overall CPU utilisation as a single number (0-100), or nil on failure.
int Hardware_CpuLoad(lua_State* L);

// Returns a table with memory info: TotalPhys, AvailPhys, TotalSwap,
// AvailSwap, LoadPercent (all in MB except LoadPercent which is 0-100).
int Hardware_Memory(lua_State* L);

// CPU brand string
int Hardware_CpuName(lua_State* L);

// Battery level and charge status. Returns a table:
// { Percent, ACLine, Charging, SecondsRemaining } or nil if no battery.
int Hardware_Battery(lua_State* L);

// GPU dedicated/shared memory usage per adapter (Windows only, nil on Linux).
// Returns { ["Adapter Name"] = { DedicatedUsageMB, SharedUsageMB, TotalCommittedMB } }
int Hardware_GpuMemory(lua_State* L);

// GPU engine utilisation per adapter (Windows only, nil on Linux).
// Returns { ["Adapter Name"] = { ["3d"]=12.5, ["copy"]=1.0, ... } }
// Uses a persistent PDH query — no sleep needed between calls.
int Hardware_GpuLoad(lua_State* L);

// NVIDIA GPU status via NVML, mirroring nvidia-smi (Windows and Linux).
// Returns { DriverVersion, NvmlVersion, CudaVersion, Gpus = { {...}, ... } },
// or nil when no NVIDIA driver is installed. Unsupported fields are omitted.
int Hardware_NvidiaSmi(lua_State* L);

// Sets a GPU's software power limit, like `nvidia-smi -i <index> -pl <watts>`.
// Returns true, or nil plus an error message. Requires admin/root.
int Hardware_NvidiaSetPowerLimit(lua_State* L);

// Disk I/O throughput per physical disk.
// Returns { ["PhysicalDisk 0 (C:)"] = { ReadBytesPerSec=..., WriteBytesPerSec=..., ActivePercent=... }, ... }
// Uses a persistent PDH query — no sleep needed between calls.
int Hardware_DiskIO(lua_State* L);

// Network I/O throughput per adapter.
// Returns { ["Ethernet"] = { RecvBytesPerSec=..., SendBytesPerSec=..., TotalBytesPerSec=... }, ... }
// Uses a persistent PDH query — no sleep needed between calls.
int Hardware_NetworkIO(lua_State* L);
