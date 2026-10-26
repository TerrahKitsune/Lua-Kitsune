#include "filesystem/LuaFileSystemMain.h"
#include "filesystem/LuaFileSystem.h"

static const struct luaL_Reg filesystemfuncs[] = {
	{ "GetAll",              FileSystem_GetAll },
	{ "GetFiles",            FileSystem_GetFiles },
	{ "GetDirectories",      FileSystem_GetDirectories },
	{ "GetFileInfo",         FileSystem_GetFileInfo },
	{ "Open",                FileSystem_Open },
	{ "Copy",                FileSystem_Copy },
	{ "Move",                FileSystem_Move },
	{ "Delete",              FileSystem_Delete },
	{ "CreateDirectory",     FileSystem_CreateDirectory },
	{ "RemoveDirectory",     FileSystem_RemoveDirectory },
	{ "Rename",              FileSystem_Rename },
	{ "CurrentDirectory",    FileSystem_CurrentDirectory },
	{ "GetTempFileName",     FileSystem_GetTempFileName },
	{ "SetCurrentDirectory", FileSystem_SetCurrentDirectory },
	{ "GetDrives",           FileSystem_GetDrives },
	{ "SetAttributes",       FileSystem_SetAttributes },
#ifdef _WIN32
	{ "GetSpecialFolder",    FileSystem_GetSpecialFolder },
#endif
	{ NULL, NULL }
};

int luaopen_filesystem(lua_State* L) {

	luaL_newlibtable(L, filesystemfuncs);
	luaL_setfuncs(L, filesystemfuncs, 0);

	return 1;
}