#pragma once
#include "lua_main_incl.h"

// CPU temperature — returns an array of {Name, Value} tables (one per thermal
// zone found), or nil if unavailable.
int hardware_cpu_temp(lua_State* L);

// Per-hardware-thread CPU load as a flat {[ThreadKey] = percent} table.
// Windows key format: "socket,thread" (e.g. "0,0", "0,1").
// Linux key format: "cpu0", "cpu1", etc.
int hardware_cpu_threads_load(lua_State* L);

// Overall CPU utilisation as a single number (0-100), or nil on failure.
int hardware_cpu_load(lua_State* L);

// Returns a table with memory info: TotalPhys, AvailPhys, TotalSwap,
// AvailSwap, LoadPercent (all in MB except LoadPercent which is 0-100).
int hardware_memory(lua_State* L);

// CPU brand string
int hardware_cpu_name(lua_State* L);

// Battery level and charge status. Returns a table:
// { Percent, ACLine, Charging, SecondsRemaining } or nil if no battery.
int hardware_battery(lua_State* L);

// GPU dedicated/shared memory usage per adapter (Windows only, nil on Linux).
// Returns { ["Adapter Name"] = { DedicatedUsageMB, SharedUsageMB, TotalCommittedMB } }
int hardware_gpu_memory(lua_State* L);

// GPU engine utilisation per adapter (Windows only, nil on Linux).
// Returns { ["Adapter Name"] = { ["3d"]=12.5, ["copy"]=1.0, ... } }
// Uses a persistent PDH query — no sleep needed between calls.
int hardware_gpu_load(lua_State* L);

// Disk I/O throughput per physical disk.
// Returns { ["PhysicalDisk 0 (C:)"] = { ReadBytesPerSec=..., WriteBytesPerSec=..., ActivePercent=... }, ... }
// Uses a persistent PDH query — no sleep needed between calls.
int hardware_disk_io(lua_State* L);

// Network I/O throughput per adapter.
// Returns { ["Ethernet"] = { RecvBytesPerSec=..., SendBytesPerSec=..., TotalBytesPerSec=... }, ... }
// Uses a persistent PDH query — no sleep needed between calls.
int hardware_network_io(lua_State* L);
