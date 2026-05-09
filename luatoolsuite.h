#pragma once
#include "lua_main_incl.h"
#include <string>
#include <vector>

#define LUATOOLSUITE "LUATOOLSUITE"

struct ToolSuiteParam {
    std::string name;
    std::string type;
    std::string description;
    bool        required;
};

struct ToolSuiteTool {
    std::string              name;
    std::string              description;
    std::vector<ToolSuiteParam> params;
    int                      fn_ref = LUA_NOREF;
};

struct LuaToolSuite {
    std::vector<ToolSuiteTool> tools;
    // Optional permission gate: fn(name, args) -> bool
    // LUA_NOREF means no gate is set.
    int gate_ref;
    LuaToolSuite() : gate_ref(LUA_NOREF) {}
};

LuaToolSuite* lua_toolsuite_push(lua_State* L);
LuaToolSuite* lua_toolsuite_check(lua_State* L, int idx);
// Returns true if the value at idx is a LuaToolSuite userdata
bool          lua_toolsuite_is(lua_State* L, int idx);

// tools:AddTool(name, description, parameters, fn) -> true
int lua_toolsuite_addtool(lua_State* L);
// tools:Call(messages) -> number of tools called
int lua_toolsuite_call(lua_State* L);
// Returns the OpenAI-format JSON array string for all registered tools
// Pushes a string onto the stack and returns 1
int lua_toolsuite_getjson(lua_State* L);

// tools:Callback(fn) -> sets the permission gate; fn(name, args) -> bool
int lua_toolsuite_setcallback(lua_State* L);
int lua_toolsuite_gc(lua_State* L);
int lua_toolsuite_tostring(lua_State* L);
int lua_toolsuite_new(lua_State* L);
