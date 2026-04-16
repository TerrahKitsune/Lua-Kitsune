#pragma once

// ── Feature flag cascade ──────────────────────────────────────────────────────
// Define KITSUNE_ALL before including platform.h to enable every optional module.
// Individual flags can also be set independently (e.g. -DKITSUNE_MYSQL).
#ifdef KITSUNE_ALL
#	ifndef KITSUNE_MYSQL
#		define KITSUNE_MYSQL
#	endif
#	ifndef KITSUNE_POSTGRES
#		define KITSUNE_POSTGRES
#	endif
#	ifndef KITSUNE_KAFKA
#		define KITSUNE_KAFKA
#	endif
#	ifndef KITSUNE_ARCHIVE
#		define KITSUNE_ARCHIVE
#	endif
#	ifndef KITSUNE_REDIS
#		define KITSUNE_REDIS
#	endif
#	ifndef KITSUNE_MONGO
#		define KITSUNE_MONGO
#	endif
#	ifndef KITSUNE_HTTP
#		define KITSUNE_HTTP
#	endif
#endif

#ifdef _WIN32
// On Windows just pull in the real Windows.h -- it provides all Win32 types,
// macros, and functions used throughout the codebase.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <stdint.h>   // uint8_t, uint16_t, uint32_t, etc. for cross-platform struct fields
#include <Windows.h>
#else
// ── Linux / POSIX portability layer ──────────────────────────────────────────
// Provides the minimum set of Windows types, macros and inline helpers that
// KitsuneEngine sources use, so every file can include "platform.h" instead
// of <Windows.h> and compile cleanly on Linux.
// ─────────────────────────────────────────────────────────────────────────────

#include <stdint.h>
#include <stdlib.h>
#include <string.h>     // memset, memcpy
#include <time.h>       // nanosleep, struct timespec
#include <unistd.h>     // sched_yield (via sched.h), legacy usleep header
#include <signal.h>     // signal(), SIGPIPE
#include <sched.h>      // sched_yield
#include <algorithm>    // std::min, std::max

// ── Fundamental type aliases ──────────────────────────────────────────────────
typedef uint8_t         BYTE;
typedef uint16_t        WORD;
typedef uint32_t        DWORD;
typedef int32_t         LONG;
typedef int             BOOL;
typedef void*           HANDLE;
typedef void*           HMODULE;
typedef void*           LPVOID;
typedef char*           LPSTR;
typedef const char*     LPCSTR;
typedef wchar_t         WCHAR;
typedef uint32_t*       LPDWORD;
typedef intptr_t        INT_PTR;

#define TRUE  1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

// ── Calling conventions (no-op on Linux / GCC) ───────────────────────────────
#define WINAPI
#define APIENTRY
#define CALLBACK
#define __cdecl
#define __stdcall

// ── Memory helpers ────────────────────────────────────────────────────────────
#define ZeroMemory(ptr, len)      memset((ptr), 0, (len))
#define CopyMemory(dst, src, len) memcpy((dst), (src), (len))
#define FillMemory(ptr, len, val) memset((ptr), (val), (len))

#ifndef MAXDWORD
#define MAXDWORD 0xFFFFFFFFUL
#endif

// ── Sleep ─────────────────────────────────────────────────────────────────────
// Windows Sleep() takes milliseconds.  On Linux we use nanosleep(), which
// accepts any non-negative duration without useconds_t overflow or the old
// POSIX < 1 s limit on usleep().  Sleep(0) yields the scheduler time-slice.
static inline void Sleep(unsigned long ms) {
	if (ms == 0) {
		sched_yield();
	}
	else {
		struct timespec ts;
		ts.tv_sec  = (time_t)(ms / 1000UL);
		ts.tv_nsec = (long)((ms % 1000UL) * 1000000UL);
		nanosleep(&ts, nullptr);
	}
}

// ── KITSUNE_API visibility ────────────────────────────────────────────────────
// On Linux, both the export and import cases use the same GCC visibility
// attribute -- the linker handles dynamic resolution from .so symbols without
// needing separate import stubs.
#ifdef KITSUNE_ENGINE_EXPORTS
#define KITSUNE_API __attribute__((visibility("default")))
#else
#define KITSUNE_API __attribute__((visibility("default")))
#endif

#endif // _WIN32
