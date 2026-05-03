// KitsuneLuaDebug.cpp
// Implementation of the dual-slot Lua hook dispatch layer.
// Including kitsune_internal.h here is safe: .cpp files are never included.
#include "kitsune_internal.h"
#include "Kitsune/KitsuneLuaDebug.h"

KitsuneState* g_debugState = nullptr;

void InitLuaDebug(KitsuneState* state) {
	g_debugState = state;
}

// Single hook function installed via lua_sethook_real.
// Snapshots both slots under the lock so neither is called while locked.
static void merged_hook(lua_State* L, lua_Debug* ar) {
	if (!g_debugState)
		return;
	KitsuneHookState snap;
	{
		std::lock_guard<std::mutex> lk(g_debugState->hookMtx);
		auto it = g_debugState->hookMap.find(L);
		if (it == g_debugState->hookMap.end())
			return;
		snap = it->second;
	}
	if (snap.kitsune.func && (snap.kitsune.mask & (1 << ar->event)))
		snap.kitsune.func(L, ar);
	if (snap.external.func && (snap.external.mask & (1 << ar->event)))
		snap.external.func(L, ar);
}

// Recompute merged mask/count and push to Lua.
// Must be called with g_debugState->hookMtx already held.
static void kitsune_recompute(lua_State* L, KitsuneHookState& s) {
	int merged_mask = s.kitsune.mask | s.external.mask;
	int merged_count = 0;
	if (merged_mask & LUA_MASKCOUNT) {
		int kc = s.kitsune.count;
		int ec = s.external.count;
		if (kc > 0 && ec > 0)
			merged_count = (kc < ec) ? kc : ec;
		else if (kc > 0)
			merged_count = kc;
		else
			merged_count = ec;
	}
	if (merged_mask == 0) {
		g_debugState->hookMap.erase(L);
		lua_sethook_real(L, nullptr, 0, 0);
	}
	else {
		lua_sethook_real(L, merged_hook, merged_mask, merged_count);
	}
}

// ---------------------------------------------------------------------------
// kitsune_* — scheduler-owned hook slot
// ---------------------------------------------------------------------------
void kitsune_sethook(lua_State* L, lua_Hook func, int mask, int count) {
	if (!g_debugState) {
		lua_sethook_real(L, func, mask, count);
		return;
	}
	std::lock_guard<std::mutex> lk(g_debugState->hookMtx);
	KitsuneHookState& s = g_debugState->hookMap[L];
	s.kitsune = { func, mask, count };
	kitsune_recompute(L, s);
}

lua_Hook kitsune_gethook(lua_State* L) {
	if (!g_debugState)
		return lua_gethook_real(L);
	std::lock_guard<std::mutex> lk(g_debugState->hookMtx);
	auto it = g_debugState->hookMap.find(L);
	return (it != g_debugState->hookMap.end()) ? it->second.kitsune.func : nullptr;
}

int kitsune_gethookmask(lua_State* L) {
	if (!g_debugState)
		return lua_gethookmask_real(L);
	std::lock_guard<std::mutex> lk(g_debugState->hookMtx);
	auto it = g_debugState->hookMap.find(L);
	return (it != g_debugState->hookMap.end()) ? it->second.kitsune.mask : 0;
}

int kitsune_gethookcount(lua_State* L) {
	if (!g_debugState)
		return lua_gethookcount_real(L);
	std::lock_guard<std::mutex> lk(g_debugState->hookMtx);
	auto it = g_debugState->hookMap.find(L);
	return (it != g_debugState->hookMap.end()) ? it->second.kitsune.count : 0;
}

// ---------------------------------------------------------------------------
// lua_sethook / lua_gethook* — external/debugger-owned hook slot
// extern "C" so GetProcAddress finds undecorated "lua_sethook" etc.
// ---------------------------------------------------------------------------
extern "C" {

LUA_API void lua_sethook(lua_State* L, lua_Hook func, int mask, int count) {
	if (!g_debugState) {
		lua_sethook_real(L, func, mask, count);
		return;
	}
	std::lock_guard<std::mutex> lk(g_debugState->hookMtx);
	KitsuneHookState& s = g_debugState->hookMap[L];
	s.external = { func, mask, count };
	kitsune_recompute(L, s);
}

LUA_API lua_Hook lua_gethook(lua_State* L) {
	if (!g_debugState)
		return lua_gethook_real(L);
	std::lock_guard<std::mutex> lk(g_debugState->hookMtx);
	auto it = g_debugState->hookMap.find(L);
	return (it != g_debugState->hookMap.end()) ? it->second.external.func : nullptr;
}

LUA_API int lua_gethookmask(lua_State* L) {
	if (!g_debugState)
		return lua_gethookmask_real(L);
	std::lock_guard<std::mutex> lk(g_debugState->hookMtx);
	auto it = g_debugState->hookMap.find(L);
	return (it != g_debugState->hookMap.end()) ? it->second.external.mask : 0;
}

LUA_API int lua_gethookcount(lua_State* L) {
	if (!g_debugState)
		return lua_gethookcount_real(L);
	std::lock_guard<std::mutex> lk(g_debugState->hookMtx);
	auto it = g_debugState->hookMap.find(L);
	return (it != g_debugState->hookMap.end()) ? it->second.external.count : 0;
}

} // extern "C"

// ---------------------------------------------------------------------------
// kitsune_hook_remove_state — call before any lua_State* becomes invalid
// ---------------------------------------------------------------------------
void kitsune_hook_remove_state(lua_State* L) {
	if (!L || !g_debugState)
		return;
	std::lock_guard<std::mutex> lk(g_debugState->hookMtx);
	g_debugState->hookMap.erase(L);
	lua_sethook_real(L, nullptr, 0, 0);
}

// ---------------------------------------------------------------------------
// kitsune_inherit_external_hook — copy the external (debugger) hook slot
// from src to dst. Called when a new coroutine thread is created so that
// luadebug.dll's hook (installed on the main state during -e bootstrap)
// is active on the thread that actually runs the user script.
// Caller must hold LuaAccess but must NOT hold hookMtx.
// ---------------------------------------------------------------------------
void kitsune_inherit_external_hook(lua_State* src, lua_State* dst) {
	if (!g_debugState || !src || !dst)
		return;
	std::lock_guard<std::mutex> lk(g_debugState->hookMtx);
	auto it = g_debugState->hookMap.find(src);
	if (it == g_debugState->hookMap.end() || it->second.external.func == nullptr)
		return;
	KitsuneHookSlot ext = it->second.external;
	KitsuneHookState& ds = g_debugState->hookMap[dst];
	ds.external = ext;
	kitsune_recompute(dst, ds);
}