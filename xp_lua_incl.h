#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif

// LUA_BUILD_AS_DLL causes Lua to use __declspec(dllexport/dllimport), which is
// MSVC / Windows-only.  On Linux the Lua core is a static lib linked directly
// into KitsuneEngine.so, so no dllexport decoration is needed.
#ifdef _WIN32
#define LUA_BUILD_AS_DLL
#endif

// LUA_CORE marks this translation unit as part of the Lua core (exposes the
// private lstate.h / lobject.h headers).  Needed on all platforms.
#define LUA_CORE

// Suppress MSVC deprecation warnings for CRT functions (strcpy, sprintf, …).
#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif

// ImGui uses ImTextureID as a 64-bit handle on Windows.  Not needed on Linux
// because ImGui is in the out-of-scope module list for the barebones build.
#ifdef _WIN32
#define ImTextureID ImU64
#endif

#include "./lua/lua.hpp"