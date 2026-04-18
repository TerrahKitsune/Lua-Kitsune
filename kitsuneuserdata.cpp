#include "kitsuneuserdata.h"

bool lua_registerkitsuneuserdata(lua_State* L, const char* name, const KitsuneUserDataRegistration* registration, lua_CFunction cfunctionWrapper, void (*pushGCHook)(lua_State*, void*, kitsune_Finalizer)) {
	if (!L || !name || !registration || !cfunctionWrapper)
		return false;

	// __gc and __tostring are mandatory: __gc releases the managed GCHandle; __tostring
	// ensures Lua's tostring() and print() produce meaningful output.  Both must be
	// provided (the C# layer always injects defaults for both).  Fail before touching the
	// registry so there is no partial registration to clean up.
	bool hasGc = false;
	bool hasToString = false;
	const KitsuneNamedFunction* check = registration->MetaTableFunctions;
	while (check) {
		if (strcmp(check->name, "__gc") == 0)
			hasGc = true;
		else if (strcmp(check->name, "__tostring") == 0)
			hasToString = true;
		if (hasGc && hasToString)
			break;
		check = check->Next;
	}
	if (!hasGc || !hasToString)
		return false;

	// luaL_newmetatable returns 0 when the name is already registered; nothing to do.
	if (luaL_newmetatable(L, name) == 0) {
		lua_pop(L, 1);
		return false;
	}

	// Stack: [..., metatable]
	int meta_idx = lua_gettop(L);

	// __name lets FillKitsuneVariableFromStack read the type name when bridging back to C.
	lua_pushliteral(L, "__name");
	lua_pushstring(L, name);
	lua_rawset(L, meta_idx);

	// Sentinel field: the address of this function is unique per process.
	// FillKitsuneVariableFromStack checks for it to distinguish Kitsune-registered
	// userdatas (which expose their instance pointer) from foreign userdatas.
	lua_pushliteral(L, "__kitsune_userdata");
	lua_pushlightuserdata(L, (void*)lua_registerkitsuneuserdata);
	lua_rawset(L, meta_idx);

	// Build the methods table and set it as __index so obj:Method() works.
	lua_newtable(L);
	// Stack: [..., metatable, methods]
	int methods_idx = lua_gettop(L);

	const KitsuneNamedFunction* fn = registration->Functions;
	while (fn) {
		lua_pushstring(L, fn->name);
		lua_pushlightuserdata(L, (void*)fn->func);
		lua_pushlightuserdata(L, fn->userdata);
		int nupvals = 2;
		if (pushGCHook && fn->finalizer) {
			pushGCHook(L, fn->userdata, fn->finalizer);
			nupvals = 3;
		}
		lua_pushcclosure(L, cfunctionWrapper, nupvals);
		lua_rawset(L, methods_idx);
		fn = fn->Next;
	}

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, methods_idx);
	lua_rawset(L, meta_idx);

	// Pop the methods table; it is anchored via __index inside the metatable.
	lua_pop(L, 1);

	// Register metamethods (__gc, __tostring, etc.) directly on the metatable.
	fn = registration->MetaTableFunctions;
	while (fn) {
		lua_pushstring(L, fn->name);
		lua_pushlightuserdata(L, (void*)fn->func);
		lua_pushlightuserdata(L, fn->userdata);
		int nupvals = 2;
		if (pushGCHook && fn->finalizer) {
			pushGCHook(L, fn->userdata, fn->finalizer);
			nupvals = 3;
		}
		lua_pushcclosure(L, cfunctionWrapper, nupvals);
		lua_rawset(L, meta_idx);
		fn = fn->Next;
	}

	// Pop the metatable; it is anchored in the Lua registry by luaL_newmetatable.
	lua_pop(L, 1);
	return true;
}