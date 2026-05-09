#include "platform.h"
#include "luatoolsuite.h"
#include "luajson.h"
#include <cstring>
#include <string>

// ── Helpers ────────────────────────────────────────────────────────────────────

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += (char)c;
            }
            break;
        }
    }
    return out;
}

// Builds the OpenAI-format JSON for a single tool definition.
static std::string build_tool_json(const ToolSuiteTool& t) {
    std::string s;
    s += "{\"type\":\"function\",\"function\":{";
    s += "\"name\":\"" + json_escape(t.name) + "\",";
    s += "\"description\":\"" + json_escape(t.description) + "\",";
    s += "\"parameters\":{\"type\":\"object\",\"properties\":{";
    std::vector<std::string> required;
    for (size_t i = 0; i < t.params.size(); i++) {
        const ToolSuiteParam& p = t.params[i];
        if (i > 0)
            s += ",";
        s += "\"" + json_escape(p.name) + "\":{";
        s += "\"type\":\"" + json_escape(p.type) + "\",";
        s += "\"description\":\"" + json_escape(p.description) + "\"";
        s += "}";
        if (p.required)
            required.push_back(p.name);
    }
    s += "},\"required\":[";
    for (size_t i = 0; i < required.size(); i++) {
        if (i > 0)
            s += ",";
        s += "\"" + json_escape(required[i]) + "\"";
    }
    s += "]}}}";
    return s;
}

// Builds the full OpenAI tools array JSON string.
static std::string build_suite_json(const LuaToolSuite* suite) {
    std::string s = "[";
    for (size_t i = 0; i < suite->tools.size(); i++) {
        if (i > 0)
            s += ",";
        s += build_tool_json(suite->tools[i]);
    }
    s += "]";
    return s;
}

// ── Userdata lifecycle ─────────────────────────────────────────────────────────

LuaToolSuite* lua_toolsuite_push(lua_State* L) {
    LuaToolSuite* s = (LuaToolSuite*)lua_newuserdata(L, sizeof(LuaToolSuite));
    new (s) LuaToolSuite();
    luaL_setmetatable(L, LUATOOLSUITE);
    return s;
}

LuaToolSuite* lua_toolsuite_check(lua_State* L, int idx) {
    return (LuaToolSuite*)luaL_checkudata(L, idx, LUATOOLSUITE);
}

bool lua_toolsuite_is(lua_State* L, int idx) {
    if (!lua_isuserdata(L, idx))
        return false;
    if (luaL_testudata(L, idx, LUATOOLSUITE) == nullptr)
        return false;
    return true;
}

int lua_toolsuite_gc(lua_State* L) {
    LuaToolSuite* suite = lua_toolsuite_check(L, 1);
    for (ToolSuiteTool& t : suite->tools) {
        if (t.fn_ref != LUA_NOREF)
            luaL_unref(L, LUA_REGISTRYINDEX, t.fn_ref);
    }
    if (suite->gate_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, suite->gate_ref);
    suite->~LuaToolSuite();
    return 0;
}

int lua_toolsuite_tostring(lua_State* L) {
    LuaToolSuite* suite = lua_toolsuite_check(L, 1);
    lua_pushfstring(L, "ToolSuite(%d tools)", (int)suite->tools.size());
    return 1;
}

// ── Llama.CreateToolSuite() ────────────────────────────────────────────────────

int lua_toolsuite_new(lua_State* L) {
    lua_toolsuite_push(L);
    return 1;
}

// ── tools:AddTool(name, description, parameters, fn) ──────────────────────────
// parameters: array of {name, type, description, required=false}

int lua_toolsuite_addtool(lua_State* L) {
    LuaToolSuite* suite = lua_toolsuite_check(L, 1);
    const char* name        = luaL_checkstring(L, 2);
    const char* description = luaL_checkstring(L, 3);
    luaL_checktype(L, 4, LUA_TTABLE);
    luaL_checktype(L, 5, LUA_TFUNCTION);

    ToolSuiteTool tool;
    tool.name        = name;
    tool.description = description;

    int n = (int)lua_rawlen(L, 4);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 4, i);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            continue;
        }
        ToolSuiteParam p;
        p.required = false;

        lua_getfield(L, -1, "name");
        if (lua_isstring(L, -1))
            p.name = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "type");
        if (lua_isstring(L, -1))
            p.type = lua_tostring(L, -1);
        else
            p.type = "string";
        lua_pop(L, 1);

        lua_getfield(L, -1, "description");
        if (lua_isstring(L, -1))
            p.description = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "required");
        if (!lua_isnil(L, -1))
            p.required = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);

        if (!p.name.empty())
            tool.params.push_back(std::move(p));
        lua_pop(L, 1);
    }

    lua_pushvalue(L, 5);
    tool.fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    suite->tools.push_back(std::move(tool));

    lua_pushboolean(L, 1);
    return 1;
}

// ── tools:GetJson() ────────────────────────────────────────────────────────────

int lua_toolsuite_getjson(lua_State* L) {
    LuaToolSuite* suite = lua_toolsuite_check(L, 1);
    std::string json = build_suite_json(suite);
    lua_pushlstring(L, json.c_str(), json.size());
    return 1;
}

// ── Yieldable dispatch state ───────────────────────────────────────────────────
// Heap-allocated so it survives across yields. Holds the pre-decoded list of
// tool calls to dispatch and bookkeeping for where we are in the loop.

struct ToolCallEntry {
    std::string name;
    std::string id;
    int args_ref = LUA_NOREF;
};

struct ToolCallState {
    // Registry ref to the messages table (arg 2 of the original call)
    int messages_ref;
    // Original length of messages before any tool replies were appended
    int msg_base;
    // Number of replies successfully appended so far
    int dispatched;
    // Index of the next entry to dispatch (0-based)
    int next;
    // Index of the entry currently being gated/dispatched (0-based)
    int current_idx;
    // Registry ref of the permission gate callback (LUA_NOREF = no gate)
    int gate_ref;
    // Pre-built list of calls from the decoded tool_calls array
    std::vector<ToolCallEntry> calls;
    // Pointer back to the suite
    LuaToolSuite* suite;
};

static void toolcallstate_free(lua_State* L, ToolCallState* state) {
    if (!state)
        return;
    for (ToolCallEntry& e : state->calls) {
        if (e.args_ref != LUA_NOREF)
            luaL_unref(L, LUA_REGISTRYINDEX, e.args_ref);
    }
    if (state->messages_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, state->messages_ref);
    // gate_ref is an owned ref taken at dispatch time, always free it
    if (state->gate_ref != LUA_NOREF)
        luaL_unref(L, LUA_REGISTRYINDEX, state->gate_ref);
    delete state;
}

// Append {role='tool', content, tool_call_id} to the messages table.
static void append_tool_message(lua_State* L, int messages_ref, int slot,
                                const std::string& id, const std::string& result) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, messages_ref);
    lua_newtable(L);
    lua_pushliteral(L, "tool");
    lua_setfield(L, -2, "role");
    lua_pushlstring(L, result.c_str(), result.size());
    lua_setfield(L, -2, "content");
    if (!id.empty()) {
        lua_pushlstring(L, id.c_str(), id.size());
        lua_setfield(L, -2, "tool_call_id");
    }
    lua_rawseti(L, -2, slot);
    lua_pop(L, 1); // pop messages table
}

// Forward declarations
static int toolsuite_dispatch_one(lua_State* L, ToolCallState* state);
static int toolsuite_dispatch_tool(lua_State* L, ToolCallState* state);

// ── Tool result continuation ───────────────────────────────────────────────────
// Called after the tool callback completes (or resumes from a yield).
static int toolsuite_continuation(lua_State* L, int status, lua_KContext ctx) {
    ToolCallState* state = (ToolCallState*)ctx;

    std::string result_str;
    if (status == LUA_OK || status == LUA_YIELD) {
        const char* res = lua_tostring(L, -1);
        result_str = res ? res : "";
    } else {
        const char* err = lua_tostring(L, -1);
        result_str = err ? std::string("error: ") + err : "error";
    }
    lua_pop(L, 1);

    int slot = state->msg_base + state->dispatched + 1;
    append_tool_message(L, state->messages_ref, slot, state->calls[state->current_idx].id, result_str);
    state->dispatched++;

    return toolsuite_dispatch_one(L, state);
}

// ── Gate continuation ──────────────────────────────────────────────────────────
// Called after the permission gate callback completes (or resumes from a yield).
// If the gate returned truthy, dispatch the tool; otherwise record a denied reply.
static int toolsuite_gate_continuation(lua_State* L, int status, lua_KContext ctx) {
    ToolCallState* state = (ToolCallState*)ctx;

    bool allowed = false;
    if (status == LUA_OK || status == LUA_YIELD)
        allowed = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);

    if (!allowed) {
        int slot = state->msg_base + state->dispatched + 1;
        append_tool_message(L, state->messages_ref, slot,
                            state->calls[state->current_idx].id,
                            "error: permission denied");
        state->dispatched++;
        return toolsuite_dispatch_one(L, state);
    }

    return toolsuite_dispatch_tool(L, state);
}

// ── Dispatch the tool callback for state->current_idx ─────────────────────────
static int toolsuite_dispatch_tool(lua_State* L, ToolCallState* state) {
    const ToolCallEntry& entry = state->calls[state->current_idx];

    const ToolSuiteTool* tool = nullptr;
    for (const ToolSuiteTool& t : state->suite->tools) {
        if (t.name == entry.name) {
            tool = &t;
            break;
        }
    }

    if (!tool || tool->fn_ref == LUA_NOREF) {
        std::string result = entry.name.empty()
            ? std::string("Unknown tool")
            : std::string("Tool not found: ") + entry.name;
        int slot = state->msg_base + state->dispatched + 1;
        append_tool_message(L, state->messages_ref, slot, entry.id, result);
        state->dispatched++;
        return toolsuite_dispatch_one(L, state);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, tool->fn_ref);

    int nargs = 0;
    if (entry.args_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, entry.args_ref);
        int args_top = lua_gettop(L);
        if (lua_istable(L, args_top)) {
            for (const ToolSuiteParam& p : tool->params) {
                lua_getfield(L, args_top, p.name.c_str());
                nargs++;
            }
        }
        lua_remove(L, args_top);
    }

    int rc = lua_pcallk(L, nargs, 1, 0,
                        (lua_KContext)state, toolsuite_continuation);

    std::string result_str;
    if (rc == LUA_OK) {
        const char* res = lua_tostring(L, -1);
        result_str = res ? res : "";
    } else {
        const char* err = lua_tostring(L, -1);
        result_str = err ? std::string("error: ") + err : "error";
    }
    lua_pop(L, 1);

    int slot = state->msg_base + state->dispatched + 1;
    append_tool_message(L, state->messages_ref, slot, entry.id, result_str);
    state->dispatched++;

    return toolsuite_dispatch_one(L, state);
}

// ── Main loop — picks the next entry and runs gate (if any) then tool ─────────
static int toolsuite_dispatch_one(lua_State* L, ToolCallState* state) {
    while (state->next < (int)state->calls.size()) {
        state->current_idx = state->next;
        state->next++;

        const ToolCallEntry& entry = state->calls[state->current_idx];

        // If there is no gate, go straight to the tool.
        // dispatch_tool re-enters dispatch_one for remaining entries, so return here.
        if (state->gate_ref == LUA_NOREF)
            return toolsuite_dispatch_tool(L, state);

        // Push the gate: fn(name, args_table_or_nil)
        lua_rawgeti(L, LUA_REGISTRYINDEX, state->gate_ref);
        lua_pushlstring(L, entry.name.c_str(), entry.name.size());
        if (entry.args_ref != LUA_NOREF)
            lua_rawgeti(L, LUA_REGISTRYINDEX, entry.args_ref);
        else
            lua_pushnil(L);

        int rc = lua_pcallk(L, 2, 1, 0,
                            (lua_KContext)state, toolsuite_gate_continuation);

        // Synchronous return from gate
        bool allowed = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);

        if (!allowed) {
            int slot = state->msg_base + state->dispatched + 1;
            append_tool_message(L, state->messages_ref, slot,
                                entry.id, "error: permission denied");
            state->dispatched++;
            continue;
        }

        // Allowed — dispatch the tool; it re-enters dispatch_one itself
        return toolsuite_dispatch_tool(L, state);
    }

    int dispatched = state->dispatched;
    toolcallstate_free(L, state);
    lua_pushinteger(L, dispatched);
    return 1;
}

// ── tools:Callback(fn) ────────────────────────────────────────────────────────
// Registers an optional permission gate called before each tool invocation.
// fn(name, args) -> bool   (yieldable: may wait for user interaction)
// Pass nil to remove the gate.

int lua_toolsuite_setcallback(lua_State* L) {
    LuaToolSuite* suite = lua_toolsuite_check(L, 1);

    if (suite->gate_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, suite->gate_ref);
        suite->gate_ref = LUA_NOREF;
    }

    if (!lua_isnil(L, 2)) {
        luaL_checktype(L, 2, LUA_TFUNCTION);
        lua_pushvalue(L, 2);
        suite->gate_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    return 0;
}

// ── tools:Call(messages) ──────────────────────────────────────────────────────
// Inspects the last message in messages. If it is an assistant tool_calls
// message, dispatches each call to the matching registered function and appends
// {role='tool', content=result, tool_call_id=id} entries.
// Uses lua_pcallk so tool callbacks and the permission gate can yield.
// Returns the number of tools dispatched (0 if the last message is not a tool call).

int lua_toolsuite_call(lua_State* L) {
    LuaToolSuite* suite = lua_toolsuite_check(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    // Snapshot the stack depth now so every early-return path can restore it
    // with lua_settop rather than relying on counted pops.
    const int top = lua_gettop(L);

#define CALL_RETURN_ZERO() do { lua_settop(L, top); lua_pushinteger(L, 0); return 1; } while(0)

    int msg_count = (int)lua_rawlen(L, 2);
    if (msg_count == 0) {
        lua_pushinteger(L, 0);
        return 1;
    }

    // Get last message
    lua_rawgeti(L, 2, msg_count);
    if (!lua_istable(L, -1))
        CALL_RETURN_ZERO();

    // Check role == 'assistant' and tool_calls present
    lua_getfield(L, -1, "role");
    bool is_assistant = lua_isstring(L, -1) &&
                        std::string(lua_tostring(L, -1)) == "assistant";
    lua_pop(L, 1);
    if (!is_assistant)
        CALL_RETURN_ZERO();

    lua_getfield(L, -1, "tool_calls");
    if (lua_isnil(L, -1))
        CALL_RETURN_ZERO();

    // tool_calls is a JSON string — decode it via the shared bridge
    const char* tc_str = nullptr;
    size_t      tc_len = 0;
    if (lua_isstring(L, -1))
        tc_str = lua_tolstring(L, -1, &tc_len);

    if (!tc_str || tc_len == 0)
        CALL_RETURN_ZERO();

    // Decode tool_calls JSON using the shared bridge
    lua_rawgetp(L, LUA_REGISTRYINDEX, lua_json_bridge_registry_key());
    lua_pushcfunction(L, lua_json_decode);
    lua_insert(L, -2);
    lua_pushlstring(L, tc_str, tc_len);
    // This decode itself is plain pcall; it is a pure C function, not user code.
    if (lua_pcall(L, 2, 1, 0) != LUA_OK)
        CALL_RETURN_ZERO();

    if (!lua_istable(L, -1))
        CALL_RETURN_ZERO();

#undef CALL_RETURN_ZERO

    int calls_table = lua_gettop(L);
    int num_calls   = (int)lua_rawlen(L, calls_table);

    // Build the dispatch state from the decoded array while we still have the
    // Lua tables on the stack, then clean the stack before any yield can occur.
    ToolCallState* state = new ToolCallState();
    state->dispatched   = 0;
    state->next         = 0;
    state->current_idx  = 0;
    state->msg_base     = msg_count;
    state->suite        = suite;
    state->messages_ref = LUA_NOREF;
    state->gate_ref     = LUA_NOREF;
    // Take an independent owned ref to the gate so it cannot be freed under us
    // if the user calls suite:Callback(nil) while this Call is yielded mid-loop.
    if (suite->gate_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, suite->gate_ref);
        state->gate_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    // Store a registry ref to the messages table so it survives yields
    lua_pushvalue(L, 2);
    state->messages_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    for (int ci = 1; ci <= num_calls; ci++) {
        lua_rawgeti(L, calls_table, ci);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            continue;
        }

        ToolCallEntry entry;
        entry.args_ref = LUA_NOREF;

        lua_getfield(L, -1, "name");
        if (lua_isstring(L, -1))
            entry.name = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "id");
        if (lua_isstring(L, -1))
            entry.id = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, -1, "arguments");
        if (lua_istable(L, -1)) {
            // Anchor arguments table in registry so it survives yields
            entry.args_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        } else {
            lua_pop(L, 1);
        }

        lua_pop(L, 1); // pop call object
        state->calls.push_back(std::move(entry));
    }

    // Restore the stack to the baseline captured at function entry.
    // This clears decoded_array + tool_calls_str + last_msg in one safe call,
    // regardless of how many items the decode phase left behind.
    lua_settop(L, top);

    if (state->calls.empty()) {
        toolcallstate_free(L, state);
        lua_pushinteger(L, 0);
        return 1;
    }

    return toolsuite_dispatch_one(L, state);
}
