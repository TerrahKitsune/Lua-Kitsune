#include "LuaHardwareMain.h"
#include "LuaHardware.h"

static const luaL_Reg hardware_functions[] = {
    { "CpuTemp",        hardware_cpu_temp         },
    { "CpuThreadsLoad", hardware_cpu_threads_load  },
    { "CpuLoad",        hardware_cpu_load          },
    { "Memory",         hardware_memory            },
    { "CpuName",        hardware_cpu_name          },
    { "Battery",        hardware_battery           },
    { "GpuMemory",      hardware_gpu_memory        },
    { "GpuLoad",        hardware_gpu_load          },
    { "NvidiaSmi",      hardware_nvidia_smi        },
    { "NvidiaSetPowerLimit", hardware_nvidia_set_power_limit },
    { "DiskIO",         hardware_disk_io           },
    { "NetworkIO",      hardware_network_io        },
    { NULL, NULL }
};

int luaopen_hardware(lua_State* L)
{
    luaL_newlibtable(L, hardware_functions);
    luaL_setfuncs(L, hardware_functions, 0);
    return 1;
}
