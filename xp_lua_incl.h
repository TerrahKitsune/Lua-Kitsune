#define _CRT_SECURE_NO_WARNINGS 1
//build it as a dll
#define LUA_BUILD_AS_DLL
//then embed the dll directly
#define LUA_CORE
#pragma warning(disable:4996)
//Imgui 32bit
#define ImTextureID ImU64

#include "./lua/lua.hpp"