#pragma once
#include "LuaMainInclude.h"
#include "sha1.h"
static const char* LUASHA1 = "LUASHA1";

typedef struct LuaSHA1 {
	SHA1_CTX ctx;
	bool finished;
	unsigned char hash[20];
} LuaSHA1;

LuaSHA1* lua_tosha1(lua_State* L, int index);
LuaSHA1* lua_pushsha1(lua_State* L);

int SHA1_New(lua_State* L);
int SHA1_Update(lua_State* L);
int SHA1_Finish(lua_State* L);

int SHA1_GC(lua_State* L);
int SHA1_ToString(lua_State* L);