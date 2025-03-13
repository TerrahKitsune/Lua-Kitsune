#pragma once
#include "lua_main_incl.h"
#include "sha1.h"
static const char* LUASHA1 = "LUASHA1";

typedef struct LuaSHA1 {
	SHA1_CTX ctx;
	bool finished;
	unsigned char hash[20];
} LuaSHA1;

LuaSHA1* lua_tosha1(lua_State* L, int index);
LuaSHA1* lua_pushsha1(lua_State* L);

int NewSHA1(lua_State* L);
int UpdateSHA1(lua_State* L);
int FinalSHA1(lua_State* L);

int sha1_gc(lua_State* L);
int sha1_tostring(lua_State* L);