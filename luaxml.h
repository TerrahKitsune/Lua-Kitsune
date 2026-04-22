#pragma once
#include "lua_main_incl.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

// Forward-declare so the header stays lightweight.
namespace pugi { class xml_document; }

#define LUAXML "LUAXML"

// Lua owns the LuaXml struct (raw userdata, zeroed on creation).
// C++ owns the xml_document via normal new/delete routed through kitsune
// allocators (operator new/delete are overridden globally in mem.cpp).
// This keeps ownership models separate and makes double-__gc safe
// because delete nullptr is a no-op.
typedef struct LuaXml {
	int                  indent;  // 0 = compact, 1 = indented
	pugi::xml_document* doc;     // heap-allocated; NULL until lua_xml_push

	// Anti-recursion stack for Encode (table pointer addresses on the call stack)
	uintptr_t* rec;
	size_t               recLen;
	size_t               recCap;

	char* outBuf;  // encode output buffer (kitsune_malloc, reused)
	size_t               outLen;
	size_t               outCap;
} LuaXml;

void    ensure_allocator(void);

LuaXml* lua_xml_push(lua_State* L);
LuaXml* lua_xml_check(lua_State* L, int idx);

int lua_xml_gc(lua_State* L);
int lua_xml_tostring(lua_State* L);
int lua_xml_new(lua_State* L);
int lua_xml_encode(lua_State* L);
int lua_xml_decode(lua_State* L);
