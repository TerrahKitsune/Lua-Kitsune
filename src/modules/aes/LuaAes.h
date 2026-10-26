#pragma once
#include "LuaMainInclude.h"
#ifdef _WIN32
#include <Windows.h>
#else
#include <stdint.h>
typedef uint32_t DWORD;
#endif
#include "aes.hpp"
static const char * LUAES = "LUAES";

#define AES_256_ECB 1
#define AES_256_CBC 2
#define AES_256_CTR 3

typedef struct LuaAes {

	DWORD type;
	uint8_t iv[AES_BLOCKLEN];
	AES_ctx context;

} LuaAes;


LuaAes * lua_pushaes(lua_State *L);
LuaAes * lua_toaes(lua_State *L, int index);

int Aes_New(lua_State *L);
int Aes_Encrypt(lua_State *L);
int Aes_Decrypt(lua_State *L);
int Aes_SetIV(lua_State *L);

int Aes_GC(lua_State *L);
int Aes_ToString(lua_State *L);