#pragma once
#define LIBARCHIVE_STATIC

#pragma comment(lib, "libarchive/liblzma.lib")
#pragma comment(lib, "libarchive/archive_static.lib")

#include "libarchive/lzma.h"
#include "lua_main_incl.h"
#include <Windows.h>
static const char* ARCHIVE = "ARCHIVE";
#include "libarchive/archive.h"
#include "libarchive/archive_entry.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct LuaArchive {

	char* file;
	bool isRead;

} LuaArchive;


LuaArchive* lua_pusharchive(lua_State* L);
LuaArchive* lua_toarchive(lua_State* L, int index);

int OpenReadArchive(lua_State* L);

int archive_gc(lua_State* L);
int archive_tostring(lua_State* L);