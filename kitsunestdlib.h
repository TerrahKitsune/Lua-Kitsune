#pragma once
#include "lua_main_incl.h"

// UTF-8 file names for Lua's standard libraries.
//
// Lua passes file names, commands and environment names straight to the C runtime. On
// Linux and macOS those are byte strings, so UTF-8 works as is; on Windows the narrow CRT
// reads them in the process's ANSI code page. kitsune.exe's manifest makes that UTF-8,
// but other hosts (such as .NET applications) keep e.g. Windows-1252. The vendored Lua
// sources in Lua/ are kept unmodified, so on Windows the affected functions are replaced
// after luaL_openlibs by equivalents that behave the same but use kitsune_fopen and the
// other wrappers in kitsunefile.h.

// luaL_loadfilex for a UTF-8 path (same chunk name, BOM / '#' line handling, text or
// binary chunks and error messages). filename == NULL loads stdin, as luaL_loadfilex.
int kitsune_loadfilex(lua_State* L, const char* filename, const char* mode);
#define kitsune_loadfile(L, filename) kitsune_loadfilex(L, filename, NULL)

// io.open for a UTF-8 path (same contract as Lua's io.open), on every platform.
int kitsune_io_open(lua_State* L);

// kitsune_io_open with a stricter mode, '[rwa]%+?b?' (no repeated 'b'): FileSystem.Open.
int kitsune_io_open_strict(lua_State* L);

// Windows: replaces io.open, io.lines, io.input, io.output, io.popen, os.remove,
// os.rename, os.getenv, os.execute, os.tmpname, loadfile, dofile, package.loadlib,
// package.searchpath and the file searchers used by require, and recomputes package.path /
// package.cpath from the UTF-8 environment and executable directory. Libraries that are
// not open are skipped. Other platforms: does nothing. Call once, after luaL_openlibs.
void kitsune_open_utf8_stdlib(lua_State* L);
