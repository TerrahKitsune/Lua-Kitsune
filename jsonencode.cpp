#include "jsonencode.h"
#include "jsonutil.h"
#include "math.h"
#include "stream.h"
#include "luawchar.h"

void json_encodenumber(lua_State* L, JsonContext* context) {

	double numb = lua_tonumber(L, -1);
	char number[50];

	if (isfinite(numb)) {

		long long rounded = llround(numb);

		if (rounded == numb) {
			sprintf(number, "%lld", rounded);
		}
		else {
			sprintf(number, "%.16f", numb);
			for (size_t n = strlen(number) - 1; n > 0; n--) {
				if (number[n] == '0') {
					number[n] = '\0';
				}
				else if (number[n] == '.') {
					number[n + 1] = '0';
					break;
				}
				else {
					break;
				}
			}
		}
	}
	else if (isnan(numb)) {
		strcpy(number, "null");
	}
	else {

		if (numb == INFINITY) {
			strcpy(number, "1e+9999");
		}
		else if (numb == -INFINITY) {
			strcpy(number, "-1e+9999");
		}
		else {
			strcpy(number, "null");
		}
	}

	json_append(number, strlen(number), L, context);
}

void json_encodevalue(lua_State* L, JsonContext* context, int* depth) {

	if (json_isnull(L, context)) {

		json_append("null", 4, L, context);
		return;
	}

	switch (lua_type(L, -1)) {
	case LUA_TTABLE:
		json_encodetable(L, context, depth);
		break;
	case LUA_TNUMBER:
		json_encodenumber(L, context);
		break;
	case LUA_TBOOLEAN: {
		int b = lua_toboolean(L, -1);
		json_append(b ? "true" : "false", b ? 4 : 5, L, context);
		break;
	}
	default:
		if (lua_isnoneornil(L, -1)) {
			json_append("null", 4, L, context);
		}
		else {
			json_encodestring(L, context);
		}
		break;
	}
}

void json_encodestring(lua_State* L, JsonContext* C) {

	size_t len;
	const char* str;
	if (lua_isstring(L, -1)) {
		str = lua_tolstring(L, -1, &len);
	}
	else if (lua_isstream(L, -1)) {
		LuaStream* stream = lua_toluastream(L, -1);
		if (stream) {
			str = (const char*)stream->data;
			len = stream->len;
		}
		else {
			str = NULL;
			len = 0;
		}
	}
	else if (lua_iswchar(L, -1)) {
		ToUtf8(L);
		lua_copy(L, -1, -2);
		lua_pop(L, 1);
		str = lua_tolstring(L, -1, &len);
	}
	else {
		str = luaL_tolstring(L, -1, &len);
		lua_pop(L, 1);
	}
	char hex[7];

	json_append("\"", 1, L, C);
	for (size_t i = 0; i < len; i++)
	{
		switch (str[i]) {
		case '\"':
			json_append("\\\"", 2, L, C);
			break;
		case '\\':
			json_append("\\\\", 2, L, C);
			break;
		case '/':
			json_append("\\/", 2, L, C);
			break;
		case '\n':
			json_append("\\n", 2, L, C);
			break;
		case '\r':
			json_append("\\r", 2, L, C);
			break;
		default:

			if ((unsigned char)str[i] < 32) {
					sprintf(hex, "\\u%04x", (unsigned char)str[i]);
					json_append(hex, 6, L, C);
				}
			else {
				json_append(&str[i], 1, L, C);
			}

			break;
		}
	}

	json_append("\"", 1, L, C);
}

static void json_pad(int count, lua_State* L, JsonContext* C) {

	for (int i = 0; i < count; i++) {
		json_append("\t", 1, L, C);
	}
}

static int json_lua_pairs(lua_State* L) {

	if (lua_isnil(L, -1) && lua_isnil(L, -2)) {

		lua_pop(L, 2);

		lua_getglobal(L, "pairs");
		lua_pushvalue(L, -2);

		if (lua_pcall(L, 1, 3, NULL)) {
			lua_error(L);
			return 0;
		}

		lua_pushvalue(L, -3);
		lua_rotate(L, -2, -1);
		lua_rotate(L, -3, -1);
		lua_rotate(L, -2, -1);

		if (lua_pcall(L, 2, 2, NULL)) {
			lua_error(L);
			return 0;
		}

		return 3;
	}
	else if (!lua_isfunction(L, -2)) {
		return 0;
	}

	lua_pushvalue(L, -2);
	lua_pushvalue(L, -4);
	lua_pushvalue(L, -3);

	if (lua_pcall(L, 2, 2, NULL)) {
		lua_error(L);
		return 0;
	}

	lua_remove(L, -3);

	if (lua_isnil(L, -1) && lua_isnil(L, -2)) {
		lua_pop(L, 3);
		return 0;
	}

	return 3;
}

void json_encodetable(lua_State* L, JsonContext* C, int* depth) {

	if (C->refThreadInput != LUA_NOREF) {
		json_encodethread(L, C, depth);
		return;
	}

	int tbl = lua_absindex(L, -1);
	uintptr_t id = (uintptr_t)lua_topointer(L, tbl);

	if (json_existsinantirecursion(id, C)) {
		json_bail(L, C, "Recursion detected");
		return;
	}
	else if (!json_addtoantirecursion(id, C)) {

		lua_gc(L, LUA_GCCOLLECT, 0);

		if (!json_addtoantirecursion(id, C)) {
			json_bail(L, C, "Out of memory");
			return;
		}
	}

	lua_len(L, tbl);
	int size = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);

	int any = 0;
	lua_pushnil(L);
	lua_pushnil(L);
	if (json_lua_pairs(L) != 0) {
		if (!lua_isnil(L, -1) || !lua_isnil(L, -2)) {
			any = 1;
		}
		lua_pop(L, 3);
	}

	// Empty table
	if (any == 0 && size <= 0) {
		json_append("[]", 2, L, C);
	}
	// Object
	else if (size <= 0) {

		json_append("{", 1, L, C);
		if (depth) { (*depth)++; json_append("\n", 1, L, C); }

		int first = 1;
		lua_pushnil(L);
		lua_pushnil(L);
		while (json_lua_pairs(L) != 0) {

			if (!first) {
				json_append(depth ? ",\n" : ",", depth ? 2 : 1, L, C);
			}
			else {
				first = 0;
			}

			if (depth) json_pad(*depth, L, C);

			lua_pushvalue(L, -2);
			json_encodestring(L, C);
			lua_pop(L, 1);
			json_append(depth ? ": " : ":", depth ? 2 : 1, L, C);
			json_encodevalue(L, C, depth);
			lua_pop(L, 1);
		}

		if (depth) { (*depth)--; json_append("\n", 1, L, C); json_pad(*depth, L, C); }
		json_append("}", 1, L, C);
	}
	// Array
	else {

		json_append("[", 1, L, C);
		if (depth) { (*depth)++; json_append("\n", 1, L, C); }

		for (int i = 1; i <= size; i++) {
			if (depth) json_pad(*depth, L, C);
			lua_geti(L, tbl, i);
			json_encodevalue(L, C, depth);
			if (i < size) {
				json_append(depth ? ",\n" : ",", depth ? 2 : 1, L, C);
			}
			lua_pop(L, 1);
		}

		if (depth) { (*depth)--; json_append("\n", 1, L, C); json_pad(*depth, L, C); }
		json_append("]", 1, L, C);
	}

	json_removefromantirecursion(id, C);
}

void json_getnextthread(lua_State* L, JsonContext* C) {

	lua_rawgeti(L, LUA_REGISTRYINDEX, C->refThreadInput);

	int nresults;

	lua_State* T = lua_tothread(L, -1);
	int result = lua_resume(T, L, 0, &nresults);

	if (result == LUA_YIELD) {
		lua_pop(L, 1);
		lua_xmove(T, L, 2);
		lua_pop(T, lua_gettop(T));
	}
	else if (result == 0) {
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushnil(L);
	}
	else {
		lua_xmove(T, L, 1);
		const char* err = lua_tostring(L, -1);
		if (!err) {
			err = "Coroutine error";
		}
		json_bail(L, C, err);
	}
}

void json_encodethread(lua_State* L, JsonContext* C, int* depth) {

	char firstType = '\0';
	int count = 0;
	json_getnextthread(L, C);

	while (!lua_isnil(L, -1) || !lua_isnil(L, -2)) {

		if (firstType == '\0') {
			firstType = (lua_type(L, -2) == LUA_TSTRING) ? '}' : ']';
			char open = (firstType == '}') ? '{' : '[';
			json_append(&open, 1, L, C);
			if (depth) { (*depth)++; json_append("\n", 1, L, C); }
		}

		if (count++ > 0) {
			json_append(depth ? ",\n" : ",", depth ? 2 : 1, L, C);
		}
		if (depth) json_pad(*depth, L, C);

		if (firstType == ']') {
			json_encodevalue(L, C, depth);
		}
		else {
			lua_pushvalue(L, -2);
			json_encodestring(L, C);
			lua_pop(L, 1);
			json_append(depth ? ": " : ":", depth ? 2 : 1, L, C);
			json_encodevalue(L, C, depth);
		}

		lua_pop(L, 2);
		json_getnextthread(L, C);
	}

	lua_pop(L, 2);

	if (firstType == '\0') {
		json_append("[]", 2, L, C);
		return;
	}

	if (depth) { (*depth)--; json_append("\n", 1, L, C); json_pad(*depth, L, C); }
	json_append(&firstType, 1, L, C);
}