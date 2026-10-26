#include "hardware/LuaHardwareMain.h"
#include "hardware/LuaHardware.h"

static const luaL_Reg hardwareFunctions[] = {
    { "CpuTemp",        Hardware_CpuTemp         },
    { "CpuThreadsLoad", Hardware_CpuThreadsLoad  },
    { "CpuLoad",        Hardware_CpuLoad          },
    { "Memory",         Hardware_Memory            },
    { "CpuName",        Hardware_CpuName          },
    { "Battery",        Hardware_Battery           },
    { "GpuMemory",      Hardware_GpuMemory        },
    { "GpuLoad",        Hardware_GpuLoad          },
    { "NvidiaSmi",      Hardware_NvidiaSmi        },
    { "NvidiaSetPowerLimit", Hardware_NvidiaSetPowerLimit },
    { "DiskIO",         Hardware_DiskIO           },
    { "NetworkIO",      Hardware_NetworkIO        },
    { NULL, NULL }
};

int luaopen_hardware(lua_State* L)
{
    luaL_newlibtable(L, hardwareFunctions);
    luaL_setfuncs(L, hardwareFunctions, 0);
    return 1;
}
