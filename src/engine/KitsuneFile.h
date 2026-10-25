#pragma once
#include <stdio.h>

// C-runtime calls that take UTF-8 text. On Windows the narrow CRT reads paths and
// environment strings in the ANSI code page, so these convert to UTF-16 and use the
// _w* variants; elsewhere they are the plain CRT calls. They set errno like the
// originals. Deliberately free of <Windows.h> and C-compatible so any source file can
// include it. kitsunestdlib.cpp builds Lua's io / os / loadfile / require on top of them.

#ifdef __cplusplus
extern "C" {
#endif

// fopen for a UTF-8 path. Returns NULL on failure (including a malformed mode).
FILE* kitsune_fopen(const char* path, const char* mode);

#ifdef _WIN32
// Like freopen, 'stream' is closed whenever NULL is returned; never fclose it afterwards.
FILE* kitsune_freopen(const char* path, const char* mode, FILE* stream);
FILE* kitsune_popen(const char* command, const char* mode);
int kitsune_remove(const char* path);
int kitsune_rename(const char* from, const char* to);
int kitsune_system(const char* command);

// Value of an environment variable as UTF-8, or NULL when unset. The result stays
// valid until the next kitsune_getenv call on the same thread.
char* kitsune_getenv(const char* name);

// Writes a unique temporary file name (UTF-8) to buf; returns 0 on success.
int kitsune_tmpnam(char* buf, size_t cap);

// Path of module (NULL = the executable) as UTF-8 in buf, like GetModuleFileNameA:
// returns the length, or 0 on failure, or cap when buf is too small.
unsigned long kitsune_module_filename(void* module, char* buf, unsigned long cap);

// LoadLibraryExA for a UTF-8 path; returns the HMODULE.
void* kitsune_loadlibraryex(const char* path, unsigned long flags);
#endif

#ifdef __cplusplus
}
#endif
