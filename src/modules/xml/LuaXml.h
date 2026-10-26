#pragma once
#include "LuaMainInclude.h"
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
	pugi::xml_document* doc;     // heap-allocated; NULL until lua_pushxml

	// Anti-recursion stack for Encode (table pointer addresses on the call stack)
	uintptr_t* rec;
	size_t               recLen;
	size_t               recCap;

	char* outBuf;  // encode output buffer (kitsune_malloc, reused)
	size_t               outLen;
	size_t               outCap;
} LuaXml;

void    EnsureAllocator(void);

LuaXml* lua_pushxml(lua_State* L);
LuaXml* luaL_checkxml(lua_State* L, int idx);

int Xml_Dispose(lua_State* L);
int Xml_ToString(lua_State* L);
int Xml_New(lua_State* L);
int Xml_Encode(lua_State* L);
int Xml_Decode(lua_State* L);
