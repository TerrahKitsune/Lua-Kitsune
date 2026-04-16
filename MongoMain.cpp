#include "MongoMain.h"
#include "LuaMongo.h"
#include "mem.h"
#include "platform.h"

#ifdef KITSUNE_MONGO
#include <mongoc/mongoc.h>
#include <mutex>

// =============================================================================
// BSON allocator vtable — routes all libbson heap calls through kitsune_malloc
// so the g_live_allocs counter detects any bson object leaks.
// aligned_alloc is left NULL; bson_mem_set_vtable falls back to malloc for it.
// =============================================================================

static const bson_mem_vtable_t s_bson_vtable = {
    kitsune_malloc,
    kitsune_calloc,
    kitsune_realloc,
    kitsune_free,
    NULL,           // aligned_alloc: bson falls back to malloc when NULL
    { NULL, NULL, NULL }
};

#endif // KITSUNE_MONGO

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
    // Set vtable first so all mongoc_init() allocations go through kitsune_malloc.
    // Both calls must happen exactly once per process; guard with once_flag.
    static std::once_flag s_init_flag;
    std::call_once(s_init_flag, []() {
        bson_mem_set_vtable(&s_bson_vtable);
        mongoc_init();
        atexit(mongoc_cleanup);
    });
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

