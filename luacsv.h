#pragma once
#include "lua_main_incl.h"
#include <Windows.h>
#include "luawchar.h"

static const char* LUACSV = "LUACSV";

typedef struct LuaCsv {

	int pos;
	LuaWChar * data;
	wchar_t last;

	FILE* file;

	size_t len;
	size_t alloc;
	wchar_t* buffer;

	wchar_t delimiter;

} LuaCsv;

LuaCsv* lua_pushcsv(lua_State* L);
LuaCsv* lua_tocsv(lua_State* L, int index);

int CreateCsv(lua_State* L);
int DecodeString(lua_State* L);
int DecodeFile(lua_State* L);

int csv_gc(lua_State* L);
int csv_tostring(lua_State* L);