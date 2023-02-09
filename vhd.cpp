#include "vhd.h"
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h> 
#include <time.h>

#define VHD_BLOCK_SIZE 2097152

time_t get_seconds_since_2000()
{
	struct tm t;
	memset(&t, 0, sizeof(t));
	t.tm_year = 100;  // 100 years after 1900
	t.tm_mday = 1;    // 1st day of the month
	t.tm_isdst = -1;  // auto-detect DST
	return mktime(&t);
}

UINT32 complement_sum(BYTE * array, size_t size)
{
	UINT32 sum = 0;
	for (size_t i = 0; i < size; i++) {
		sum += array[i];
	}
	return ~sum;
}

int VhdCreate(lua_State* L) {

	const char* fileName = lua_tostring(L, 1);
	size_t size = luaL_checkinteger(L, 2);
	LuaVhd* vhd = lua_pushvhd(L);

	memcpy(&vhd->footer.Cookie, "conectix", 8);
	vhd->footer.Features = 0;
	vhd->footer.FileFormatVersion = 0x00010000;
	vhd->footer.DataOffset = sizeof(VhdFooter);
	vhd->footer.TimeStamp = get_seconds_since_2000();
	memcpy(&vhd->footer.CreatorApplication, "vs  ", 4);
	vhd->footer.CreatorVersion = 0x00010000;
	vhd->footer.CreatorHostOS = 0x5769326B;
	vhd->footer.OriginalSize = size;
	vhd->footer.CurrentSize = size;
	vhd->footer.DiskGeometry = 1;
	vhd->footer.DiskType = 3;
	vhd->footer.Checksum = 0; 
	memset(&vhd->footer.UniqueId, 0, 16);
	vhd->footer.SavedState = 0;
	memset(&vhd->footer.Reserved, 0, 427);

	vhd->footer.Checksum = complement_sum((BYTE*)&vhd->footer, sizeof(VhdFooter));

	memcpy(&vhd->header.Cookie, "cxsparse", 8);
	vhd->header.DataOffset = 0xFFFFFFFF;
	vhd->header.TableOffset = sizeof(VhdFooter) + sizeof(VhdHeader);
	vhd->header.HeaderVersion = 0x00010000;
	vhd->header.MaxTableEntries = ((size + VHD_BLOCK_SIZE - 1) / VHD_BLOCK_SIZE);
	vhd->header.BlockSize = VHD_BLOCK_SIZE;
	vhd->header.Checksum = 0;
	memset(&vhd->header.ParentUniqueID, 0, 16);
	vhd->header.ParentTimeStamp = 0;
	vhd->header.Reserved1 = 0;
	memset(&vhd->header.ParentUnicodeName, 0, 512);
	memset(&vhd->header.ParentLocatorEntry1, 0, 24);
	memset(&vhd->header.ParentLocatorEntry2, 0, 24);
	memset(&vhd->header.ParentLocatorEntry3, 0, 24);
	memset(&vhd->header.ParentLocatorEntry4, 0, 24);
	memset(&vhd->header.ParentLocatorEntry5, 0, 24);
	memset(&vhd->header.ParentLocatorEntry6, 0, 24);
	memset(&vhd->header.ParentLocatorEntry7, 0, 24);
	memset(&vhd->header.ParentLocatorEntry8, 0, 24);
	memset(&vhd->header.Reserved2, 0, 256);

	vhd->header.Checksum = complement_sum((BYTE*)&vhd->header, sizeof(VhdHeader));

	FILE* f = fopen(fileName, "wb");

	if (!f) {
		luaL_error(L, "Unable to create file %s", fileName);
		return 0;
	}

	fwrite(&vhd->footer, sizeof(VhdFooter), 1, f);
	fwrite(&vhd->header, sizeof(VhdHeader), 1, f);
	UINT32 batBlock = 0xFFFFFFFF;

	for (size_t i = 0; i < vhd->header.MaxTableEntries; i++)
	{
		fwrite(&batBlock, 4, 1, f);
	}

	fwrite(&vhd->footer, sizeof(VhdFooter), 1, f);
	fflush(f);
	fclose(f);

	return 1;
}

LuaVhd* lua_pushvhd(lua_State* L) {

	LuaVhd* vhd = (LuaVhd*)lua_newuserdata(L, sizeof(LuaVhd));

	if (vhd == NULL) {
		luaL_error(L, "Unable to push vhd");
		return	NULL;
	}

	luaL_getmetatable(L, VHD);
	lua_setmetatable(L, -2);
	memset(vhd, 0, sizeof(LuaVhd));

	return vhd;
}

LuaVhd* lua_tovhd(lua_State* L, int index) {
	LuaVhd* vhd = (LuaVhd*)luaL_checkudata(L, index, VHD);
	if (vhd == NULL)
		luaL_error(L, "parameter is not a %s", VHD);
	return vhd;
}

int vhd_gc(lua_State* L) {

	LuaVhd* vhd = lua_tovhd(L, 1);

	return 0;
}

int vhd_tostring(lua_State* L) {
	char tim[100];
	sprintf(tim, "VHD: 0x%08X", lua_tovhd(L, 1));
	lua_pushfstring(L, tim);
	return 1;
}