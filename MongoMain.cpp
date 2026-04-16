#include "MongoMain.h"
#include "LuaMongo.h"
#include "mem.h"
#include "platform.h"

#ifdef KITSUNE_MONGO
#include <mongoc/mongoc.h>
#include <mutex>

// Set to true when MongoEagerInit() claims the once_flag before the lazy path.
// Suppresses kitsune_snapshot_permanent_allocs in the lazy path because the
// caller will explicitly call MongoExplicitCleanup() instead.
static bool s_eager_init = false;

#endif // KITSUNE_MONGO

void MongoGlobalCleanup() {
    // mongoc_init/mongoc_cleanup are designed to be called exactly once per
    // process lifetime.  Cleanup is registered via atexit in MongoGlobalInit
    // and must not be called again here; doing so would leave the library
    // torn-down mid-process, crashing any subsequent engine session that
    // tries to use MongoDB.
}

void MongoEagerInit() {
#ifdef KITSUNE_MONGO
    s_eager_init = true;
    static std::once_flag s_init_flag;
    std::call_once(s_init_flag, []() {
        static const bson_mem_vtable_t vtable = {
            kitsune_malloc,
            kitsune_calloc,
            kitsune_realloc,
            kitsune_free,
            nullptr,
            { nullptr, nullptr, nullptr }
        };
        bson_mem_set_vtable(&vtable);
        mongoc_init();
        // No snapshot: caller will call MongoExplicitCleanup() before the CRT
        // check fires, so the init allocations are freed within the diff window.
    });
#endif
}

void MongoExplicitCleanup() {
#ifdef KITSUNE_MONGO
    static std::once_flag s_cleanup_flag;
    std::call_once(s_cleanup_flag, []() {
        mongoc_cleanup();
    });
#endif
}

static void MongoGlobalInit() {
#ifdef KITSUNE_MONGO
    // mongoc_init must be called exactly once per process.  Route all mongoc/bson
    // allocations through kitsune's allocators so they are tracked by g_live_allocs.
    // In the lazy path (test host, no MongoEagerInit call), snapshot the live alloc
    // count after init so EndMemoryManager does not report mongoc's one-time global
    // state as a leak across sessions.
    static std::once_flag s_init_flag;
    std::call_once(s_init_flag, []() {
        static const bson_mem_vtable_t vtable = {
            kitsune_malloc,
            kitsune_calloc,
            kitsune_realloc,
            kitsune_free,
            nullptr,
            { nullptr, nullptr, nullptr }
        };
        bson_mem_set_vtable(&vtable);
        mongoc_init();
        if (!s_eager_init)
            kitsune_snapshot_permanent_allocs();
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

