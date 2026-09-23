#include "kitsunestdlib.h"
#include "kitsunefile.h"
#include "luatext.h"
#include "platform.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The functions below mirror the stock implementations in Lua/liolib.c, loslib.c,
// lbaselib.c, lauxlib.c and loadlib.c (Lua 5.5); only the CRT calls differ. Keep them in
// step when Lua is updated. The UTF-8 path tests in KitsuneNet.Tests cover each one.

typedef luaL_Stream LStream;

// -- io.open -----------------------------------------------------------------------

static int stream_fclose(lua_State* L) {

	LStream* p = (LStream*)luaL_checkudata(L, 1, LUA_FILEHANDLE);
	errno = 0;
	return luaL_fileresult(L, fclose(p->f) == 0, NULL);
}

// liolib's newprefile: the handle starts out 'closed', so a memory error leaves it consistent.
static LStream* new_prefile(lua_State* L) {

	LStream* p = (LStream*)lua_newuserdatauv(L, sizeof(LStream), 0);
	p->closef = NULL;
	luaL_setmetatable(L, LUA_FILEHANDLE);
	p->f = NULL;
	return p;
}

static LStream* new_file(lua_State* L) {

	LStream* p = new_prefile(L);
	p->closef = &stream_fclose;
	return p;
}

// liolib's l_checkmode: '[rwa]%+?b*'. (kitsune_fopen additionally rejects repeated 'b',
// which the MSVC CRT would abort on, with EINVAL.)
static int check_mode(const char* mode) {

	return *mode != '\0' && strchr("rwa", *(mode++)) != NULL &&
		(*mode != '+' || ((void)(++mode), 1)) &&
		(strspn(mode, "b") == strlen(mode));
}

// liolib's io_open. 'strict' also rejects a repeated 'b' (FileSystem.Open's contract).
static int open_file(lua_State* L, bool strict) {

	const char* filename = luaL_checkstring(L, 1);
	const char* mode = luaL_optstring(L, 2, "r");
	LStream* p = new_file(L);
	luaL_argcheck(L, check_mode(mode) && (!strict || strchr(mode, 'b') == strrchr(mode, 'b')), 2, "invalid mode");
	errno = 0;
	p->f = kitsune_fopen(filename, mode);
	return (p->f == NULL) ? luaL_fileresult(L, 0, filename) : 1;
}

int kitsune_io_open(lua_State* L) {
	return open_file(L, false);
}

int kitsune_io_open_strict(lua_State* L) {
	return open_file(L, true);
}

// -- luaL_loadfilex -------------------------------------------------------------------

#ifdef _WIN32

typedef struct LoadF {
	unsigned n;         // number of pre-read characters
	FILE* f;            // file being read
	char buff[BUFSIZ];  // area for reading file
} LoadF;

static const char* getF(lua_State* L, void* ud, size_t* size) {

	LoadF* lf = (LoadF*)ud;
	(void)L;
	if (lf->n > 0) {
		*size = lf->n;
		lf->n = 0;
	}
	else {
		// fread can return > 0 and set EOF; stopping here avoids waiting for more input.
		if (feof(lf->f))
			return NULL;
		*size = fread(lf->buff, 1, sizeof(lf->buff), lf->f);
	}
	return lf->buff;
}

static int errfile(lua_State* L, const char* what, int fnameindex) {

	int err = errno;
	const char* filename = lua_tostring(L, fnameindex) + 1;
	if (err != 0)
		lua_pushfstring(L, "cannot %s %s: %s", what, filename, strerror(err));
	else
		lua_pushfstring(L, "cannot %s %s", what, filename);
	lua_remove(L, fnameindex);
	return LUA_ERRFILE;
}

// Skips an optional UTF-8 BOM; an incomplete one returns its first byte to force an error.
static int skipBOM(FILE* f) {

	int c = getc(f);
	if (c == 0xEF && getc(f) == 0xBB && getc(f) == 0xBF)
		return getc(f);
	return c;
}

// Skips the BOM and a first line starting with '#'; *cp gets the first "valid" character.
static int skipcomment(FILE* f, int* cp) {

	int c = *cp = skipBOM(f);
	if (c == '#') {
		do {
			c = getc(f);
		} while (c != EOF && c != '\n');
		*cp = getc(f);
		return 1;
	}
	return 0;
}

int kitsune_loadfilex(lua_State* L, const char* filename, const char* mode) {

	if (filename == NULL)
		return luaL_loadfilex(L, NULL, mode);  // stdin

	LoadF lf;
	int c;
	int fnameindex = lua_gettop(L) + 1;
	lua_pushfstring(L, "@%s", filename);
	errno = 0;
	lf.f = kitsune_fopen(filename, "r");
	if (lf.f == NULL)
		return errfile(L, "open", fnameindex);

	lf.n = 0;
	if (skipcomment(lf.f, &c))
		lf.buff[lf.n++] = '\n';  // keep line numbers correct
	if (c == LUA_SIGNATURE[0]) {  // precompiled chunk: reopen in binary mode
		lf.n = 0;
		errno = 0;
		lf.f = kitsune_freopen(filename, "rb", lf.f);
		if (lf.f == NULL)  // the old stream was closed by kitsune_freopen; don't close it again
			return errfile(L, "reopen", fnameindex);
		skipcomment(lf.f, &c);
	}
	if (c != EOF)
		lf.buff[lf.n++] = (char)c;

	// lua_load runs protected, so the file is always closed below.
	int status = lua_load(L, getF, &lf, lua_tostring(L, -1), mode);
	int readstatus = ferror(lf.f);
	errno = 0;
	fclose(lf.f);
	if (readstatus) {
		lua_settop(L, fnameindex);
		return errfile(L, "read", fnameindex);
	}
	lua_remove(L, fnameindex);
	return status;
}

#else

int kitsune_loadfilex(lua_State* L, const char* filename, const char* mode) {
	return luaL_loadfilex(L, filename, mode);
}

#endif

#ifdef _WIN32

// -- io.lines / io.input / io.output / io.popen -----------------------------------------

// liolib's opencheck: pushes the opened file or raises.
static void open_check(lua_State* L, const char* fname, const char* mode) {

	LStream* p = new_file(L);
	p->f = kitsune_fopen(fname, mode);
	if (p->f == NULL)
		luaL_error(L, "cannot open file '%s' (%s)", fname, strerror(errno));
}

// Calls upvalue 1 (the stock function) with the current arguments.
static int call_stock(lua_State* L) {

	lua_pushvalue(L, lua_upvalueindex(1));
	lua_insert(L, 1);
	lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
	return lua_gettop(L);
}

// io.lines(filename, ...). The iterator is the stock one from file:lines(...), marked
// as owning the file (its third upvalue, 'toclose') so it closes it at the end, exactly
// as stock io.lines builds it. Upvalue 1: stock io.lines (used without a file name).
static int utf8_io_lines(lua_State* L) {

	if (lua_isnone(L, 1))
		lua_pushnil(L);
	if (lua_isnil(L, 1))
		return call_stock(L);  // default input file

	const char* filename = luaL_checkstring(L, 1);
	int nformats = lua_gettop(L) - 1;
	open_check(L, filename, "r");
	lua_replace(L, 1);  // file at 1, formats after it

	lua_getfield(L, 1, "lines");
	lua_insert(L, 2);
	lua_pushvalue(L, 1);
	lua_insert(L, 3);  // file:lines, file, formats...
	lua_call(L, nformats + 1, 1);  // stack: file, iterator

	// Guard against a changed iterator layout in a future Lua version.
	if (lua_getupvalue(L, 2, 3) == NULL || lua_type(L, -1) != LUA_TBOOLEAN)
		return luaL_error(L, "io.lines: unexpected iterator layout (update kitsunestdlib.cpp)");
	lua_pop(L, 1);
	lua_pushboolean(L, 1);
	lua_setupvalue(L, 2, 3);

	lua_pushnil(L);       // state
	lua_pushnil(L);       // control
	lua_pushvalue(L, 1);  // file is the to-be-closed variable (4th result)
	return 4;
}

// io.input / io.output. A file name is opened here and handed to the stock function as a
// file handle. Upvalue 1: stock function; upvalue 2: open mode.
static int utf8_io_file(lua_State* L) {

	if (!lua_isnoneornil(L, 1) && lua_isstring(L, 1)) {
		open_check(L, lua_tostring(L, 1), lua_tostring(L, lua_upvalueindex(2)));
		lua_replace(L, 1);
		lua_settop(L, 1);
	}
	return call_stock(L);
}

static int stream_pclose(lua_State* L) {

	LStream* p = (LStream*)luaL_checkudata(L, 1, LUA_FILEHANDLE);
	errno = 0;
	return luaL_execresult(L, _pclose(p->f));
}

static int utf8_io_popen(lua_State* L) {

	const char* command = luaL_checkstring(L, 1);
	const char* mode = luaL_optstring(L, 2, "r");
	LStream* p = new_prefile(L);
	// Windows accepts "[rw][bt]?" (liolib's l_checkmodep for Windows).
	luaL_argcheck(L, (mode[0] == 'r' || mode[0] == 'w') &&
		(mode[1] == '\0' || ((mode[1] == 'b' || mode[1] == 't') && mode[2] == '\0')), 2, "invalid mode");
	errno = 0;
	p->f = kitsune_popen(command, mode);
	p->closef = &stream_pclose;
	return (p->f == NULL) ? luaL_fileresult(L, 0, command) : 1;
}

// -- os ----------------------------------------------------------------------------

static int utf8_os_execute(lua_State* L) {

	const char* cmd = luaL_optstring(L, 1, NULL);
	errno = 0;
	int stat = kitsune_system(cmd);
	if (cmd != NULL)
		return luaL_execresult(L, stat);
	lua_pushboolean(L, stat);  // true if there is a shell
	return 1;
}

static int utf8_os_remove(lua_State* L) {

	const char* filename = luaL_checkstring(L, 1);
	errno = 0;
	return luaL_fileresult(L, kitsune_remove(filename) == 0, filename);
}

static int utf8_os_rename(lua_State* L) {

	const char* fromname = luaL_checkstring(L, 1);
	const char* toname = luaL_checkstring(L, 2);
	errno = 0;
	return luaL_fileresult(L, kitsune_rename(fromname, toname) == 0, NULL);
}

static int utf8_os_tmpname(lua_State* L) {

	char buff[L_tmpnam * 3 + 1];  // UTF-8 needs at most 3 bytes per UTF-16 unit
	if (kitsune_tmpnam(buff, sizeof(buff)) != 0)
		return luaL_error(L, "unable to generate a unique filename");
	lua_pushstring(L, buff);
	return 1;
}

static int utf8_os_getenv(lua_State* L) {

	lua_pushstring(L, kitsune_getenv(luaL_checkstring(L, 1)));  // NULL pushes nil
	return 1;
}

// -- loadfile / dofile ----------------------------------------------------------------

// lbaselib's load_aux.
static int load_aux(lua_State* L, int status, int envidx) {

	if (status == LUA_OK) {
		if (envidx != 0) {
			lua_pushvalue(L, envidx);
			if (!lua_setupvalue(L, -2, 1))
				lua_pop(L, 1);
		}
		return 1;
	}
	luaL_pushfail(L);
	lua_insert(L, -2);
	return 2;
}

// Upvalue 1: stock loadfile (used for stdin).
static int utf8_loadfile(lua_State* L) {

	if (lua_isnoneornil(L, 1))
		return call_stock(L);
	const char* fname = luaL_checkstring(L, 1);
	const char* mode = luaL_optstring(L, 2, "bt");
	if (strchr(mode, 'B') != NULL)  // Lua code cannot use fixed buffers
		luaL_argerror(L, 2, "invalid mode");
	int env = !lua_isnone(L, 3) ? 3 : 0;
	return load_aux(L, kitsune_loadfilex(L, fname, mode), env);
}

static int dofile_cont(lua_State* L, int d1, lua_KContext d2) {

	(void)d1;
	(void)d2;
	return lua_gettop(L) - 1;
}

// Upvalue 1: stock dofile (used for stdin).
static int utf8_dofile(lua_State* L) {

	if (lua_isnoneornil(L, 1))
		return call_stock(L);
	const char* fname = luaL_checkstring(L, 1);
	lua_settop(L, 1);
	if (kitsune_loadfile(L, fname) != LUA_OK)
		return lua_error(L);
	lua_callk(L, 0, LUA_MULTRET, 0, dofile_cont);
	return dofile_cont(L, 0, 0);
}

// -- package ---------------------------------------------------------------------------

#define PKG_CLIBS    "_CLIBS"     // registry table of loaded C libraries (shared with loadlib.c)
#define PKG_POF      "luaopen_"  // prefix for open functions in C libraries
#define PKG_OFSEP    "_"         // separator for open functions in C libraries
#define PKG_ERRLIB   1
#define PKG_ERRFUNC  2

// LoadLibrary / GetProcAddress failure message, as UTF-8.
static void push_last_error(lua_State* L) {

	DWORD error = GetLastError();
	wchar_t buffer[256];
	DWORD len = FormatMessageW(FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, error, 0, buffer, sizeof(buffer) / sizeof(buffer[0]), NULL);
	if (len)
		lua_pushwideasutf8(L, buffer, len);
	else
		lua_pushfstring(L, "system error %d\n", (int)error);
}

// Deallocator for a library string: unloads the DLL it represents.
static void* free_lib(void* ud, void* ptr, size_t osize, size_t nsize) {

	(void)ptr;
	(void)osize;
	(void)nsize;
	FreeLibrary((HMODULE)ud);
	return NULL;
}

static void* check_clib(lua_State* L, const char* path) {

	lua_getfield(L, LUA_REGISTRYINDEX, PKG_CLIBS);
	lua_getfield(L, -1, path);
	void* plib = lua_touserdata(L, -1);
	lua_pop(L, 2);
	return plib;
}

// registry.CLIBS[path] = plib, plus a library string kept in CLIBS so the DLL is unloaded
// only when CLIBS is collected (loadlib.c's addtoclib).
static void add_to_clib(lua_State* L, const char* path, void* plib) {

	static const char dummy[] = "01234567890";
	lua_getfield(L, LUA_REGISTRYINDEX, PKG_CLIBS);
	lua_pushlightuserdata(L, plib);
	lua_setfield(L, -2, path);
	lua_pushexternalstring(L, dummy, sizeof(dummy) - 1, free_lib, plib);
	luaL_ref(L, -2);
	lua_pop(L, 1);
}

static int look_for_func(lua_State* L, const char* path, const char* sym) {

	void* reg = check_clib(L, path);
	if (reg == NULL) {
		reg = kitsune_loadlibraryex(path, 0);
		if (reg == NULL) {
			push_last_error(L);
			return PKG_ERRLIB;
		}
		add_to_clib(L, path, reg);
	}
	if (*sym == '*') {  // only load the library
		lua_pushboolean(L, 1);
		return 0;
	}
	lua_CFunction f = reinterpret_cast<lua_CFunction>(GetProcAddress((HMODULE)reg, sym));
	if (f == NULL) {
		push_last_error(L);
		return PKG_ERRFUNC;
	}
	lua_pushcfunction(L, f);
	return 0;
}

static int utf8_loadlib(lua_State* L) {

	const char* path = luaL_checkstring(L, 1);
	const char* init = luaL_checkstring(L, 2);
	int stat = look_for_func(L, path, init);
	if (stat == 0)
		return 1;
	luaL_pushfail(L);
	lua_insert(L, -2);
	lua_pushstring(L, (stat == PKG_ERRLIB) ? "open" : "init");
	return 3;
}

static int readable(const char* filename) {

	FILE* f = kitsune_fopen(filename, "r");
	if (f == NULL)
		return 0;
	fclose(f);
	return 1;
}

// Next name in '*path' = "name1;name2;...", terminated in place; NULL when the list ends.
static const char* get_next_filename(char** path, char* end) {

	char* name = *path;
	if (name == end)
		return NULL;
	if (*name == '\0') {  // from the previous iteration
		*name = *LUA_PATH_SEP;
		name++;
	}
	char* sep = strchr(name, *LUA_PATH_SEP);
	if (sep == NULL)
		sep = end;
	*sep = '\0';
	*path = sep;
	return name;
}

static void push_error_not_found(lua_State* L, const char* path) {

	luaL_Buffer b;
	luaL_buffinit(L, &b);
	luaL_addstring(&b, "no file '");
	luaL_addgsub(&b, path, LUA_PATH_SEP, "'\n\tno file '");
	luaL_addstring(&b, "'");
	luaL_pushresult(&b);
}

static const char* search_path(lua_State* L, const char* name, const char* path, const char* sep, const char* dirsep) {

	if (*sep != '\0' && strchr(name, *sep) != NULL)
		name = luaL_gsub(L, name, sep, dirsep);
	luaL_Buffer buff;
	luaL_buffinit(L, &buff);
	luaL_addgsub(&buff, path, LUA_PATH_MARK, name);
	luaL_addchar(&buff, '\0');
	char* pathname = luaL_buffaddr(&buff);
	char* endpathname = pathname + luaL_bufflen(&buff) - 1;
	const char* filename;
	while ((filename = get_next_filename(&pathname, endpathname)) != NULL) {
		if (readable(filename))
			return lua_pushstring(L, filename);
	}
	luaL_pushresult(&buff);
	push_error_not_found(L, lua_tostring(L, -1));
	return NULL;
}

static int utf8_searchpath(lua_State* L) {

	const char* f = search_path(L, luaL_checkstring(L, 1), luaL_checkstring(L, 2),
		luaL_optstring(L, 3, "."), luaL_optstring(L, 4, LUA_DIRSEP));
	if (f != NULL)
		return 1;
	luaL_pushfail(L);
	lua_insert(L, -2);
	return 2;
}

// Searchers get the 'package' table as upvalue 1, like the stock ones.
static const char* find_file(lua_State* L, const char* name, const char* pname, const char* dirsep) {

	lua_getfield(L, lua_upvalueindex(1), pname);
	const char* path = lua_tostring(L, -1);
	if (path == NULL)
		luaL_error(L, "'package.%s' must be a string", pname);
	return search_path(L, name, path, ".", dirsep);
}

static int check_load(lua_State* L, int stat, const char* filename) {

	if (stat) {
		lua_pushstring(L, filename);  // 2nd argument to the module
		return 2;
	}
	return luaL_error(L, "error loading module '%s' from file '%s':\n\t%s",
		lua_tostring(L, 1), filename, lua_tostring(L, -1));
}

static int utf8_searcher_Lua(lua_State* L) {

	const char* name = luaL_checkstring(L, 1);
	const char* filename = find_file(L, name, "path", LUA_DIRSEP);
	if (filename == NULL)
		return 1;
	return check_load(L, kitsune_loadfile(L, filename) == LUA_OK, filename);
}

static int load_func(lua_State* L, const char* filename, const char* modname) {

	modname = luaL_gsub(L, modname, ".", PKG_OFSEP);
	const char* mark = strchr(modname, *LUA_IGMARK);
	const char* openfunc;
	if (mark) {
		openfunc = lua_pushlstring(L, modname, (size_t)(mark - modname));
		openfunc = lua_pushfstring(L, PKG_POF "%s", openfunc);
		int stat = look_for_func(L, filename, openfunc);
		if (stat != PKG_ERRFUNC)
			return stat;
		modname = mark + 1;  // try the old-style name
	}
	openfunc = lua_pushfstring(L, PKG_POF "%s", modname);
	return look_for_func(L, filename, openfunc);
}

static int utf8_searcher_C(lua_State* L) {

	const char* name = luaL_checkstring(L, 1);
	const char* filename = find_file(L, name, "cpath", LUA_DIRSEP);
	if (filename == NULL)
		return 1;
	return check_load(L, load_func(L, filename, name) == 0, filename);
}

static int utf8_searcher_Croot(lua_State* L) {

	const char* name = luaL_checkstring(L, 1);
	const char* p = strchr(name, '.');
	if (p == NULL)
		return 0;  // is root
	lua_pushlstring(L, name, (size_t)(p - name));
	const char* filename = find_file(L, lua_tostring(L, -1), "cpath", LUA_DIRSEP);
	if (filename == NULL)
		return 1;
	int stat = load_func(L, filename, name);
	if (stat != 0) {
		if (stat != PKG_ERRFUNC)
			return check_load(L, 0, filename);
		lua_pushfstring(L, "no module '%s' in file '%s'", name, filename);
		return 1;
	}
	lua_pushstring(L, filename);
	return 2;
}

// loadlib.c's setpath, reading the environment as UTF-8 and expanding '!' to the UTF-8
// executable directory. Leaves package[field] as it is if the directory can't be found.
static void set_path(lua_State* L, int pkg, const char* field, const char* envname, const char* dft) {

	int top = lua_gettop(L);
	const char* nver = lua_pushfstring(L, "%s%s", envname, LUA_VERSUFFIX);
	const char* path = kitsune_getenv(nver);
	if (path == NULL)
		path = kitsune_getenv(envname);

	lua_getfield(L, LUA_REGISTRYINDEX, "LUA_NOENV");
	bool noenv = lua_toboolean(L, -1) != 0;
	lua_pop(L, 1);

	const char* dftmark;
	if (path == NULL || noenv)
		lua_pushstring(L, dft);
	else if ((dftmark = strstr(path, LUA_PATH_SEP LUA_PATH_SEP)) == NULL)
		lua_pushstring(L, path);
	else {  // ";;" is replaced by the default path
		size_t len = strlen(path);
		luaL_Buffer b;
		luaL_buffinit(L, &b);
		if (path < dftmark) {
			luaL_addlstring(&b, path, (size_t)(dftmark - path));
			luaL_addchar(&b, *LUA_PATH_SEP);
		}
		luaL_addstring(&b, dft);
		if (dftmark < path + len - 2) {
			luaL_addchar(&b, *LUA_PATH_SEP);
			luaL_addlstring(&b, dftmark + 2, (size_t)((path + len - 2) - dftmark));
		}
		luaL_pushresult(&b);
	}

	char exe[MAX_PATH * 3 + 1];
	unsigned long n = kitsune_module_filename(NULL, exe, sizeof(exe));
	char* lb = (n == 0 || n == sizeof(exe)) ? NULL : strrchr(exe, '\\');
	if (lb != NULL) {
		*lb = '\0';
		luaL_gsub(L, lua_tostring(L, -1), LUA_EXEC_DIR, exe);
		lua_setfield(L, pkg, field);
	}
	lua_settop(L, top);
}

// -- Installation ------------------------------------------------------------------

// t[name] = closure(fn, stock t[name], extra upvalue if extra != NULL).
static void replace_with_stock(lua_State* L, int t, const char* name, lua_CFunction fn, const char* extra) {

	lua_getfield(L, t, name);
	int nup = 1;
	if (extra) {
		lua_pushstring(L, extra);
		nup++;
	}
	lua_pushcclosure(L, fn, nup);
	lua_setfield(L, t, name);
}

static void replace(lua_State* L, int t, const char* name, lua_CFunction fn) {

	lua_pushcfunction(L, fn);
	lua_setfield(L, t, name);
}

// Pushes the loaded library 'name' (registry LOADED[name]); returns false (nothing pushed)
// when it isn't open.
static bool get_lib(lua_State* L, const char* name) {

	lua_getfield(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
	lua_getfield(L, -1, name);
	lua_remove(L, -2);
	if (lua_istable(L, -1))
		return true;
	lua_pop(L, 1);
	return false;
}

void kitsune_open_utf8_stdlib(lua_State* L) {

	int top = lua_gettop(L);

	if (get_lib(L, LUA_IOLIBNAME)) {
		int io = lua_gettop(L);
		replace(L, io, "open", kitsune_io_open);
		replace_with_stock(L, io, "lines", utf8_io_lines, NULL);
		replace_with_stock(L, io, "input", utf8_io_file, "r");
		replace_with_stock(L, io, "output", utf8_io_file, "w");
		replace(L, io, "popen", utf8_io_popen);
	}

	if (get_lib(L, LUA_OSLIBNAME)) {
		int os = lua_gettop(L);
		replace(L, os, "execute", utf8_os_execute);
		replace(L, os, "remove", utf8_os_remove);
		replace(L, os, "rename", utf8_os_rename);
		replace(L, os, "tmpname", utf8_os_tmpname);
		replace(L, os, "getenv", utf8_os_getenv);
	}

	if (get_lib(L, LUA_GNAME)) {  // base library: LOADED._G
		int g = lua_gettop(L);
		replace_with_stock(L, g, "loadfile", utf8_loadfile, NULL);
		replace_with_stock(L, g, "dofile", utf8_dofile, NULL);
	}

	if (get_lib(L, LUA_LOADLIBNAME)) {
		int pkg = lua_gettop(L);
		replace(L, pkg, "loadlib", utf8_loadlib);
		replace(L, pkg, "searchpath", utf8_searchpath);
		// searchers[1] (preload) needs no file access; 2-4 are the Lua, C and C-root searchers.
		if (lua_getfield(L, pkg, "searchers") == LUA_TTABLE) {
			static const lua_CFunction searchers[] = { utf8_searcher_Lua, utf8_searcher_C, utf8_searcher_Croot };
			for (int i = 0; i < 3; i++) {
				lua_pushvalue(L, pkg);  // 'package' as upvalue, like the stock searchers
				lua_pushcclosure(L, searchers[i], 1);
				lua_rawseti(L, -2, i + 2);
			}
		}
		lua_pop(L, 1);
		set_path(L, pkg, "path", "LUA_PATH", LUA_PATH_DEFAULT);
		set_path(L, pkg, "cpath", "LUA_CPATH", LUA_CPATH_DEFAULT);
	}

	lua_settop(L, top);
}

#else

void kitsune_open_utf8_stdlib(lua_State* L) {
	(void)L;  // file names are byte strings here, so the stock functions already take UTF-8
}

#endif
