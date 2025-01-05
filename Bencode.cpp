#include "Bencode.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

int DecodeValue(const char* data, size_t len, int pos, lua_State* L);

int BencodeDecode(lua_State* L) {

	size_t len;
	const char* data = luaL_checklstring(L, -1, &len);

	if (len == 0 || !data) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, 0, 0);
	int n = 0;
	int pos = 0;
	while (pos < len) {
		pos = DecodeValue(data, len, pos, L);
		lua_rawseti(L, -2, ++n);
	}

	return 1;
}

int DecodeInt(const char* data, size_t len, int pos, lua_State* L) {

	int end = -1;
	for (size_t i = pos; i < len; i++)
	{
		if (data[i] == 'e') {
			end = i;
			break;
		}
	}

	if (end < 0) {
		luaL_error(L, "Missing bencode terminator decoding int");
		return 0;
	}

	char* buffer = (char*)gff_calloc(end - pos + 1, sizeof(char));
	if (!buffer) {
		luaL_error(L, "Bencode decode out of memory");
		return 0;
	}

	memcpy(buffer, &data[pos], end - pos);
	int result = atoi(buffer);
	gff_free(buffer);

	lua_pushinteger(L, result);

	return end + 1;
}

int DecodeList(const char* data, size_t len, int pos, lua_State* L) {

	lua_createtable(L, 0, 0);
	int n = 0;
	char type = data[pos];
	while (type != 'e') {
		pos = DecodeValue(data, len, pos, L);

		lua_rawseti(L, -2, ++n);
		type = data[pos];
	}

	return pos + 1;
}

int DecodeString(const char* data, size_t len, int pos, lua_State* L) {

	int end = -1;
	for (size_t i = pos; i < len; i++)
	{
		if (data[i] == ':') {
			end = i;
			break;
		}
	}

	if (end < 0) {
		luaL_error(L, "Missing bencode binary string length seperator");
		return 0;
	}

	char* buffer = (char*)gff_calloc(end - pos + 1, sizeof(char));
	if (!buffer) {
		luaL_error(L, "Bencode decode out of memory");
		return 0;
	}

	memcpy(buffer, &data[pos], end - pos);
	int length = atoi(buffer);
	gff_free(buffer);

	pos = end + 1;

	if (length < 0 || length + pos >= len) {
		luaL_error(L, "Bencode decode binary string invalid length");
		return 0;
	}

	lua_pushlstring(L, &data[pos], length);

	return pos + length;
}

int DecodeDictionary(const char* data, size_t len, int pos, lua_State* L) {

	lua_createtable(L, 0, 0);
	char type = data[pos];

	while (type != 'e') {

		if (!isdigit(data[pos])) {
			luaL_error(L, "Invalid bencode dictonary key type is not a binary string");
		}

		pos = DecodeString(data, len, pos, L);
		pos = DecodeValue(data, len, pos, L);
		lua_settable(L, -3);
		type = data[pos];
	}

	return pos + 1;
}

int DecodeValue(const char* data, size_t len, int pos, lua_State* L) {

	char bencodeType = data[pos];

	if (bencodeType == 'i') {
		return DecodeInt(data, len, pos + 1, L);
	}
	else if (bencodeType == 'l') {
		return DecodeList(data, len, pos + 1, L);
	}
	else if (bencodeType == 'd') {
		return DecodeDictionary(data, len, pos + 1, L);
	}
	else if (isdigit(bencodeType)) {
		return DecodeString(data, len, pos, L);
	}
	else {
		luaL_error(L, "Invalid bencode type %c", (int)bencodeType);
	}

	return 0;
}