#include "platform.h"
#ifdef KITSUNE_LLAMA
#include "luallama_prompt.h"
#include "luajson.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

// ── CRC-32 helper ─────────────────────────────────────────────────────────────

static uint32_t crc32_update(uint32_t crc, const void* data, size_t len) {
	static const uint32_t table[256] = {
#define X(i) \
	(((i)>>1) ^ (((i)&1) ? 0xEDB88320u : 0u))
#define XX(i) X(i),X(i+1),X(i+2),X(i+3),X(i+4),X(i+5),X(i+6),X(i+7)
		XX(0),  XX(8),  XX(16), XX(24), XX(32), XX(40), XX(48), XX(56),
		XX(64), XX(72), XX(80), XX(88), XX(96), XX(104),XX(112),XX(120),
		XX(128),XX(136),XX(144),XX(152),XX(160),XX(168),XX(176),XX(184),
		XX(192),XX(200),XX(208),XX(216),XX(224),XX(232),XX(240),XX(248)
#undef XX
#undef X
	};
	const uint8_t* p = (const uint8_t*)data;
	for (size_t i = 0; i < len; i++)
		crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
	return crc;
}

static uint32_t crc32_str(uint32_t crc, const std::string& s) {
	return crc32_update(crc, s.data(), s.size());
}

// ── LlamaPrompt ───────────────────────────────────────────────────────────────

LlamaPrompt::LlamaPrompt() : next_id_(1) {}

void LlamaPrompt::assign_id(ChatMessage& msg) {
	msg.id = next_id_++;
}

void LlamaPrompt::SetSystem(const std::string& content) {
	system_content_ = content;
}

void LlamaPrompt::AddUserMessage(const std::string& content) {
	ChatMessage msg;
	msg.role    = "user";
	msg.content = content;
	assign_id(msg);
	messages_.push_back(std::move(msg));
}

void LlamaPrompt::AddAssistantMessage(const std::string& content,
									  const std::string& reasoning,
									  const std::vector<ToolCall>& tool_calls) {
	ChatMessage msg;
	msg.role       = "assistant";
	msg.content    = content;
	msg.reasoning  = reasoning;
	msg.tool_calls = tool_calls;
	assign_id(msg);
	messages_.push_back(std::move(msg));
}

void LlamaPrompt::AddToolResult(const std::string& tool_call_id,
								const std::string& content) {
	ChatMessage msg;
	msg.role         = "tool";
	msg.content      = content;
	msg.tool_call_id = tool_call_id;
	assign_id(msg);
	messages_.push_back(std::move(msg));
}

void LlamaPrompt::Clear() {
	messages_.clear();
	system_content_.clear();
	next_id_ = 1;
}

int LlamaPrompt::Count() const {
	return (int)messages_.size();
}

const std::string& LlamaPrompt::GetSystem() const {
	return system_content_;
}

const ChatMessage* LlamaPrompt::Get(int index) const {
	if (index < 1 || index > (int)messages_.size())
		return nullptr;
	return &messages_[(size_t)(index - 1)];
}

const ChatMessage* LlamaPrompt::Last() const {
	if (messages_.empty())
		return nullptr;
	return &messages_.back();
}

LlamaPrompt* LlamaPrompt::TrimmedFrom(int from_index) const {
	LlamaPrompt* p = new LlamaPrompt();
	p->system_content_ = system_content_;
	if (from_index < 1)
		from_index = 1;
	for (int i = from_index; i <= (int)messages_.size(); i++)
		p->messages_.push_back(messages_[(size_t)(i - 1)]);
	// Re-assign contiguous ids from 1 so the new prompt has consistent ids
	p->next_id_ = 1;
	for (ChatMessage& m : p->messages_)
		m.id = p->next_id_++;
	return p;
}

std::vector<ChatMessage> LlamaPrompt::BuildMessageList() const {
	std::vector<ChatMessage> out;
	out.reserve(1 + messages_.size());
	if (!system_content_.empty()) {
		ChatMessage sys;
		sys.role    = "system";
		sys.content = system_content_;
		out.push_back(std::move(sys));
	}
	for (const ChatMessage& m : messages_)
		out.push_back(m);
	return out;
}

uint32_t LlamaPrompt::ComputeHash() const {
	uint32_t crc = 0xFFFFFFFFu;
	crc = crc32_str(crc, system_content_);
	for (const ChatMessage& m : messages_) {
		crc = crc32_str(crc, m.role);
		crc = crc32_str(crc, m.content);
		crc = crc32_str(crc, m.reasoning);
		crc = crc32_str(crc, m.tool_call_id);
		for (const ToolCall& tc : m.tool_calls) {
			crc = crc32_str(crc, tc.id);
			crc = crc32_str(crc, tc.name);
			crc = crc32_str(crc, tc.arguments);
		}
	}
	return crc ^ 0xFFFFFFFFu;
}

// ── Lua helpers ───────────────────────────────────────────────────────────────

// Push one message as a Lua table onto the stack.
// {id, role, content [, reasoning] [, tool_call_id] [, tool_calls]}
static void push_message_table(lua_State* L, const ChatMessage* msg) {
	lua_createtable(L, 0, 6);

	lua_pushinteger(L, (lua_Integer)msg->id);
	lua_setfield(L, -2, "id");

	lua_pushlstring(L, msg->role.c_str(), msg->role.size());
	lua_setfield(L, -2, "role");

	lua_pushlstring(L, msg->content.c_str(), msg->content.size());
	lua_setfield(L, -2, "content");

	if (!msg->reasoning.empty()) {
		lua_pushlstring(L, msg->reasoning.c_str(), msg->reasoning.size());
		lua_setfield(L, -2, "reasoning");
	}

	if (!msg->tool_call_id.empty()) {
		lua_pushlstring(L, msg->tool_call_id.c_str(), msg->tool_call_id.size());
		lua_setfield(L, -2, "tool_call_id");
	}

	if (!msg->tool_calls.empty()) {
		lua_createtable(L, (int)msg->tool_calls.size(), 0);
		for (int i = 0; i < (int)msg->tool_calls.size(); i++) {
			const ToolCall& tc = msg->tool_calls[(size_t)i];
			lua_createtable(L, 0, 3);
			lua_pushlstring(L, tc.id.c_str(), tc.id.size());
			lua_setfield(L, -2, "id");
			lua_pushlstring(L, tc.name.c_str(), tc.name.size());
			lua_setfield(L, -2, "name");
			lua_pushlstring(L, tc.arguments.c_str(), tc.arguments.size());
			lua_setfield(L, -2, "arguments");
			lua_rawseti(L, -2, i + 1);
		}
		lua_setfield(L, -2, "tool_calls");
	}
}

// ── LuaLlamaPrompt lifecycle ──────────────────────────────────────────────────

LuaLlamaPrompt* lua_llama_prompt_push(lua_State* L) {
	LuaLlamaPrompt* p = (LuaLlamaPrompt*)lua_newuserdata(L, sizeof(LuaLlamaPrompt));
	p->prompt = new LlamaPrompt();
	luaL_setmetatable(L, LUALLAMA_PROMPT);
	return p;
}

LuaLlamaPrompt* lua_llama_prompt_check(lua_State* L, int idx) {
	return (LuaLlamaPrompt*)luaL_checkudata(L, idx, LUALLAMA_PROMPT);
}

bool lua_llama_prompt_is(lua_State* L, int idx) {
	if (!lua_isuserdata(L, idx))
		return false;
	if (luaL_testudata(L, idx, LUALLAMA_PROMPT) != nullptr)
		return true;
	return false;
}

// ── Metatable methods ─────────────────────────────────────────────────────────

int lua_llama_prompt_gc(lua_State* L) {
	LuaLlamaPrompt* p = lua_llama_prompt_check(L, 1);
	if (p->prompt) {
		delete p->prompt;
		p->prompt = nullptr;
	}
	return 0;
}

int lua_llama_prompt_tostring(lua_State* L) {
	LuaLlamaPrompt* p = lua_llama_prompt_check(L, 1);
	int count = p->prompt ? p->prompt->Count() : 0;
	lua_pushfstring(L, "LlamaPrompt(%d messages)", count);
	return 1;
}

int lua_llama_prompt_len(lua_State* L) {
	LuaLlamaPrompt* p = lua_llama_prompt_check(L, 1);
	lua_pushinteger(L, p->prompt ? p->prompt->Count() : 0);
	return 1;
}

// ── Method implementations ────────────────────────────────────────────────────

// prompt:SetSystem(str)
int lua_llama_prompt_setsystem(lua_State* L) {
	LuaLlamaPrompt* p = lua_llama_prompt_check(L, 1);
	size_t len = 0;
	const char* s = luaL_checklstring(L, 2, &len);
	p->prompt->SetSystem(std::string(s, len));
	return 0;
}

// prompt:GetSystem() -> str
int lua_llama_prompt_getsystem(lua_State* L) {
	LuaLlamaPrompt* p = lua_llama_prompt_check(L, 1);
	const std::string& sys = p->prompt->GetSystem();
	lua_pushlstring(L, sys.c_str(), sys.size());
	return 1;
}

// prompt:AddUserMessage(str)
int lua_llama_prompt_addusr(lua_State* L) {
	LuaLlamaPrompt* p = lua_llama_prompt_check(L, 1);
	size_t len = 0;
	const char* s = luaL_checklstring(L, 2, &len);
	p->prompt->AddUserMessage(std::string(s, len));
	return 0;
}

// prompt:AddAssistantMessage(content [, reasoning])
int lua_llama_prompt_addassistant(lua_State* L) {
	LuaLlamaPrompt* p = lua_llama_prompt_check(L, 1);
	size_t clen = 0;
	const char* content = luaL_checklstring(L, 2, &clen);
	std::string reasoning;
	if (lua_isstring(L, 3)) {
		size_t rlen = 0;
		const char* r = lua_tolstring(L, 3, &rlen);
		reasoning.assign(r, rlen);
	}
	p->prompt->AddAssistantMessage(std::string(content, clen), reasoning, {});
	return 0;
}

// prompt:Export() -> { system = str, messages = { {role,content,...}, ... } }
int lua_llama_prompt_export(lua_State* L) {
	LuaLlamaPrompt* p = lua_llama_prompt_check(L, 1);
	lua_createtable(L, 0, 2);

	// system field
	const std::string& sys = p->prompt->GetSystem();
	lua_pushlstring(L, sys.c_str(), sys.size());
	lua_setfield(L, -2, "system");

	// messages array
	int count = p->prompt->Count();
	lua_createtable(L, count, 0);
	for (int i = 1; i <= count; i++) {
		const ChatMessage* msg = p->prompt->Get(i);
		push_message_table(L, msg);
		lua_rawseti(L, -2, i);
	}
	lua_setfield(L, -2, "messages");

	return 1;
}

// prompt:AddMessage(table)  -- table in the same shape as prompt[i]
// Accepts: { role="user"|"assistant"|"tool", content=..., [reasoning=...,]
//            [tool_call_id=...,] [tool_calls={{id,name,arguments},...}] }
int lua_llama_prompt_addmessage(lua_State* L) {
	LuaLlamaPrompt* p = lua_llama_prompt_check(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);

	// role
	lua_getfield(L, 2, "role");
	const char* role = luaL_checkstring(L, -1);
	lua_pop(L, 1);

	// content
	lua_getfield(L, 2, "content");
	size_t clen = 0;
	const char* content = luaL_optlstring(L, -1, "", &clen);
	std::string content_str(content, clen);
	lua_pop(L, 1);

	if (std::strcmp(role, "user") == 0) {
		p->prompt->AddUserMessage(content_str);
	}
	else if (std::strcmp(role, "assistant") == 0) {
		// reasoning (optional)
		std::string reasoning;
		lua_getfield(L, 2, "reasoning");
		if (lua_isstring(L, -1)) {
			size_t rlen = 0;
			const char* r = lua_tolstring(L, -1, &rlen);
			reasoning.assign(r, rlen);
		}
		lua_pop(L, 1);

		// tool_calls (optional array)
		std::vector<ToolCall> tool_calls;
		lua_getfield(L, 2, "tool_calls");
		if (lua_istable(L, -1)) {
			int n = (int)lua_rawlen(L, -1);
			for (int i = 1; i <= n; i++) {
				lua_rawgeti(L, -1, i);
				if (lua_istable(L, -1)) {
					ToolCall tc;
					lua_getfield(L, -1, "id");
					if (lua_isstring(L, -1))
						tc.id = lua_tostring(L, -1);
					lua_pop(L, 1);
					lua_getfield(L, -1, "name");
					if (lua_isstring(L, -1))
						tc.name = lua_tostring(L, -1);
					lua_pop(L, 1);
					lua_getfield(L, -1, "arguments");
					if (lua_isstring(L, -1))
						tc.arguments = lua_tostring(L, -1);
					lua_pop(L, 1);
					tool_calls.push_back(std::move(tc));
				}
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);

		p->prompt->AddAssistantMessage(content_str, reasoning, tool_calls);
	}
	else if (std::strcmp(role, "tool") == 0) {
		lua_getfield(L, 2, "tool_call_id");
		size_t idlen = 0;
		const char* id = luaL_optlstring(L, -1, "", &idlen);
		std::string id_str(id, idlen);
		lua_pop(L, 1);
		p->prompt->AddToolResult(id_str, content_str);
	}
	else {
		return luaL_error(L, "AddMessage: unknown role '%s'", role);
	}

	return 0;
}

// prompt:AddToolResult(tool_call_id, content)
int lua_llama_prompt_addtoolresult(lua_State* L) {
	LuaLlamaPrompt* p = lua_llama_prompt_check(L, 1);
	size_t idlen = 0;
	const char* id = luaL_checklstring(L, 2, &idlen);
	size_t clen = 0;
	const char* content = luaL_checklstring(L, 3, &clen);
	p->prompt->AddToolResult(std::string(id, idlen), std::string(content, clen));
	return 0;
}

// prompt:Last() -> table or nil
int lua_llama_prompt_last(lua_State* L) {
	LuaLlamaPrompt* p = lua_llama_prompt_check(L, 1);
	const ChatMessage* msg = p->prompt->Last();
	if (!msg) {
		lua_pushnil(L);
		return 1;
	}
	push_message_table(L, msg);
	return 1;
}

// prompt:TrimmedFrom(index) -> new prompt userdata
int lua_llama_prompt_trimmedfrom(lua_State* L) {
	LuaLlamaPrompt* p = lua_llama_prompt_check(L, 1);
	int from = (int)luaL_checkinteger(L, 2);
	LuaLlamaPrompt* np = lua_llama_prompt_push(L);
	delete np->prompt;
	np->prompt = p->prompt->TrimmedFrom(from);
	return 1;
}

// prompt:Clear()
int lua_llama_prompt_clear(lua_State* L) {
	LuaLlamaPrompt* p = lua_llama_prompt_check(L, 1);
	p->prompt->Clear();
	return 0;
}

// prompt:Import(data)  -- data is in the same shape Export() returns
// Clears the prompt then loads system + messages from the table.
int lua_llama_prompt_import(lua_State* L) {
	LuaLlamaPrompt* p = lua_llama_prompt_check(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);

	p->prompt->Clear();

	// system
	lua_getfield(L, 2, "system");
	if (lua_isstring(L, -1)) {
		size_t len = 0;
		const char* s = lua_tolstring(L, -1, &len);
		p->prompt->SetSystem(std::string(s, len));
	}
	lua_pop(L, 1);

	// messages — reuse AddMessage to keep parsing in one place
	lua_getfield(L, 2, "messages");
	if (lua_istable(L, -1)) {
		int n = (int)lua_rawlen(L, -1);
		for (int i = 1; i <= n; i++) {
			lua_rawgeti(L, -1, i);
			if (lua_istable(L, -1)) {
				lua_pushcfunction(L, lua_llama_prompt_addmessage);
				lua_pushvalue(L, 1);   // self
				lua_pushvalue(L, -3);  // the message table
				lua_call(L, 2, 0);
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);

	return 0;
}

// ── __index dispatch ──────────────────────────────────────────────────────────

static const struct { const char* name; lua_CFunction fn; } prompt_methods[] = {
	{ "SetSystem",          lua_llama_prompt_setsystem    },
	{ "GetSystem",          lua_llama_prompt_getsystem    },
	{ "AddUserMessage",     lua_llama_prompt_addusr       },
	{ "AddAssistantMessage",lua_llama_prompt_addassistant },
	{ "AddToolResult",      lua_llama_prompt_addtoolresult},
	{ "AddMessage",         lua_llama_prompt_addmessage   },
	{ "Export",             lua_llama_prompt_export       },
	{ "Import",             lua_llama_prompt_import       },
	{ "Last",               lua_llama_prompt_last         },
	{ "TrimmedFrom",        lua_llama_prompt_trimmedfrom  },
	{ "Clear",              lua_llama_prompt_clear        },
	{ nullptr, nullptr }
};

// prompt[i]  -> message table (1-based)
// prompt.MethodName -> function
int lua_llama_prompt_index(lua_State* L) {
	LuaLlamaPrompt* p = lua_llama_prompt_check(L, 1);

	if (lua_type(L, 2) == LUA_TNUMBER) {
		int idx = (int)lua_tointeger(L, 2);
		const ChatMessage* msg = p->prompt->Get(idx);
		if (!msg) {
			lua_pushnil(L);
			return 1;
		}
		push_message_table(L, msg);
		return 1;
	}

	const char* key = lua_tostring(L, 2);
	if (!key) {
		lua_pushnil(L);
		return 1;
	}

	for (int i = 0; prompt_methods[i].name; i++) {
		if (std::strcmp(prompt_methods[i].name, key) == 0) {
			lua_pushcfunction(L, prompt_methods[i].fn);
			return 1;
		}
	}

	lua_pushnil(L);
	return 1;
}

// ── Llama.CreatePrompt() ──────────────────────────────────────────────────────

int lua_llama_prompt_new(lua_State* L) {
	lua_llama_prompt_push(L);
	return 1;
}

// ── Register metatable (called from the Llama module open function) ───────────

void lua_llama_prompt_register(lua_State* L) {
	luaL_newmetatable(L, LUALLAMA_PROMPT);

	lua_pushcfunction(L, lua_llama_prompt_gc);
	lua_setfield(L, -2, "__gc");

	lua_pushcfunction(L, lua_llama_prompt_tostring);
	lua_setfield(L, -2, "__tostring");

	lua_pushcfunction(L, lua_llama_prompt_len);
	lua_setfield(L, -2, "__len");

	lua_pushcfunction(L, lua_llama_prompt_index);
	lua_setfield(L, -2, "__index");

	lua_pop(L, 1); // pop metatable
}

#endif // KITSUNE_LLAMA
