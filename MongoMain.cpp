#include "MongoMain.h"
#include "LuaMongo.h"
#include "mem.h"
#include "platform.h"

#ifdef KITSUNE_MONGO
#include <mongoc/mongoc.h>
#include <mutex>

#endif // KITSUNE_MONGO

void MongoGlobalCleanup() {
    // mongoc_init/mongoc_cleanup are designed to be called exactly once per
    // process lifetime.  Cleanup is registered via atexit in MongoGlobalInit
    // and must not be called again here; doing so would leave the library
    // torn-down mid-process, crashing any subsequent engine session that
    // tries to use MongoDB.
}

static void MongoGlobalInit() {
#ifdef KITSUNE_MONGO
    // mongoc_init must be called exactly once per process.  We do NOT set
    // bson_mem_set_vtable here: mongoc manages its own internal allocations
    // independently of kitsune_malloc, so those allocations do not affect
    // g_live_allocs and will not cause false "memory leak" reports at the
    // end of a session.
    static std::once_flag s_init_flag;
    std::call_once(s_init_flag, []() {
        mongoc_init();
        atexit([]() { mongoc_cleanup(); });
    });
#endif
}

static const luaL_Reg mongofunctions[] = {
    { "Connect",        MongoConnect        },
    { "IsFinished",     MongoIsFinished     },
    { "Wait",           MongoWait           },
    { "Cancel",         MongoCancel         },
    { "GetResult",      MongoGetResult      },
    { "Find",           MongoFind           },
    { "FindOne",        MongoFindOne        },
    { "InsertOne",      MongoInsertOne      },
    { "InsertMany",     MongoInsertMany     },
    { "UpdateOne",      MongoUpdateOne      },
    { "UpdateMany",     MongoUpdateMany     },
    { "DeleteOne",      MongoDeleteOne      },
    { "DeleteMany",     MongoDeleteMany     },
    { "Aggregate",      MongoAggregate      },
    { "Command",        MongoCommand        },
    { "CountDocuments", MongoCountDocuments },
    { "Close",          MongoClose          },
    { NULL, NULL }
};

static const luaL_Reg mongometa[] = {
    { "__gc",       luamongo_gc       },
    { "__tostring", luamongo_tostring },
    { NULL, NULL }
};

int luaopen_mongo(lua_State* L) {
#ifdef KITSUNE_MONGO
    MongoGlobalInit();
#endif

    luaL_newlibtable(L, mongofunctions);
    luaL_setfuncs(L, mongofunctions, 0);

    luaL_newmetatable(L, LUAMONGO);
    luaL_setfuncs(L, mongometa, 0);

    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -3);
    lua_rawset(L, -3);

    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -3);
    lua_rawset(L, -3);

    lua_pop(L, 1);
    return 1;
}

