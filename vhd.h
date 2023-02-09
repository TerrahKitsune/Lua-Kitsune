#pragma once
#include "lua_main_incl.h"
#include <Windows.h>
static const char* VHD = "VHD";
#include <stdint.h>

typedef struct VhdFooter {

	BYTE Cookie[8];
	UINT32 Features;
	UINT32 FileFormatVersion;
	UINT64 DataOffset;
	UINT32 TimeStamp;
	BYTE CreatorApplication[4];
	UINT32 CreatorVersion;
	UINT32 CreatorHostOS;
	UINT64 OriginalSize;
	UINT64 CurrentSize;
	UINT32 DiskGeometry;
	UINT32 DiskType;
	UINT32 Checksum;
	BYTE UniqueId[16];
	BYTE SavedState;
	BYTE Reserved[427];

} VhdFooter;

typedef struct VhdHeader {

	BYTE Cookie[8];
	UINT64 DataOffset;
	UINT64 TableOffset;
	UINT32 HeaderVersion;
	UINT32 MaxTableEntries;
	UINT32 BlockSize;
	UINT32 Checksum;
	BYTE ParentUniqueID[16];
	UINT32 ParentTimeStamp;
	UINT32 Reserved1;
	BYTE ParentUnicodeName[512];
	BYTE ParentLocatorEntry1[24];
	BYTE ParentLocatorEntry2[24];
	BYTE ParentLocatorEntry3[24];
	BYTE ParentLocatorEntry4[24];
	BYTE ParentLocatorEntry5[24];
	BYTE ParentLocatorEntry6[24];
	BYTE ParentLocatorEntry7[24];
	BYTE ParentLocatorEntry8[24];
	BYTE Reserved2[256];

} VhdHeader;

typedef struct LuaVhd {

	VhdHeader header;
	VhdFooter footer;

} LuaVhd;

LuaVhd* lua_pushvhd(lua_State* L);
LuaVhd* lua_tovhd(lua_State* L, int index);

int VhdCreate(lua_State* L);

int vhd_gc(lua_State* L);
int vhd_tostring(lua_State* L);