#include "luacsv.h"
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h> 
#include <windows.h> 
#include "luawchar.h"

bool IsAtEnd(LuaCsv* csv) {

	if (csv->data) {
		return csv->pos >= csv->data->len;
	}
	else if (csv->file && fgetwc(csv->file) != WEOF) {
		fseek(csv->file, -1, SEEK_CUR);
		return false;
	}
	else {
		return true;
	}
}

wchar_t GetNext(LuaCsv* csv, bool peek = false) {

	wchar_t last;

	if (csv->data != NULL && csv->pos < csv->data->len) {

		last = csv->data->str[csv->pos];

		if (!peek) {
			csv->pos++;
		}
	}
	else if (csv->file) {

		last = fgetwc(csv->file);

		if (peek) {
			fseek(csv->file, -1, SEEK_CUR);
		}
		
		if (last == WEOF) {			
			last = L'\0';
		}		
	}
	else {
		last = L'\0';
	}

	csv->last = last;

	return last;
}

wchar_t SkipForwards(LuaCsv* csv) {

	wchar_t next = GetNext(csv, true);
	while (next == L' ' || next == L'\t') {
		next = GetNext(csv);
	}

	return next;
}

bool ResizeBuffer(LuaCsv* csv) {

	void * temp = gff_realloc(csv->buffer, (csv->alloc + 1024 + 1) * sizeof(wchar_t));
	if (!temp) {
		return false;
	}
	else {
		csv->buffer = (wchar_t*)temp;
		csv->alloc += 1024;
		return true;
	}
}

bool WriteToBuffer(LuaCsv* csv, wchar_t wc) {

	if (!csv->buffer || csv->len >= csv->alloc) {

		if (!ResizeBuffer(csv)) {
			return false;
		}
	}

	csv->buffer[csv->len++] = wc;
	csv->buffer[csv->len] = L'\0';

	return true;
}

void ClearBuffer(LuaCsv* csv, bool dealloc) {
	
	csv->len = 0;

	if (dealloc && csv->buffer) {
		gff_free(csv->buffer);
		csv->buffer = NULL;
	}
	else if(csv->buffer) {
		csv->buffer[csv->len] = L'\0';
	}

	if (csv->file != NULL && dealloc) {
		fclose(csv->file);
		csv->file = NULL;
	}

csv->data = NULL;
csv->pos = 0;
}

bool IsEndline(LuaCsv* csv) {

	if (csv->last == L'\n' || csv->last == L'\0') {
		return true;
	}
	else if (csv->last == L'\r') {

		wchar_t next = GetNext(csv, true);

		if (next == L'\n') {
			return GetNext(csv) == L'\n';
		}
		else {
			return false;
		}
	}
	else {
		return false;
	}
}

void PushAndClearbuffer(LuaCsv* csv, lua_State* L) {

	lua_pushwchar(L, csv->buffer, csv->len);
	ClearBuffer(csv, false);
}

void DecodeComments(LuaCsv* csv, lua_State* L) {

	wchar_t next = SkipForwards(csv);

	lua_createtable(L, 0, 0);

	if (next != L'*') {
		return;
	}

	int nth = 0;
	next = GetNext(csv);

	while (true) {

		while (!IsEndline(csv)) {
			if (!WriteToBuffer(csv, next)) {
				ClearBuffer(csv, true);
				luaL_error(L, "Out of memory");
				return;
			}
			next = GetNext(csv);
		}

		PushAndClearbuffer(csv, L);
		lua_rawseti(L, -2, ++nth);

		next = SkipForwards(csv);

		if (next != L'*') {
			break;
		}
	}
}

bool ReadCsvFieldIntoBuffer(LuaCsv* csv) {

	wchar_t wc = SkipForwards(csv);
	bool isSkipping = false;

	while (true) {

		wc = GetNext(csv, false);

		if (wc == L'"') {

			if (isSkipping && GetNext(csv, true) == L'"') {
				WriteToBuffer(csv, GetNext(csv));
			}
			else if (isSkipping) {

				while (true) {

					if (IsEndline(csv) || IsAtEnd(csv)) {
						return false;
					}
					else if (wc == csv->delimiter) {
						return true;
					}

					wc = GetNext(csv);
				}
			}
			else {
				isSkipping = true;
			}
		}
		else if (isSkipping) {
			if (IsAtEnd(csv)){
				break;
			}

			WriteToBuffer(csv, wc);
		}
		else if (wc == csv->delimiter) {
			return true;
		}
		else if (IsEndline(csv) || IsAtEnd(csv)) {
			break;
		}
		else {
			WriteToBuffer(csv, wc);
		}
	}

	return false;
}

void DecodeRows(LuaCsv* csv, lua_State* L) {

	bool result;
	wchar_t next = L'\0';
	int nth = 0;
	int subnth;

	lua_createtable(L, 0, 0);

	do{
		subnth = 0;
		lua_createtable(L, 0, 0);
		result = true;

		while (result) {

			result = ReadCsvFieldIntoBuffer(csv);
			PushAndClearbuffer(csv, L);
			lua_rawseti(L, -2, ++subnth);
		}

		lua_rawseti(L, -2, ++nth);

	}while (!IsAtEnd(csv));
}

void Decode(LuaCsv* csv, lua_State* L) {

	lua_createtable(L, 0, 2);

	lua_pushstring(L, "Comments");
	DecodeComments(csv, L);	
	lua_settable(L, -3);

	lua_pushstring(L, "Rows");
	DecodeRows(csv, L);
	lua_settable(L, -3);
}

int CreateCsv(lua_State* L) {

	LuaCsv* csv = lua_pushcsv(L);

	csv->delimiter = L',';

	return 1;
}

int DecodeFile(lua_State* L) {

	LuaCsv* csv = lua_tocsv(L, 1);

	ClearBuffer(csv, true);

	LuaWChar* fileName = lua_stringtowchar(L, 2);

	csv->file = _wfopen(fileName->str, L"r");
	
	if (!csv->file) {
		luaL_error(L, "Unable to open file");
		return 0;
	}

	Decode(csv, L);

	ClearBuffer(csv, true);

	return 1;
}

int DecodeString(lua_State* L) {

	LuaCsv* csv = lua_tocsv(L, 1);

	ClearBuffer(csv, true);

	csv->data = lua_stringtowchar(L, 2);
	csv->pos = 0;

	Decode(csv, L);

	ClearBuffer(csv, true);

	return 1;
}

LuaCsv* lua_pushcsv(lua_State* L) {
	LuaCsv* csv = (LuaCsv*)lua_newuserdata(L, sizeof(LuaCsv));
	if (csv == NULL)
		luaL_error(L, "Unable to push csv");
	luaL_getmetatable(L, LUACSV);
	lua_setmetatable(L, -2);
	memset(csv, 0, sizeof(LuaCsv));

	return csv;
}

LuaCsv* lua_tocsv(lua_State* L, int index) {
	LuaCsv* csv = (LuaCsv*)luaL_checkudata(L, index, LUACSV);
	if (csv == NULL)
		luaL_error(L, "parameter is not a %s", LUACSV);
	return csv;
}

int csv_gc(lua_State* L) {

	LuaCsv* csv = lua_tocsv(L, 1);

	ClearBuffer(csv, true);

	return 0;
}

int csv_tostring(lua_State* L) {
	char tim[100];
	sprintf(tim, "Csv: 0x%08X", lua_tocsv(L, 1));
	lua_pushfstring(L, tim);
	return 1;
}