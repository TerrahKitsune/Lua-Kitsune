#include "KitsuneFile.h"
#include "text/LuaText.h"
#include "Platform.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

// UTF-8 C-runtime wrappers declared in kitsunefile.h. On Windows the narrow CRT interprets
// paths and environment strings in the ANSI code page, so they are converted to UTF-16 for the
// _w* variants; elsewhere these are the plain CRT calls.

#ifdef _WIN32
// Validates a stdio mode and widens it into wmode (room for 4 wchar_t). The MSVC CRT
// aborts the process on a malformed mode, so only [rwa] followed by at most one '+' and
// at most one of 'b' / 't' is accepted (popen: only [rw] followed by 'b' / 't').
static bool WidenMode(const char* mode, wchar_t* wmode, bool popen) {

	bool valid = mode[0] == 'r' || mode[0] == 'w' || (!popen && mode[0] == 'a');
	int plus = 0;
	int kind = 0;
	for (const char* m = mode + 1; valid && *m; m++) {
		if (*m == '+' && !popen)
			valid = ++plus == 1;
		else if (*m == 'b' || *m == 't')
			valid = ++kind == 1;
		else
			valid = false;
	}
	if (!valid) {
		errno = EINVAL;
		return false;
	}

	size_t i = 0;
	for (; mode[i]; i++)
		wmode[i] = (wchar_t)(unsigned char)mode[i];
	wmode[i] = L'\0';
	return true;
}

// kitsune_utf8_to_wide_alloc that sets errno = ENOMEM on failure.
static wchar_t* WidenArg(const char* s) {

	wchar_t* w = kitsune_utf8_to_wide_alloc(s);
	if (!w)
		errno = ENOMEM;
	return w;
}

// Frees w without disturbing errno (set by the preceding CRT call).
static void FreeKeepErrno(wchar_t* w) {

	int saved = errno;
	kitsune_free(w);
	errno = saved;
}
#endif

FILE* kitsune_fopen(const char* path, const char* mode) {
#ifdef _WIN32
	wchar_t wmode[4];
	if (!WidenMode(mode, wmode, false))
		return NULL;
	wchar_t* wpath = WidenArg(path);
	if (!wpath)
		return NULL;
	FILE* f = _wfopen(wpath, wmode);
	FreeKeepErrno(wpath);
	return f;
#else
	return fopen(path, mode);
#endif
}

#ifdef _WIN32
// Like freopen, 'stream' is closed whenever NULL is returned (also when the mode or the
// path conversion fails before _wfreopen is reached), so the caller must not close it again.
static FILE* CloseFailed(FILE* stream) {

	int saved = errno;
	if (stream)
		fclose(stream);
	errno = saved;
	return NULL;
}

FILE* kitsune_freopen(const char* path, const char* mode, FILE* stream) {

	wchar_t wmode[4];
	if (!WidenMode(mode, wmode, false))
		return CloseFailed(stream);
	wchar_t* wpath = WidenArg(path);
	if (!wpath)
		return CloseFailed(stream);
	FILE* f = _wfreopen(wpath, wmode, stream);
	FreeKeepErrno(wpath);
	return f;
}

FILE* kitsune_popen(const char* command, const char* mode) {

	wchar_t wmode[4];
	if (!WidenMode(mode, wmode, true))
		return NULL;
	wchar_t* wcmd = WidenArg(command);
	if (!wcmd)
		return NULL;
	fflush(NULL);
	FILE* f = _wpopen(wcmd, wmode);
	FreeKeepErrno(wcmd);
	return f;
}

int kitsune_remove(const char* path) {

	wchar_t* wpath = WidenArg(path);
	if (!wpath)
		return -1;
	int rc = _wremove(wpath);
	FreeKeepErrno(wpath);
	return rc;
}

int kitsune_rename(const char* from, const char* to) {

	wchar_t* wfrom = WidenArg(from);
	wchar_t* wto = wfrom ? WidenArg(to) : NULL;
	int rc = (wfrom && wto) ? _wrename(wfrom, wto) : -1;
	FreeKeepErrno(wfrom);
	FreeKeepErrno(wto);
	return rc;
}

int kitsune_system(const char* command) {

	// system(NULL) asks whether a command processor exists.
	if (!command)
		return _wsystem(NULL);
	wchar_t* wcmd = WidenArg(command);
	if (!wcmd)
		return -1;
	int rc = _wsystem(wcmd);
	FreeKeepErrno(wcmd);
	return rc;
}

// Converts a null-terminated wide string into buf as null-terminated UTF-8 (unpaired
// surrogates become U+FFFD). Returns the byte length, or cap when it does not fit.
static size_t WideToUtf8Buf(const wchar_t* w, char* buf, size_t cap) {

	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, (int)cap, NULL, NULL);
	return n > 0 ? (size_t)n - 1 : cap;  // n counts the terminator; 0 means buf is too small
}

char* kitsune_getenv(const char* name) {

	// Per-thread so concurrent callers never share it; released when the thread exits.
	// It outlives engine sessions, so it uses the CRT heap directly rather than
	// kitsune_malloc / operator new (a std::string allocates even when empty in debug
	// builds), which would count it as a leak of whichever session touched the thread first.
	struct EnvBuffer {
		char* data = NULL;
		size_t cap = 0;
		~EnvBuffer() { ::free(data); }
	};
	static thread_local EnvBuffer value;

	wchar_t* wname = WidenArg(name);
	if (!wname)
		return NULL;
	const wchar_t* wvalue = _wgetenv(wname);
	kitsune_free(wname);
	if (!wvalue)
		return NULL;

	size_t need = wcslen(wvalue) * 3 + 1;  // a UTF-16 unit never needs more than 3 bytes
	if (need > value.cap) {
		char* grown = (char*)::realloc(value.data, need);
		if (!grown)
			return NULL;
		value.data = grown;
		value.cap = need;
	}
	WideToUtf8Buf(wvalue, value.data, value.cap);
	return value.data;
}

int kitsune_tmpnam(char* buf, size_t cap) {

	wchar_t wname[L_tmpnam];
	if (!_wtmpnam(wname))
		return -1;
	return WideToUtf8Buf(wname, buf, cap) < cap ? 0 : -1;
}

unsigned long kitsune_module_filename(void* module, char* buf, unsigned long cap) {

	wchar_t wname[1024];
	DWORD n = GetModuleFileNameW((HMODULE)module, wname, 1024);
	if (n == 0 || n == 1024)
		return n == 0 ? 0 : cap;
	return (unsigned long)WideToUtf8Buf(wname, buf, cap);
}

void* kitsune_loadlibraryex(const char* path, unsigned long flags) {

	wchar_t* wpath = kitsune_utf8_to_wide_alloc(path);
	if (!wpath) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return NULL;
	}
	HMODULE lib = LoadLibraryExW(wpath, NULL, flags);
	DWORD err = GetLastError();
	kitsune_free(wpath);
	SetLastError(err);
	return lib;
}
#endif
