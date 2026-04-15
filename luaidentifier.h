#pragma once
#include "lua_main_incl.h"
#include "platform.h"

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

int identifier_newuuid(lua_State* L);
int identifier_newoid(lua_State* L);
int identifier_fromstring(lua_State* L);
int identifier_frombytes(lua_State* L);
int identifier_gettype(lua_State* L);
int identifier_asbytes(lua_State* L);
int identifier_asstring(lua_State* L);
int identifier_isempty(lua_State* L);

int identifier_eq(lua_State* L);
int identifier_tostring(lua_State* L);

// Returns non-zero if the value at the given stack index is a LuaIdentifier.
int lua_isidentifier(lua_State* L, int index);
// Pushes the canonical string representation of the LuaIdentifier at the given index.
// The caller must ensure the value at index is a LuaIdentifier.
void lua_identifier_push_string(lua_State* L, int index);
// Parses a 36-char UUID string (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx) and pushes
// a LuaIdentifier. Returns the new identifier on success, or NULL and pushes nil on failure.
LuaIdentifier* lua_pushidentifier_fromstring(lua_State* L, const char* str, size_t len);
