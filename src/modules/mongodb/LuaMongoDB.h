#pragma once
#include "LuaMainInclude.h"
#include <inttypes.h>

extern const char* LUAMONGO;

typedef struct LuaMongo {
    void* worker;        // LuaMongoWorker* — persistent thread, lives for connection lifetime
    void* client;        // mongoc_client_t* convenience alias; worker owns lifetime
    int   aliveTokenRef; // LUA_NOREF when not set
} LuaMongo;

LuaMongo* luaL_checkmongodb(lua_State* L, int index);
LuaMongo* lua_pushmongodb(lua_State* L);

int MongoDB_Connect(lua_State* L);
int MongoDB_IsFinished(lua_State* L);
int MongoDB_Wait(lua_State* L);
int MongoDB_Cancel(lua_State* L);
int MongoDB_SetAliveToken(lua_State* L);
int MongoDB_GetResult(lua_State* L);
int MongoDB_Find(lua_State* L);
int MongoDB_FindOne(lua_State* L);
int MongoDB_InsertOne(lua_State* L);
int MongoDB_InsertMany(lua_State* L);
int MongoDB_UpdateOne(lua_State* L);
int MongoDB_UpdateMany(lua_State* L);
int MongoDB_DeleteOne(lua_State* L);
int MongoDB_DeleteMany(lua_State* L);
int MongoDB_Aggregate(lua_State* L);
int MongoDB_Command(lua_State* L);
int MongoDB_CountDocuments(lua_State* L);

int MongoDB_Close(lua_State* L);
int MongoDB_GC(lua_State* L);
int MongoDB_ToString(lua_State* L);
