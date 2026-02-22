#pragma once
#include "lua_json.h"
#include <stdlib.h> 
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include "math.h"

uintptr_t json_popfromantirecursion(JsonContext* context);
void json_append(const char * data, size_t len, lua_State *L, JsonContext* context, bool isEnd=false);
void json_bail(lua_State *L, JsonContext* context, const char * err);
unsigned int table_crc32(const unsigned char* data, int size);
bool json_addtoantirecursion(uintptr_t id, JsonContext* context);
bool json_existsinantirecursion(uintptr_t id, JsonContext* context);
void json_removefromantirecursion(uintptr_t id, JsonContext* context);
void json_pushnullornil(lua_State* L, JsonContext* context);
bool json_isnull(lua_State* L, JsonContext* context);