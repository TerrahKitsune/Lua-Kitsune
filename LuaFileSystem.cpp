#include "LuaFileSystem.h"
#include "luatext.h"
#include "kitsunestdlib.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define MAX_PATH_LENGTH 1024

// FileSystem.Open is io.open (UTF-8 path, nil, "<path>: <error>", errno on failure) with a
// stricter mode: '[rwa]%+?b?' only, where io.open also accepts repeated 'b'.
int OpenFileWide(lua_State* L) {
	return kitsune_io_open_strict(L);
}

// =========================================================
// Windows implementation
// =========================================================
#ifdef _WIN32

#include <Windows.h>
#include <winioctl.h>
#include <io.h>
#include <shlobj.h>

static wchar_t _PATHW[MAX_PATH_LENGTH];

#ifndef IO_REPARSE_TAG_LX_SYMLINK
#define IO_REPARSE_TAG_LX_SYMLINK (0xA000001DL)
#endif
#ifndef FILE_ATTRIBUTE_RECALL_ON_OPEN
#define FILE_ATTRIBUTE_RECALL_ON_OPEN 0x00040000
#endif
#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#endif

// FSCTL_GET_REPARSE_POINT output for the Microsoft link tags (REPARSE_DATA_BUFFER is only in the DDK).
// Name offsets and lengths are in bytes, relative to PathBuffer, and the names are not null-terminated.
typedef struct REPARSE_DATA {
	DWORD  ReparseTag;
	WORD   ReparseDataLength;
	WORD   Reserved;
	union {
		struct {
			WORD   SubstituteNameOffset;
			WORD   SubstituteNameLength;
			WORD   PrintNameOffset;
			WORD   PrintNameLength;
			DWORD  Flags;
			WCHAR  PathBuffer[1];
		} SymbolicLink;
		struct {
			WORD   SubstituteNameOffset;
			WORD   SubstituteNameLength;
			WORD   PrintNameOffset;
			WORD   PrintNameLength;
			WCHAR  PathBuffer[1];
		} MountPoint;
		struct {
			DWORD  Version;
			char   Target[1];  // UTF-8, not null-terminated
		} LxSymlink;
	};
} REPARSE_DATA;

// Internal: copy the UTF-8 string argument at idx into dst as a null-terminated wide string.
// Creates no Lua objects, so the result cannot be garbage collected mid-use.
// Returns the number of wchar_t written, or -1 if it does not fit in cap (including the terminator).
static int arg_to_wide(lua_State* L, int idx, wchar_t* dst, int cap) {

	size_t len;
	const char* s = luaL_checklstring(L, idx, &len);

	// A UTF-8 string never needs more wide units than bytes, so only a long one is measured.
	if (len + 1 > (size_t)cap && kitsune_utf8_wide_len(s, len) + 1 > (size_t)cap)
		return -1;
	return (int)kitsune_utf8_to_wide(s, len, dst);
}

// Internal: convert a UTF-8 path to wchar_t (normalises slashes, optionally appends "\*").
static const wchar_t* to_pathw(lua_State* L, int idx, bool wildcard = false) {

	int len = arg_to_wide(L, idx, _PATHW, MAX_PATH_LENGTH);
	if (len < 0)
		luaL_error(L, "path is too long (max %d characters)", MAX_PATH_LENGTH - 1);

	for (int n = 0; n < len; n++) {
		if (_PATHW[n] == L'/')
			_PATHW[n] = L'\\';
	}

	if (wildcard) {
		bool needsep = len > 0 && _PATHW[len - 1] != L'\\';
		if ((size_t)len + (needsep ? 2 : 1) >= MAX_PATH_LENGTH)
			luaL_error(L, "path is too long (max %d characters)", MAX_PATH_LENGTH - 1);
		if (needsep)
			wcscat(_PATHW, L"\\");
		wcscat(_PATHW, L"*");
	}

	return _PATHW;
}

static time_t FILETIME_to_time_t(const FILETIME* ft) {

	/* FILETIME is UTC in 100ns ticks since 1601-01-01; convert straight to
	   Unix seconds (no local-time round trip through mktime). */
	ULARGE_INTEGER ull;
	ull.LowPart = ft->dwLowDateTime;
	ull.HighPart = ft->dwHighDateTime;
	const unsigned long long epoch_diff = 116444736000000000ULL;
	if (ull.QuadPart < epoch_diff)
		return 0;
	return (time_t)((ull.QuadPart - epoch_diff) / 10000000ULL);
}

static void push_find_dataw(lua_State* L, const WIN32_FIND_DATAW* d) {

	lua_createtable(L, 0, 13);

	lua_pushstring(L, "FileName");
	lua_pushwideasutf8(L, d->cFileName);
	lua_settable(L, -3);

	lua_pushstring(L, "AlternateFileName");
	lua_pushwideasutf8(L, d->cAlternateFileName);
	lua_settable(L, -3);

	lua_pushstring(L, "isFolder");
	lua_pushboolean(L, (d->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
	lua_settable(L, -3);

	lua_pushstring(L, "Attributes");
	lua_pushinteger(L, d->dwFileAttributes);
	lua_settable(L, -3);

	lua_pushstring(L, "Size");
	lua_pushinteger(L, ((DWORD64)d->nFileSizeHigh << 32) | d->nFileSizeLow);
	lua_settable(L, -3);

	lua_pushstring(L, "Creation");
	lua_pushinteger(L, FILETIME_to_time_t(&d->ftCreationTime));
	lua_settable(L, -3);

	lua_pushstring(L, "Access");
	lua_pushinteger(L, FILETIME_to_time_t(&d->ftLastAccessTime));
	lua_settable(L, -3);

	lua_pushstring(L, "Write");
	lua_pushinteger(L, FILETIME_to_time_t(&d->ftLastWriteTime));
	lua_settable(L, -3);

	// dwReserved0 holds the reparse tag, but only when the entry is a reparse point.
	bool reparse = (d->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
	if (reparse) {
		lua_pushstring(L, "ReparseTag");
		lua_pushinteger(L, d->dwReserved0);
		lua_settable(L, -3);
	}

	// Name surrogates (junctions, mount points, symlinks) point at another path; cloud placeholders,
	// dedup and compressed files are reparse points too but are not links.
	lua_pushstring(L, "isLink");
	lua_pushboolean(L, reparse && IsReparseTagNameSurrogate(d->dwReserved0));
	lua_settable(L, -3);

	// Not (fully) on this device: OneDrive/cloud online-only files and folders, or HSM-offloaded files.
	// Reading one downloads it, and listing an unpopulated placeholder folder fetches its listing.
	lua_pushstring(L, "isPlaceholder");
	lua_pushboolean(L, (d->dwFileAttributes &
		(FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_OFFLINE)) != 0);
	lua_settable(L, -3);
}

// Internal: push a link target read from a substitute name ("\??\C:\x", "\??\UNC\srv\share",
// "\??\Volume{guid}\") as a Win32 path: drive paths lose the prefix, the rest get "\\?\".
static void push_substitute_name(lua_State* L, const wchar_t* name, size_t len) {

	if (len >= 4 && wcsncmp(name, L"\\??\\", 4) == 0) {
		name += 4;
		len -= 4;
		if (!(len >= 2 && name[1] == L':')) {
			lua_pushstring(L, "\\\\?\\");
			lua_pushwideasutf8(L, name, len);
			lua_concat(L, 2);
			return;
		}
	}
	lua_pushwideasutf8(L, name, len);
}

// Internal: add LinkType and Link to the FileInfo table on top of the stack for the link at path.
// Only called for name surrogates, so cloud placeholders are never opened (which could recall them).
static void push_link_target(lua_State* L, const wchar_t* path) {

	HANDLE fh = CreateFileW(path, 0,
		FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
		0, OPEN_EXISTING,
		FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, 0);

	if (fh == INVALID_HANDLE_VALUE)
		return;

	DWORD buf[MAXIMUM_REPARSE_DATA_BUFFER_SIZE / sizeof(DWORD)];
	const REPARSE_DATA* rp = (const REPARSE_DATA*)buf;
	DWORD ret = 0;
	BOOL ok = DeviceIoControl(fh, FSCTL_GET_REPARSE_POINT, NULL, 0, buf, sizeof(buf), &ret, NULL);
	CloseHandle(fh);

	if (!ok || ret < 8 || ret < 8u + rp->ReparseDataLength)
		return;

	const char* type = NULL;
	const WCHAR* pathbuf = NULL;
	size_t pathbytes = 0;
	WORD subOff = 0, subLen = 0, printOff = 0, printLen = 0;

	if (rp->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT && rp->ReparseDataLength >= 8) {
		pathbuf = rp->MountPoint.PathBuffer;
		pathbytes = rp->ReparseDataLength - 8;
		subOff = rp->MountPoint.SubstituteNameOffset;
		subLen = rp->MountPoint.SubstituteNameLength;
		printOff = rp->MountPoint.PrintNameOffset;
		printLen = rp->MountPoint.PrintNameLength;
		type = "junction";
	}
	else if (rp->ReparseTag == IO_REPARSE_TAG_SYMLINK && rp->ReparseDataLength >= 12) {
		pathbuf = rp->SymbolicLink.PathBuffer;
		pathbytes = rp->ReparseDataLength - 12;
		subOff = rp->SymbolicLink.SubstituteNameOffset;
		subLen = rp->SymbolicLink.SubstituteNameLength;
		printOff = rp->SymbolicLink.PrintNameOffset;
		printLen = rp->SymbolicLink.PrintNameLength;
		type = "symlink";
	}
	else if (rp->ReparseTag == IO_REPARSE_TAG_LX_SYMLINK && rp->ReparseDataLength >= 4) {
		lua_pushstring(L, "LinkType");
		lua_pushstring(L, "symlink");
		lua_settable(L, -3);
		lua_pushstring(L, "Link");
		lua_pushlstring(L, rp->LxSymlink.Target, rp->ReparseDataLength - 4);
		lua_settable(L, -3);
		return;
	}
	else {
		return;
	}

	if ((size_t)subOff + subLen > pathbytes || (size_t)printOff + printLen > pathbytes)
		return;

	const WCHAR* sub = (const WCHAR*)((const BYTE*)pathbuf + subOff);

	// A volume mount point is a mount-point tag whose target is a volume rather than a directory.
	if (rp->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT && subLen >= 22 && wcsncmp(sub, L"\\??\\Volume{", 11) == 0)
		type = "mount";

	lua_pushstring(L, "LinkType");
	lua_pushstring(L, type);
	lua_settable(L, -3);

	// The print name is the path as the user gave it (relative for relative symlinks); some tools
	// leave it empty, and volume mount points have none, so fall back to the substitute name.
	lua_pushstring(L, "Link");
	if (printLen > 0)
		lua_pushwideasutf8(L, (const WCHAR*)((const BYTE*)pathbuf + printOff), printLen / sizeof(WCHAR));
	else
		push_substitute_name(L, sub, subLen / sizeof(WCHAR));
	lua_settable(L, -3);
}

int GetCurrent(lua_State* L) {

	DWORD len = GetCurrentDirectoryW(MAX_PATH_LENGTH, _PATHW);
	lua_pushwideasutf8(L, _PATHW, (len < MAX_PATH_LENGTH) ? len : 0);
	return 1;
}

int GetSpecialFolder(lua_State* L) {

	if (SUCCEEDED(SHGetFolderPathW(NULL, (int)luaL_optinteger(L, 1, CSIDL_DESKTOPDIRECTORY), NULL, 0, _PATHW)))
		lua_pushwideasutf8(L, _PATHW);
	else
		lua_pushnil(L);
	return 1;
}

int GetFiles(lua_State* L) {

	const wchar_t* path = to_pathw(L, 1, true);
	WIN32_FIND_DATAW ffd;
	HANDLE h = FindFirstFileW(path, &ffd);
	lua_pop(L, lua_gettop(L));
	lua_newtable(L);
	int n = 0;

	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (wcscmp(ffd.cFileName, L".") != 0 &&
				wcscmp(ffd.cFileName, L"..") != 0 &&
				!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				lua_pushwideasutf8(L, ffd.cFileName);
				lua_rawseti(L, -2, ++n);
			}
		} while (FindNextFileW(h, &ffd));
		FindClose(h);
	}

	return 1;
}

int GetDirectories(lua_State* L) {

	const wchar_t* path = to_pathw(L, 1, true);
	WIN32_FIND_DATAW ffd;
	HANDLE h = FindFirstFileW(path, &ffd);
	lua_pop(L, lua_gettop(L));
	lua_newtable(L);
	int n = 0;

	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (wcscmp(ffd.cFileName, L".") != 0 &&
				wcscmp(ffd.cFileName, L"..") != 0 &&
				(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				lua_pushwideasutf8(L, ffd.cFileName);
				lua_rawseti(L, -2, ++n);
			}
		} while (FindNextFileW(h, &ffd));
		FindClose(h);
	}

	return 1;
}

int GetAllInFolder(lua_State* L) {

	const wchar_t* path = to_pathw(L, 1, true);
	WIN32_FIND_DATAW ffd;
	HANDLE h = FindFirstFileW(path, &ffd);
	lua_pop(L, lua_gettop(L));
	lua_newtable(L);
	int n = 0;

	// Directory prefix (path without the trailing "*") for building a link's full path.
	wchar_t full[MAX_PATH_LENGTH + MAX_PATH];
	size_t dirlen = wcslen(path) - 1;
	wmemcpy(full, path, dirlen);

	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (wcscmp(ffd.cFileName, L".") != 0 && wcscmp(ffd.cFileName, L"..") != 0) {
				push_find_dataw(L, &ffd);
				if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && IsReparseTagNameSurrogate(ffd.dwReserved0)) {
					wcscpy(full + dirlen, ffd.cFileName);
					push_link_target(L, full);
				}
				lua_rawseti(L, -2, ++n);
			}
		} while (FindNextFileW(h, &ffd));
		FindClose(h);
	}

	return 1;
}

int GetFileInfo(lua_State* L) {

	const wchar_t* path = to_pathw(L, 1);
	WIN32_FIND_DATAW data;
	HANDLE h = FindFirstFileW(path, &data);

	if (h == INVALID_HANDLE_VALUE) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}

	FindClose(h);
	lua_pop(L, 1);
	push_find_dataw(L, &data);

	if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && IsReparseTagNameSurrogate(data.dwReserved0))
		push_link_target(L, path);

	return 1;
}

int lua_CopyFile(lua_State* L) {

	const wchar_t* src = to_pathw(L, 1);
	wchar_t srccopy[MAX_PATH_LENGTH];
	wcsncpy(srccopy, src, MAX_PATH_LENGTH - 1);
	srccopy[MAX_PATH_LENGTH - 1] = 0;
	const wchar_t* dst = to_pathw(L, 2);
	BOOL no_overwrite = !lua_toboolean(L, 3);
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, CopyFileW(srccopy, dst, no_overwrite));
	return 1;
}

int lua_MoveFile(lua_State* L) {

	const wchar_t* src = to_pathw(L, 1);
	wchar_t srccopy[MAX_PATH_LENGTH];
	wcsncpy(srccopy, src, MAX_PATH_LENGTH - 1);
	srccopy[MAX_PATH_LENGTH - 1] = 0;
	const wchar_t* dst = to_pathw(L, 2);
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, MoveFileW(srccopy, dst));
	return 1;
}

int lua_DeleteFile(lua_State* L) {

	const wchar_t* path = to_pathw(L, 1);
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, DeleteFileW(path));
	return 1;
}

int lua_CreateDirectory(lua_State* L) {

	const wchar_t* path = to_pathw(L, 1);
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, CreateDirectoryW(path, NULL));
	return 1;
}

int lua_RemoveDirectory(lua_State* L) {

	const wchar_t* path = to_pathw(L, 1);
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, RemoveDirectoryW(path));
	return 1;
}

int lua_Rename(lua_State* L) {

	const wchar_t* src = to_pathw(L, 1);
	wchar_t srccopy[MAX_PATH_LENGTH];
	wcsncpy(srccopy, src, MAX_PATH_LENGTH - 1);
	srccopy[MAX_PATH_LENGTH - 1] = 0;
	const wchar_t* dst = to_pathw(L, 2);
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, MoveFileW(srccopy, dst));
	return 1;
}

int lua_TempFile(lua_State* L) {

	wchar_t temp[MAX_PATH_LENGTH];
	GetTempPathW(MAX_PATH_LENGTH, temp);

	if (lua_gettop(L) <= 0 || !lua_toboolean(L, 1)) {
		wchar_t file[MAX_PATH_LENGTH];
		if (GetTempFileNameW(temp, L"gff", 0, file)) {
			lua_pushwideasutf8(L, file);
			return 1;
		}
	}

	lua_pushwideasutf8(L, temp);
	return 1;
}

int lua_SetCurrentDirectory(lua_State* L) {

	const wchar_t* path = to_pathw(L, 1);
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, SetCurrentDirectoryW(path));
	return 1;
}

int lua_SetFileAttributes(lua_State* L) {

	const wchar_t* path = to_pathw(L, 1);
	DWORD mask = (DWORD)luaL_checkinteger(L, 2);
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, SetFileAttributesW(path, mask));
	return 1;
}

static void PushDrive(lua_State* L, const char* drive) {

	ULARGE_INTEGER fc, tot, tf;
	DWORD type = GetDriveType(drive);

	if (!GetDiskFreeSpaceExA(drive, &fc, &tot, &tf)) {
		memset(&fc,  0, sizeof(fc));
		memset(&tot, 0, sizeof(tot));
		memset(&tf,  0, sizeof(tf));
	}

	lua_createtable(L, 0, 5);

	lua_pushstring(L, "Drive");
	lua_pushfstring(L, "%c", drive[0]);
	lua_settable(L, -3);

	lua_pushstring(L, "Type");
	lua_pushinteger(L, type);
	lua_settable(L, -3);

	lua_pushstring(L, "FreeBytesAvailableToCaller");
	lua_pushinteger(L, fc.QuadPart);
	lua_settable(L, -3);

	lua_pushstring(L, "TotalNumberOfBytes");
	lua_pushinteger(L, tot.QuadPart);
	lua_settable(L, -3);

	lua_pushstring(L, "TotalNumberOfFreeBytes");
	lua_pushinteger(L, tf.QuadPart);
	lua_settable(L, -3);
}

int lua_GetAllAvailableDrives(lua_State* L) {

	size_t len;
	const char* opt = luaL_optlstring(L, 1, NULL, &len);
	char drive[5] = {0};
	strcpy(drive, "A:\\");

	if (opt != NULL) {
		char letter = (len == 1) ? (char)toupper(opt[0]) : 0;
		lua_pop(L, lua_gettop(L));
		if (letter >= 'A' && letter <= 'Z') {
			drive[0] = letter;
			PushDrive(L, drive);
		}
		else {
			lua_pushnil(L);
		}
		return 1;
	}

	DWORD drives = GetLogicalDrives();
	DWORD mask = 1;
	int n = 0;
	lua_pop(L, lua_gettop(L));
	lua_newtable(L);

	for (int c = 'A'; c <= 'Z'; c++, mask <<= 1) {
		if (drives & mask) {
			drive[0] = (char)c;
			PushDrive(L, drive);
			lua_rawseti(L, -2, ++n);
		}
	}

	return 1;
}

// =========================================================
// Linux implementation
// =========================================================
#else

#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>

static char _PATH[MAX_PATH_LENGTH];

// Internal: copy the UTF-8 path argument into a caller-supplied buffer.
static const char* dup_path(lua_State* L, int idx, char* buf, size_t bufsz) {

	size_t len;
	const char* p = luaL_checklstring(L, idx, &len);
	if (len >= bufsz)
		luaL_error(L, "path is too long (max %d characters)", (int)bufsz - 1);
	memcpy(buf, p, len + 1);
	return buf;
}

int GetCurrent(lua_State* L) {

	if (!getcwd(_PATH, sizeof(_PATH)))
		luaL_error(L, "getcwd failed: %s", strerror(errno));
	lua_pushstring(L, _PATH);
	return 1;
}

int GetSpecialFolder(lua_State* L) {
	(void)L;
	lua_pushnil(L);
	return 1;
}

int GetFiles(lua_State* L) {

	char dirpath[MAX_PATH_LENGTH];
	dup_path(L, 1, dirpath, sizeof(dirpath));
	lua_pop(L, lua_gettop(L));
	lua_newtable(L);
	int n = 0;
	DIR* dir = opendir(dirpath);

	if (dir) {
		struct dirent* e;
		while ((e = readdir(dir)) != NULL) {
			if (e->d_name[0] == '.')
				continue;
			if (e->d_type == DT_REG || e->d_type == DT_UNKNOWN) {
				lua_pushstring(L, e->d_name);
				lua_rawseti(L, -2, ++n);
			}
		}
		closedir(dir);
	}

	return 1;
}

int GetDirectories(lua_State* L) {

	char dirpath[MAX_PATH_LENGTH];
	dup_path(L, 1, dirpath, sizeof(dirpath));
	lua_pop(L, lua_gettop(L));
	lua_newtable(L);
	int n = 0;
	DIR* dir = opendir(dirpath);

	if (dir) {
		struct dirent* e;
		while ((e = readdir(dir)) != NULL) {
			if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
				continue;
			if (e->d_type == DT_DIR) {
				lua_pushstring(L, e->d_name);
				lua_rawseti(L, -2, ++n);
			}
		}
		closedir(dir);
	}

	return 1;
}

int GetAllInFolder(lua_State* L) {

	char dirpath[MAX_PATH_LENGTH];
	dup_path(L, 1, dirpath, sizeof(dirpath));
	lua_pop(L, lua_gettop(L));
	lua_newtable(L);
	int n = 0;
	DIR* dir = opendir(dirpath);

	if (dir) {
		struct dirent* e;
		while ((e = readdir(dir)) != NULL) {
			if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
				continue;

			char full[MAX_PATH_LENGTH];
			snprintf(full, sizeof(full), "%s/%s", dirpath, e->d_name);
			struct stat st;
			bool statok = stat(full, &st) == 0;

			struct stat lst;
			bool islink = e->d_type == DT_LNK ||
				(e->d_type == DT_UNKNOWN && lstat(full, &lst) == 0 && S_ISLNK(lst.st_mode));

			lua_createtable(L, 0, 10);

			lua_pushstring(L, "FileName");
			lua_pushstring(L, e->d_name);
			lua_settable(L, -3);

			// A link to a directory counts as a folder, as a junction does on Windows.
			lua_pushstring(L, "isFolder");
			if (e->d_type == DT_DIR || e->d_type == DT_REG)
				lua_pushboolean(L, e->d_type == DT_DIR);
			else
				lua_pushboolean(L, statok && S_ISDIR(st.st_mode));
			lua_settable(L, -3);

			lua_pushstring(L, "isLink");
			lua_pushboolean(L, islink);
			lua_settable(L, -3);

			lua_pushstring(L, "isPlaceholder");
			lua_pushboolean(L, false);
			lua_settable(L, -3);

			if (islink) {
				char link[MAX_PATH_LENGTH];
				ssize_t lr = readlink(full, link, sizeof(link) - 1);
				lua_pushstring(L, "LinkType");
				lua_pushstring(L, "symlink");
				lua_settable(L, -3);
				if (lr > 0) {
					lua_pushstring(L, "Link");
					lua_pushlstring(L, link, (size_t)lr);
					lua_settable(L, -3);
				}
			}

			if (statok) {
				lua_pushstring(L, "Size");
				lua_pushinteger(L, (lua_Integer)st.st_size);
				lua_settable(L, -3);

				lua_pushstring(L, "Creation");
				lua_pushinteger(L, (lua_Integer)st.st_ctime);
				lua_settable(L, -3);

				lua_pushstring(L, "Access");
				lua_pushinteger(L, (lua_Integer)st.st_atime);
				lua_settable(L, -3);

				lua_pushstring(L, "Write");
				lua_pushinteger(L, (lua_Integer)st.st_mtime);
				lua_settable(L, -3);
			}

			lua_rawseti(L, -2, ++n);
		}
					closedir(dir);
			}

			return 1;
		}

		int GetFileInfo(lua_State* L) {

	char path[MAX_PATH_LENGTH];
	dup_path(L, 1, path, sizeof(path));
	struct stat st;

	if (stat(path, &st) != 0) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}

	lua_pop(L, 1);
	lua_createtable(L, 0, 10);

	const char* fname = strrchr(path, '/');
	lua_pushstring(L, "FileName");
	lua_pushstring(L, fname ? fname + 1 : path);
	lua_settable(L, -3);

	lua_pushstring(L, "isFolder");
	lua_pushboolean(L, S_ISDIR(st.st_mode));
	lua_settable(L, -3);

	lua_pushstring(L, "Size");
	lua_pushinteger(L, (lua_Integer)st.st_size);
	lua_settable(L, -3);

	lua_pushstring(L, "Creation");
	lua_pushinteger(L, (lua_Integer)st.st_ctime);
	lua_settable(L, -3);

	lua_pushstring(L, "Access");
	lua_pushinteger(L, (lua_Integer)st.st_atime);
	lua_settable(L, -3);

	lua_pushstring(L, "Write");
	lua_pushinteger(L, (lua_Integer)st.st_mtime);
	lua_settable(L, -3);

	struct stat lst;
	bool islink = lstat(path, &lst) == 0 && S_ISLNK(lst.st_mode);

	lua_pushstring(L, "isLink");
	lua_pushboolean(L, islink);
	lua_settable(L, -3);

	lua_pushstring(L, "isPlaceholder");
	lua_pushboolean(L, false);
	lua_settable(L, -3);

	if (islink) {
		lua_pushstring(L, "LinkType");
		lua_pushstring(L, "symlink");
		lua_settable(L, -3);

		char link[MAX_PATH_LENGTH];
		ssize_t lr = readlink(path, link, sizeof(link) - 1);
		if (lr > 0) {
			link[lr] = '\0';
			lua_pushstring(L, "Link");
			lua_pushstring(L, link);
			lua_settable(L, -3);
		}
	}

	return 1;
}

int lua_CopyFile(lua_State* L) {

	char src[MAX_PATH_LENGTH];
	dup_path(L, 1, src, sizeof(src));
	char dst[MAX_PATH_LENGTH];
	dup_path(L, 2, dst, sizeof(dst));
	bool overwrite = lua_toboolean(L, 3) != 0;
	lua_pop(L, lua_gettop(L));

	if (!overwrite) {
		struct stat st;
		if (stat(dst, &st) == 0) {
			lua_pushboolean(L, false);
			return 1;
		}
	}

	int sfd = open(src, O_RDONLY);
	if (sfd < 0) {
		lua_pushboolean(L, false);
		return 1;
	}

	int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (dfd < 0) {
		close(sfd);
		lua_pushboolean(L, false);
		return 1;
	}

	char buf[65536];
	ssize_t nr;
	bool ok = true;

	while ((nr = read(sfd, buf, sizeof(buf))) > 0) {
		if (write(dfd, buf, (size_t)nr) != nr) {
			ok = false;
			break;
		}
	}

	close(sfd);
	close(dfd);
	lua_pushboolean(L, ok && nr >= 0);
	return 1;
}

int lua_MoveFile(lua_State* L) {

	char src[MAX_PATH_LENGTH];
	dup_path(L, 1, src, sizeof(src));
	char dst[MAX_PATH_LENGTH];
	dup_path(L, 2, dst, sizeof(dst));
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, rename(src, dst) == 0);
	return 1;
}

int lua_DeleteFile(lua_State* L) {

	char path[MAX_PATH_LENGTH];
	dup_path(L, 1, path, sizeof(path));
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, unlink(path) == 0 || rmdir(path) == 0);
	return 1;
}

int lua_CreateDirectory(lua_State* L) {

	char path[MAX_PATH_LENGTH];
	dup_path(L, 1, path, sizeof(path));
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, mkdir(path, 0755) == 0);
	return 1;
}

int lua_RemoveDirectory(lua_State* L) {

	char path[MAX_PATH_LENGTH];
	dup_path(L, 1, path, sizeof(path));
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, rmdir(path) == 0);
	return 1;
}

int lua_Rename(lua_State* L) {

	char src[MAX_PATH_LENGTH];
	dup_path(L, 1, src, sizeof(src));
	char dst[MAX_PATH_LENGTH];
	dup_path(L, 2, dst, sizeof(dst));
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, rename(src, dst) == 0);
	return 1;
}


int lua_TempFile(lua_State* L) {

	char tmpl[] = "/tmp/gff_XXXXXX";
	int fd = mkstemp(tmpl);

	if (fd < 0)
		luaL_error(L, "mkstemp failed: %s", strerror(errno));

	close(fd);
	lua_pushstring(L, tmpl);
	return 1;
}

int lua_SetCurrentDirectory(lua_State* L) {

	char path[MAX_PATH_LENGTH];
	dup_path(L, 1, path, sizeof(path));
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, chdir(path) == 0);
	return 1;
}

int lua_SetFileAttributes(lua_State* L) {

	(void)L;
	lua_pushboolean(L, false);
	return 1;
}

int lua_GetAllAvailableDrives(lua_State* L) {

	lua_pop(L, lua_gettop(L));
	lua_newtable(L);

	lua_createtable(L, 0, 4);

	lua_pushstring(L, "Drive");
	lua_pushstring(L, "/");
	lua_settable(L, -3);

	struct statvfs sv;
	if (statvfs("/", &sv) == 0) {
		lua_pushstring(L, "TotalNumberOfBytes");
		lua_pushinteger(L, (lua_Integer)((uint64_t)sv.f_blocks * sv.f_frsize));
		lua_settable(L, -3);

		lua_pushstring(L, "TotalNumberOfFreeBytes");
		lua_pushinteger(L, (lua_Integer)((uint64_t)sv.f_bfree * sv.f_frsize));
		lua_settable(L, -3);

		lua_pushstring(L, "FreeBytesAvailableToCaller");
		lua_pushinteger(L, (lua_Integer)((uint64_t)sv.f_bavail * sv.f_frsize));
		lua_settable(L, -3);
	}

	lua_rawseti(L, -2, 1);
	return 1;
}

#endif  // _WIN32
