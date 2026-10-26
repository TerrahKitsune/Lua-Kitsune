#pragma once
#include "LuaMainInclude.h"
extern "C" {
#include "sha256.h"
}
static const char * LUASHA256 = "LUASHA256";

typedef struct LuaSHA256 {
	SHA256_CTX SHA;
	unsigned char hash[SHA256_BLOCK_SIZE];
	bool finished;   // set by Finish(); hash then holds the digest for repeated calls
} LuaSHA256;

LuaSHA256 * lua_tosha256(lua_State *L, int index);
LuaSHA256 * lua_pushsha256(lua_State *L);

int SHA256_New(lua_State *L);
int SHA256_Update(lua_State *L);
int SHA256_Finish(lua_State *L);

int SHA256_GC(lua_State *L);
int SHA256_ToString(lua_State *L);