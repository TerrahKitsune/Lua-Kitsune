#pragma once
#include "lua_main_incl.h"
#include "KitsuneEngine.h"

struct LuaKitsuneUserdata {

	const char* name; // Name of the userdata
	void* userdata; // Opaque pointer passed to C functions registered in this userdata's metatable; null if it its userdata that was not registered by KitsuneRegisterUserdata
};

// Creates a new userdata registration but if it already exists it returns false.
// Returns false if registration->MetaTableFunctions contains no __gc or __tostring entry —
// both are mandatory: __gc frees the managed GCHandle, __tostring enables meaningful
// output from Lua's tostring() and print().  The C# RegisterUserdata<T> layer always
// injects defaults for both so this check mainly guards direct C++ callers.
// cfunctionWrapper must be the LuaCFunctionWrapper trampoline from KitsuneEngine.cpp.
bool lua_registerkitsuneuserdata(lua_State* L, const char* name, const KitsuneUserDataRegistration* registration, lua_CFunction cfunctionWrapper);