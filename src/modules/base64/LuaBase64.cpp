#include "base64/LuaBase64.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static char g_encodingTable[] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
'w', 'x', 'y', 'z', '0', '1', '2', '3',
'4', '5', '6', '7', '8', '9', '+', '/' };
static char g_decodingTable[256];
static int modTable[] = { 0, 2, 1 };


char *Base64Encode(const unsigned char *data,
	size_t input_length,
	size_t *output_length) {

	*output_length = 4 * ((input_length + 2) / 3);

	char *encodedData = (char *)kitsune_malloc(*output_length);
	if (encodedData == NULL) return NULL;

	for (size_t i = 0, j = 0; i < input_length;) {

		uint32_t octetA = i < input_length ? (unsigned char)data[i++] : 0;
		uint32_t octetB = i < input_length ? (unsigned char)data[i++] : 0;
		uint32_t octetC = i < input_length ? (unsigned char)data[i++] : 0;

		uint32_t triple = (octetA << 0x10) + (octetB << 0x08) + octetC;

		encodedData[j++] = g_encodingTable[(triple >> 3 * 6) & 0x3F];
		encodedData[j++] = g_encodingTable[(triple >> 2 * 6) & 0x3F];
		encodedData[j++] = g_encodingTable[(triple >> 1 * 6) & 0x3F];
		encodedData[j++] = g_encodingTable[(triple >> 0 * 6) & 0x3F];
	}

	for (int i = 0; i < modTable[input_length % 3]; i++)
		encodedData[*output_length - 1 - i] = '=';

	return encodedData;
}


unsigned char *Base64Decode(const char *data,
	size_t input_length,
	size_t *output_length) {

	if (input_length == 0 || input_length % 4 != 0) return NULL;

	*output_length = input_length / 4 * 3;
	if (data[input_length - 1] == '=') (*output_length)--;
	if (data[input_length - 2] == '=') (*output_length)--;

	unsigned char *decodedData = (unsigned char *)kitsune_malloc(*output_length);
	if (decodedData == NULL) return NULL;

	for (size_t i = 0, j = 0; i < input_length;) {

		uint32_t sextetA = data[i] == '=' ? 0 : g_decodingTable[(unsigned char)data[i]];
		i++;
		uint32_t sextetB = data[i] == '=' ? 0 : g_decodingTable[(unsigned char)data[i]];
		i++;
		uint32_t sextetC = data[i] == '=' ? 0 : g_decodingTable[(unsigned char)data[i]];
		i++;
		uint32_t sextetD = data[i] == '=' ? 0 : g_decodingTable[(unsigned char)data[i]];
		i++;

		uint32_t triple = (sextetA << 3 * 6)
			+ (sextetB << 2 * 6)
			+ (sextetC << 1 * 6)
			+ (sextetD << 0 * 6);

		if (j < *output_length) decodedData[j++] = (triple >> 2 * 8) & 0xFF;
		if (j < *output_length) decodedData[j++] = (triple >> 1 * 8) & 0xFF;
		if (j < *output_length) decodedData[j++] = (triple >> 0 * 8) & 0xFF;
	}

	return decodedData;
}


void BuildDecodingTable() {

	memset(g_decodingTable, 0, sizeof(g_decodingTable));   // drop entries from a previous table
	for (int i = 0; i < 64; i++) {
		g_decodingTable[(unsigned char)g_encodingTable[i]] = i;
	}
}

int Base64_Encode(lua_State *L) {

	size_t len;
	const char * data = luaL_checklstring(L, 1, &len);
	
	size_t encodedlen;
	char* encoded = Base64Encode((const unsigned char*)data, len, &encodedlen);

	if (encoded) {
		lua_pushlstring(L, encoded, encodedlen);
		kitsune_free(encoded);
	}
	else {
		lua_pushnil(L);
	}

	return 1;
}

int Base64_Decode(lua_State *L) {

	size_t len;
	const char * data = luaL_checklstring(L, 1, &len);

	if (len == 0) {
		lua_pushliteral(L, "");
		return 1;
	}

	size_t encodedlen;
	unsigned char* decoded = Base64Decode(data, len, &encodedlen);

	if (decoded) {
		lua_pushlstring(L, (const char*)decoded, encodedlen);
		kitsune_free(decoded);
	}
	else {
		lua_pushnil(L);
	}

	return 1;
}

int Base64_GetEncodeTable(lua_State *L) {

	lua_pushlstring(L, g_encodingTable, 64);
	return 1;
}

int Base64_SetEncodeTable(lua_State *L) {

	size_t len;
	const char * data = luaL_checklstring(L, 1, &len);

	if (len != 64) {
		luaL_error(L, "Encoding table must be 64 bytes long");
		return 0;
	}

	for (size_t x = 0; x < len; x++)
	{
		for (size_t y = x+1; y < len; y++)
		{
			if (data[x] == data[y]) {
				luaL_error(L, "Entries in the encoding table must be unique");
				return 0;
			}
		}
	}

	memcpy(&g_encodingTable[0], data, 64);
	BuildDecodingTable();

	return 0;
}