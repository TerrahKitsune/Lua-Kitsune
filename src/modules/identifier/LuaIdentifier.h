#pragma once
#include "LuaMainInclude.h"
#include "Platform.h"

static const char* LUAIDENTIFIER = "IDENTIFIER";

typedef enum {
	IDENTIFIER_UUID = 0,
	IDENTIFIER_OID  = 1
} IdentifierType;

typedef struct LuaIdentifier {
	IdentifierType type;
	uint8_t bytes[16];
	int len;
} LuaIdentifier;

LuaIdentifier* lua_pushidentifier(lua_State* L);
LuaIdentifier* lua_toidentifier(lua_State* L, int index);

int Identifier_NewUUID(lua_State* L);
int Identifier_NewOID(lua_State* L);
int Identifier_FromString(lua_State* L);
int Identifier_FromBytes(lua_State* L);
int Identifier_GetType(lua_State* L);
int Identifier_AsBytes(lua_State* L);
int Identifier_AsString(lua_State* L);
int Identifier_IsEmpty(lua_State* L);

int Identifier_Eq(lua_State* L);
int Identifier_ToString(lua_State* L);

// Returns non-zero if the value at the given stack index is a LuaIdentifier.
int lua_isidentifier(lua_State* L, int index);
// Pushes the canonical string representation of the LuaIdentifier at the given index.
// The caller must ensure the value at index is a LuaIdentifier.
void lua_pushidentifierstring(lua_State* L, int index);
// Parses a 36-char UUID string (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx) and pushes
// a LuaIdentifier. Returns the new identifier on success, or NULL and pushes nil on failure.
LuaIdentifier* lua_pushidentifierfromstring(lua_State* L, const char* str, size_t len);
