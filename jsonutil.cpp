#include "jsonutil.h"

void json_pushnullornil(lua_State* L, JsonContext* context) {

	if (context->refNullValue == LUA_NOREF) {
		lua_pushnil(L);
	}
	else {
		lua_rawgeti(L, LUA_REGISTRYINDEX, context->refNullValue);
	}
}

bool json_isnull(lua_State* L, JsonContext* context) {

	if (context->refNullValue == LUA_NOREF) {

		return false;
	}
	else {

		lua_rawgeti(L, LUA_REGISTRYINDEX, context->refNullValue);
		
		int equal = lua_rawequal(L, -1, -2);
		lua_pop(L, 1);

		return equal != 0;
	}
}

void json_bail(lua_State *L, JsonContext* context, const char * err) {

	if (context->bufferFile) {
		fclose(context->bufferFile);

		// Delete the file on error
		if (err && context->fileName) {
			remove(context->fileName);
		}
	}

	if (context->readFile) {
		fclose(context->readFile);
		context->readFile = NULL;
	}

	if (context->buffer) {
		gff_free(context->buffer);
		context->buffer = NULL;
	}

	if (context->fileName) {
		gff_free(context->fileName);
		context->fileName = NULL;
	}

	if (context->antiRecursion) {
		gff_free(context->antiRecursion);
		context->antiRecursion = NULL;
	}

	if (context->readFileBuffer) {
		gff_free(context->readFileBuffer);
		context->readFileBuffer = NULL;
	}

	if (context->refWriteFunction != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, context->refWriteFunction);
		context->refWriteFunction = LUA_NOREF;
	}

	if (context->refReadFunction != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, context->refReadFunction);
		context->refReadFunction = LUA_NOREF;
	}

	if (context->refThreadInput != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, context->refThreadInput);
		context->refThreadInput = LUA_NOREF;
	}

	context->bufferFile = NULL;
	context->bufferLength = 0;
	context->bufferSize = 0;
	context->resultReallocStep = 0;
	context->read = NULL;
	context->readSize = 0;
	context->readCursor = 0;
	context->readLine = 0;
	context->readPosition = 0;
	context->quoteSymbol = 0;
	context->prevFileChar[0] = 0;
	context->prevFileChar[1] = 0;
	context->antiRecursionSize = 0;
	context->readFileBufferSize = 0;
	// refNullValue and pretty are intentionally preserved

	if (err) {
		luaL_error(L, err);
	}
}

void json_append(const char * data, size_t len, lua_State *L, JsonContext* context, bool isEnd) {

	if (context->bufferFile) {

		if (fwrite(data, sizeof(char), len, context->bufferFile) != len) {
			json_bail(L, context, "Failed to write to file");
		}
	}
	else {

		if (context->bufferLength + (len + 1) > context->bufferSize) {

			size_t newSize = (size_t)pow(2.0, (++context->resultReallocStep) > 20 ? 20 : context->resultReallocStep);

			if (newSize < JSONINITBUFFERSIZE) {
				newSize = JSONINITBUFFERSIZE;
			}

			if (newSize < len + 1) {
				newSize = len + 1;
			}

			newSize = context->bufferSize + (newSize * sizeof(char));

			void * temp = gff_realloc(context->buffer, newSize);
			if (!temp) {

				lua_gc(L, LUA_GCCOLLECT, 0);
				temp = gff_realloc(context->buffer, newSize);

				if (!temp) {
					json_bail(L, context, "Out of memory");
				}
			}
			else {
				context->buffer = (char*)temp;
				context->bufferSize = newSize;
			}
		}

		memcpy(&context->buffer[context->bufferLength], data, (len * sizeof(char)));
		context->bufferLength += (len * sizeof(char));
		context->buffer[context->bufferLength] = '\0';

		if (context->refWriteFunction != LUA_NOREF && context->bufferLength > 0 && (isEnd || context->bufferLength >= JSONFILEREADBUFFERSIZE)) {

			lua_rawgeti(L, LUA_REGISTRYINDEX, context->refWriteFunction);
			lua_pushlstring(L, context->buffer, context->bufferLength);
			context->bufferLength = 0;

			if (lua_pcall(L, 1, 0, NULL)) {

				const char * err = lua_tostring(L, -1);
				lua_pop(L, 1);

				if (!err) {
					err = "err";
				}

				json_bail(L, context, err);
			}
		}
	}
}

bool json_addtoantirecursion(uintptr_t id, JsonContext* context) {

	int idx = -1;
	if (context->antiRecursion) {
		for (size_t i = 0; i < context->antiRecursionSize; i++)
		{
			if (context->antiRecursion[i] == 0) {
				idx = (int)i;
				break;
			}
		}
	}

	if (idx == -1) {
		void*temp = gff_realloc(context->antiRecursion, (context->antiRecursionSize * sizeof(uintptr_t)) + (10 * sizeof(uintptr_t)));
		if (!temp) {
			return false;
		}

		context->antiRecursion = (uintptr_t*)temp;
		idx = (int)context->antiRecursionSize;
		memset(&context->antiRecursion[context->antiRecursionSize], 0, 10 * sizeof(uintptr_t));
		context->antiRecursionSize += 10;
	}

	context->antiRecursion[idx] = id;

	return true;
}

bool json_existsinantirecursion(uintptr_t id, JsonContext* context) {

	if (context->antiRecursion) {
		for (size_t i = 0; i < context->antiRecursionSize; i++)
		{
			if (context->antiRecursion[i] == id) {
				return true;
			}
		}
	}

	return false;
}

void json_removefromantirecursion(uintptr_t id, JsonContext* context) {

	if (context->antiRecursion) {
		for (size_t i = 0; i < context->antiRecursionSize; i++)
		{
			if (context->antiRecursion[i] == id) {
				context->antiRecursion[i] = 0;
				return;
			}
		}
	}
}

uintptr_t json_popfromantirecursion(JsonContext* context) {

	if (context->antiRecursion) {
		size_t len = 0;
		for (size_t i = 0; i < context->antiRecursionSize; i++)
		{
			if (context->antiRecursion[i] == 0) {
				len = i;
				break;
			}
		}

		if (len == 0) {
			return 0;
		}

		uintptr_t result = context->antiRecursion[len - 1];
		context->antiRecursion[len - 1] = 0;
		return result;
	}

	return 0;
}