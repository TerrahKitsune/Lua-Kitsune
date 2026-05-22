#pragma once
#include "lua_main_incl.h"
#ifdef KITSUNE_LLAMA
#include "LlamaContext.h"
#include <string>
#include <vector>
#include <cstdint>

#define LUALLAMA_PROMPT "LUALLAMA_PROMPT"

// ── LlamaPrompt ───────────────────────────────────────────────────────────────
//
// Owns the full conversation history in a neutral, model-agnostic format.
// The system message is stored separately and always prepended when building
// the working message list for the model.
//
// Rules:
//   - Messages are append-only; ids are stable and assigned in order from 1.
//   - The prompt is never mutated by LlamaContext or tools:Call — they work
//     on copies or append new entries via the public API.
//   - FormatPrompt reads role/content/tool_calls only; reasoning and id are
//     never sent to the model.

class LlamaPrompt {
public:
	LlamaPrompt();

	// ── Mutation API ───────────────────────────────────────────────────────

	void        SetSystem(const std::string& content);
	void        AddUserMessage(const std::string& content);
	void        AddAssistantMessage(const std::string& content,
									const std::string& reasoning,
									const std::vector<ToolCall>& tool_calls);
	void        AddToolResult(const std::string& tool_call_id,
							  const std::string& content);
	void        Clear();

	// ── Query API ─────────────────────────────────────────────────────────

	int                   Count()     const;
	const std::string&    GetSystem() const;

	// 1-based; returns nullptr if out of range.
	const ChatMessage*    Get(int index) const;

	// Returns the last message, or nullptr if empty.
	const ChatMessage*    Last() const;

	// ── Derived views ─────────────────────────────────────────────────────

	// Returns a new heap-allocated LlamaPrompt that is a copy of this one
	// starting from the message with the given 1-based index.
	// The system message is always preserved.
	// Caller owns the returned pointer.
	LlamaPrompt*          TrimmedFrom(int from_index) const;

	// Builds the flat message list passed to ChatTemplate::FormatPrompt.
	// System message (if set) is inserted at position 0.
	std::vector<ChatMessage> BuildMessageList() const;

	// CRC-32 over the full content (system + all messages).
	// Used by LlamaContext to detect whether the KV cache can be reused.
	uint32_t              ComputeHash() const;

private:
	uint32_t                  next_id_;
	std::string               system_content_;
	std::vector<ChatMessage>  messages_;

	void assign_id(ChatMessage& msg);
};

// ── LuaLlamaPrompt ────────────────────────────────────────────────────────────
//
// Thin Lua userdata wrapper around a heap-allocated LlamaPrompt.
// __gc deletes the owned pointer.

struct LuaLlamaPrompt {
	LlamaPrompt* prompt;
};

LuaLlamaPrompt* lua_llama_prompt_push(lua_State* L);
LuaLlamaPrompt* lua_llama_prompt_check(lua_State* L, int idx);
bool            lua_llama_prompt_is(lua_State* L, int idx);

// Called once from the module open function to register the metatable.
void lua_llama_prompt_register(lua_State* L);

// ── Lua-facing functions ──────────────────────────────────────────────────────

// Llama.CreatePrompt() -> prompt userdata
int lua_llama_prompt_new(lua_State* L);
int lua_llama_prompt_gc(lua_State* L);
int lua_llama_prompt_tostring(lua_State* L);
int lua_llama_prompt_len(lua_State* L);      // #prompt
int lua_llama_prompt_index(lua_State* L);    // prompt[i] or prompt:method()

// Methods exposed via __index dispatch:
int lua_llama_prompt_setsystem(lua_State* L);       // prompt:SetSystem(str)
int lua_llama_prompt_addusr(lua_State* L);          // prompt:AddUserMessage(str)
int lua_llama_prompt_addassistant(lua_State* L);    // prompt:AddAssistantMessage(str [,reasoning])
int lua_llama_prompt_addtoolresult(lua_State* L);   // prompt:AddToolResult(id, str)
int lua_llama_prompt_getsystem(lua_State* L);       // prompt:GetSystem() -> str
int lua_llama_prompt_last(lua_State* L);            // prompt:Last() -> table or nil
int lua_llama_prompt_trimmedfrom(lua_State* L);     // prompt:TrimmedFrom(id) -> new prompt
int lua_llama_prompt_clear(lua_State* L);           // prompt:Clear()

#endif // KITSUNE_LLAMA
