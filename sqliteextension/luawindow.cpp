#include "luasqlite.h"

typedef struct {
	lua_State* L;
	int function_ref;
	int context_ref;
} WindowContext;

static void WindowStep(sqlite3_context* ctx, int nArg, sqlite3_value* apArg[]) {

}

static void WindowInverse(sqlite3_context* ctx, int nArg, sqlite3_value* apArg[]) {

}

static void WindowFinal(sqlite3_context* ctx) {

}

static void WindowValue(sqlite3_context* ctx) {

}

static void DestroyContext(void* ptr) {

	WindowContext* ctx = (WindowContext*)ptr;

	if (ctx->function_ref != LUA_NOREF) {
		luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->function_ref);
	}

	if (ctx->context_ref != LUA_NOREF) {
		luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->context_ref);
	}

	sqlite3_free(ptr);
}

int sqlite3_registerwindow(lua_State* L, ResState* state) {

	const char* name = luaL_checkstring(L, 1);
	luaL_checktype(L, -1, LUA_TFUNCTION);

	ResRegistration* reg = GetRegistration(state, name);
	WindowContext* ctx;

	if (reg && reg->type != RES_TYPE_WINDOW) {
		luaL_error(L, "%s is already a registered sqlite resource", name);
		return 0;
	}
	else if (reg) {

		ctx = (WindowContext*)reg->ptr;

		if (ctx->function_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, ctx->function_ref);
		}

		if (ctx->context_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, ctx->context_ref);
		}

		ctx->context_ref = LUA_NOREF;
		ctx->function_ref = luaL_ref(L, LUA_REGISTRYINDEX);

		return 0;
	}

	ctx = (WindowContext*)sqlite3_malloc(sizeof(WindowContext));
	if (!ctx) {
		luaL_error(L, "Out of memory");
		return 0;
	}

	ctx->L = L;
	ctx->function_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	ctx->context_ref = LUA_NOREF;

	sqlite3_create_window_function(state->db, name, -1, SQLITE_UTF8, ctx, WindowStep, WindowFinal, WindowValue, WindowInverse, DestroyContext);

	reg = AddRegistration(state, name, RES_TYPE_WINDOW);
	reg->ptr = ctx;

	return 0;
}