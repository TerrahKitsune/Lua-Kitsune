#pragma once
#include "lua_main_incl.h"
#include <inttypes.h>

extern const char* LUAMONGO;

typedef struct LuaMongo {
    void* worker;        // LuaMongoWorker* — persistent thread, lives for connection lifetime
    void* client;        // mongoc_client_t* convenience alias; worker owns lifetime
    int   aliveTokenRef; // LUA_NOREF when not set
} LuaMongo;

LuaMongo* lua_tomongo(lua_State* L, int index);
LuaMongo* lua_pushmongo(lua_State* L);

int MongoConnect(lua_State* L);
int MongoIsFinished(lua_State* L);
int MongoWait(lua_State* L);
int MongoCancel(lua_State* L);
int MongoSetAliveToken(lua_State* L);
int MongoGetResult(lua_State* L);
int MongoFind(lua_State* L);
int MongoFindOne(lua_State* L);
int MongoInsertOne(lua_State* L);
int MongoInsertMany(lua_State* L);
int MongoUpdateOne(lua_State* L);
int MongoUpdateMany(lua_State* L);
int MongoDeleteOne(lua_State* L);
int MongoDeleteMany(lua_State* L);
int MongoAggregate(lua_State* L);
int MongoCommand(lua_State* L);
int MongoCountDocuments(lua_State* L);

int MongoClose(lua_State* L);
int luamongo_gc(lua_State* L);
int luamongo_tostring(lua_State* L);
