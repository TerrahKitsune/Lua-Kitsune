#include "platform.h"
#include "luallama.h"
#include "luatoolsuite.h"
#include "LlamaContext.h"
#include "luajson.h"
#include <cstring>

LuaLlama* lua_llama_push(lua_State* L) {
	LuaLlama* x = (LuaLlama*)lua_newuserdata(L, sizeof(LuaLlama));
	memset(x, 0, sizeof(LuaLlama));
	x->messages_ref = LUA_NOREF;
	luaL_setmetatable(L, LUALLAMA);
	return x;
}

LuaLlama* lua_llama_check(lua_State* L, int idx) {
	return (LuaLlama*)luaL_checkudata(L, idx, LUALLAMA);
}

#ifdef KITSUNE_LLAMA

static int push_disposed_error(lua_State* L) {
	lua_pushnil(L);
	lua_pushliteral(L, "disposed");
	return 2;
}

static int push_busy_error(lua_State* L) {
	lua_pushnil(L);
	lua_pushliteral(L, "busy");
	return 2;
}

// Llama.CreateContext(opts) -> userdata or nil, err
int lua_llama_new(lua_State* L) {
	LlamaCtxOpts opts;

	if (lua_istable(L, 1)) {
		lua_getfield(L, 1, "n_gpu_layers");
		if (!lua_isnil(L, -1))
			opts.n_gpu_layers = (int)lua_tointeger(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 1, "n_ctx");
		if (!lua_isnil(L, -1))
			opts.n_ctx = (int)lua_tointeger(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 1, "n_threads");
		if (!lua_isnil(L, -1))
			opts.n_threads = (int)lua_tointeger(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 1, "n_batch");
		if (!lua_isnil(L, -1))
			opts.n_batch = (int)lua_tointeger(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 1, "flash_attn");
		if (!lua_isnil(L, -1))
			opts.flash_attn = lua_toboolean(L, -1) != 0;
		lua_pop(L, 1);

		lua_getfield(L, 1, "model_ttl_ms");
		if (!lua_isnil(L, -1))
			opts.model_ttl_ms = (int64_t)lua_tointeger(L, -1);
		lua_pop(L, 1);
	}

	LuaLlama* llama = lua_llama_push(L);
	if (!llama)
		return luaL_error(L, "out of memory");

	llama->context = new LlamaContext(opts);
	if (!llama->context)
		return luaL_error(L, "out of memory");

	return 1;
}

int lua_llama_gc(lua_State* L) {
	LuaLlama* llama = lua_llama_check(L, 1);
	if (llama->messages_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, llama->messages_ref);
		llama->messages_ref = LUA_NOREF;
	}
	if (llama->context) {
		delete llama->context;
		llama->context = nullptr;
	}
	return 0;
}

int lua_llama_tostring(lua_State* L) {
	lua_pushfstring(L, "LlamaContext: %p", lua_llama_check(L, 1));
	return 1;
}

// ctx:SetModel(path, opts) -> true or nil, err
int lua_llama_setmodel(lua_State* L) {
	LuaLlama* llama = lua_llama_check(L, 1);
	if (!llama->context || llama->context->IsDisposed())
		return push_disposed_error(L);

	const char* path = luaL_checkstring(L, 2);
	int n_gpu_layers = -1;

	if (lua_istable(L, 3)) {
		lua_getfield(L, 3, "n_gpu_layers");
		if (!lua_isnil(L, -1))
			n_gpu_layers = (int)lua_tointeger(L, -1);
		lua_pop(L, 1);
	}

	if (!llama->context->SetModel(path, n_gpu_layers))
		return push_busy_error(L);

	lua_pushboolean(L, 1);
	return 1;
}

// ctx:LoadModel() -> true or nil, err
int lua_llama_loadmodel(lua_State* L) {
	LuaLlama* llama = lua_llama_check(L, 1);
	if (!llama->context || llama->context->IsDisposed())
		return push_disposed_error(L);

	if (!llama->context->LoadModel()) {
		lua_pushnil(L);
		std::string err = llama->context->GetError();
		if (err.empty())
			lua_pushliteral(L, "busy");
		else
			lua_pushstring(L, err.c_str());
		return 2;
	}

	lua_pushboolean(L, 1);
	return 1;
}

// ctx:UnloadModel() -> true or nil, err
int lua_llama_unloadmodel(lua_State* L) {
	LuaLlama* llama = lua_llama_check(L, 1);
	if (!llama->context || llama->context->IsDisposed())
		return push_disposed_error(L);

	if (!llama->context->UnloadModel())
		return push_busy_error(L);

	lua_pushboolean(L, 1);
	return 1;
}

// ctx:IsModelLoaded() -> bool
int lua_llama_ismodelloaded(lua_State* L) {
	LuaLlama* llama = lua_llama_check(L, 1);
	if (!llama->context || llama->context->IsDisposed()) {
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, llama->context->IsModelLoaded() ? 1 : 0);
	return 1;
}

// ctx:IsReady() -> bool
int lua_llama_isready(lua_State* L) {
	LuaLlama* llama = lua_llama_check(L, 1);
	if (!llama->context || llama->context->IsDisposed()) {
		lua_pushboolean(L, 0);
		return 1;
	}
	lua_pushboolean(L, llama->context->IsReady() ? 1 : 0);
	return 1;
}

// Helper: read messages table from Lua stack
static void read_messages(lua_State* L, int idx, std::vector<ChatMessage>& out) {
	luaL_checktype(L, idx, LUA_TTABLE);
	int n = (int)lua_rawlen(L, idx);
	out.reserve(n);

	for (int i = 1; i <= n; i++) {
		lua_rawgeti(L, idx, i);
		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			continue;
		}

		ChatMessage msg;

		lua_getfield(L, -1, "role");
		if (lua_isstring(L, -1))
			msg.role = lua_tostring(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, -1, "content");
		if (lua_isstring(L, -1))
			msg.content = lua_tostring(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, -1, "tool_call_id");
		if (lua_isstring(L, -1))
			msg.tool_call_id = lua_tostring(L, -1);
		lua_pop(L, 1);

		// tool_calls can be a table or JSON string
		lua_getfield(L, -1, "tool_calls");
		if (lua_isstring(L, -1)) {
			msg.tool_calls_json = lua_tostring(L, -1);
		} else if (lua_istable(L, -1)) {
			// Serialize table to JSON manually (simple approach)
			// We build a JSON string from the OpenAI-format tool_calls array
			std::string json = "[";
			int tc_n = (int)lua_rawlen(L, -1);
			for (int j = 1; j <= tc_n; j++) {
				lua_rawgeti(L, -1, j);
				if (lua_istable(L, -1)) {
					if (j > 1)
						json += ",";
					json += "{";

					lua_getfield(L, -1, "id");
					if (lua_isstring(L, -1))
						json += "\"id\":\"" + std::string(lua_tostring(L, -1)) + "\",";
					lua_pop(L, 1);

					lua_getfield(L, -1, "type");
					if (lua_isstring(L, -1))
						json += "\"type\":\"" + std::string(lua_tostring(L, -1)) + "\",";
					lua_pop(L, 1);

					lua_getfield(L, -1, "function");
					if (lua_istable(L, -1)) {
						json += "\"function\":{";
						lua_getfield(L, -1, "name");
						if (lua_isstring(L, -1))
							json += "\"name\":\"" + std::string(lua_tostring(L, -1)) + "\",";
						lua_pop(L, 1);

						lua_getfield(L, -1, "arguments");
						if (lua_isstring(L, -1))
							json += "\"arguments\":\"" + std::string(lua_tostring(L, -1)) + "\"";
						lua_pop(L, 1);

						json += "}";
					}
					lua_pop(L, 1);

					json += "}";
				}
				lua_pop(L, 1);
			}
			json += "]";
			msg.tool_calls_json = json;
		}
		lua_pop(L, 1);

		out.push_back(std::move(msg));
		lua_pop(L, 1);
	}
}

// ctx:Generate(messages, opts) -> true or nil, err
int lua_llama_generate(lua_State* L) {
	LuaLlama* llama = lua_llama_check(L, 1);
	if (!llama->context || llama->context->IsDisposed())
		return push_disposed_error(L);

	std::vector<ChatMessage> messages;
	read_messages(L, 2, messages);
	if (messages.empty()) {
		lua_pushnil(L);
		lua_pushliteral(L, "empty messages");
		return 2;
	}

	LlamaGenOpts opts;
	if (lua_istable(L, 3)) {
		lua_getfield(L, 3, "temperature");
		if (!lua_isnil(L, -1))
			opts.temperature = (float)lua_tonumber(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 3, "top_p");
		if (!lua_isnil(L, -1))
			opts.top_p = (float)lua_tonumber(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 3, "top_k");
		if (!lua_isnil(L, -1))
			opts.top_k = (int)lua_tointeger(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 3, "min_p");
		if (!lua_isnil(L, -1))
			opts.min_p = (float)lua_tonumber(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 3, "seed");
		if (!lua_isnil(L, -1))
			opts.seed = (int)lua_tointeger(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 3, "max_tokens");
		if (!lua_isnil(L, -1))
			opts.max_tokens = (int)lua_tointeger(L, -1);
		lua_pop(L, 1);
	}

	// Optional tools: 4th arg when opts present, 3rd arg otherwise; string, table, or ToolSuite
	int tools_arg = lua_istable(L, 3) ? 4 : 3;
	if (lua_toolsuite_is(L, tools_arg)) {
		// Extract JSON directly from the ToolSuite userdata
		lua_pushcfunction(L, lua_toolsuite_getjson);
		lua_pushvalue(L, tools_arg);
		if (lua_pcall(L, 1, 1, 0) == LUA_OK) {
			size_t len = 0;
			const char* s = lua_tolstring(L, -1, &len);
			if (s && len > 0)
				opts.tools_json.assign(s, len);
			lua_pop(L, 1);
		} else {
			lua_pop(L, 1);
		}
	} else if (lua_isstring(L, tools_arg)) {
		size_t len = 0;
		const char* s = lua_tolstring(L, tools_arg, &len);
		if (s && len > 0)
			opts.tools_json.assign(s, len);
	} else if (lua_istable(L, tools_arg)) {
		// Serialize via a fresh LuaJson with emptyObjectAsSentinel so empty
		// parameter schemas encode as {} rather than [].
		LuaJson* j = lua_json_push(L);
		j->emptyObjectAsSentinel = 1;
		lua_pushcfunction(L, lua_json_encode);
		lua_insert(L, -2);               // json_encode fn, then LuaJson on stack
		lua_pushvalue(L, tools_arg);     // the tools table
		if (lua_pcall(L, 2, 1, 0) == LUA_OK) {
			size_t len = 0;
			const char* s = lua_tolstring(L, -1, &len);
			if (s && len > 0)
				opts.tools_json.assign(s, len);
			lua_pop(L, 1);
		} else {
			lua_pop(L, 1); // discard pcall error; proceed without tools
		}
	}

	// Release any previous messages ref
	if (llama->messages_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, llama->messages_ref);
		llama->messages_ref = LUA_NOREF;
	}

	if (!llama->context->Generate(std::move(messages), std::move(opts))) {
		lua_pushnil(L);
		LlamaStatus s = llama->context->GetStatus();
		if (s == LlamaStatus::GENERATING)
			lua_pushliteral(L, "already running");
		else if (llama->context->GetModelPath().empty())
			lua_pushliteral(L, "no model");
		else
			lua_pushliteral(L, "busy");
		return 2;
	}

	// Store a reference to the messages table so Poll can append the reply
	lua_pushvalue(L, 2);
	llama->messages_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	lua_pushboolean(L, 1);
	return 1;
}

// ctx:Poll() -> text, type, status
int lua_llama_poll(lua_State* L) {
	LuaLlama* llama = lua_llama_check(L, 1);
	if (!llama->context || llama->context->IsDisposed()) {
		lua_pushboolean(L, 0);
		lua_pushnil(L);
		return 2;
	}

	std::string text, type;
	bool ok = llama->context->Poll(text, type);

	if (!ok) {
		// Append the assistant reply to the stored messages table before returning
		if (llama->messages_ref != LUA_NOREF) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, llama->messages_ref);
			if (lua_istable(L, -1)) {
				int n = (int)lua_rawlen(L, -1);
				lua_newtable(L);                            // new message table
				lua_pushliteral(L, "assistant");
				lua_setfield(L, -2, "role");
				if (llama->context->HasToolCall()) {
					// tool_calls turn: empty content + tool_calls JSON
					lua_pushliteral(L, "");
					lua_setfield(L, -2, "content");
					const std::string& tc = llama->context->GetToolCallJson();
					lua_pushlstring(L, tc.c_str(), tc.size());
					lua_setfield(L, -2, "tool_calls");
				} else {
					const std::string& content = llama->context->GetFullContent();
					lua_pushlstring(L, content.c_str(), content.size());
					lua_setfield(L, -2, "content");
				}
				lua_rawseti(L, -2, n + 1);                 // messages[#messages+1] = msg
			}
			lua_pop(L, 1);                                  // pop messages table
			luaL_unref(L, LUA_REGISTRYINDEX, llama->messages_ref);
			llama->messages_ref = LUA_NOREF;
		}

		lua_pushboolean(L, 0);
		if (!text.empty()) {
			lua_newtable(L);
			lua_pushlstring(L, text.c_str(), text.size());
			lua_setfield(L, -2, "text");
			lua_pushliteral(L, "error");
			lua_setfield(L, -2, "type");
		} else {
			lua_pushnil(L);
		}
		return 2;
	}

	lua_pushboolean(L, 1);
	if (text.empty()) {
		lua_pushnil(L);
	} else {
		lua_newtable(L);
		lua_pushlstring(L, text.c_str(), text.size());
		lua_setfield(L, -2, "text");
		lua_pushstring(L, type.c_str());
		lua_setfield(L, -2, "type");
	}
	return 2;
}

// ctx:Stop() -> true
int lua_llama_stop(lua_State* L) {
	LuaLlama* llama = lua_llama_check(L, 1);
	if (llama->context)
		llama->context->Stop();
	lua_pushboolean(L, 1);
	return 1;
}

// ctx:Reset() -> true or nil, err
int lua_llama_reset(lua_State* L) {
	LuaLlama* llama = lua_llama_check(L, 1);
	if (!llama->context || llama->context->IsDisposed())
		return push_disposed_error(L);

	if (!llama->context->Reset())
		return push_busy_error(L);

	lua_pushboolean(L, 1);
	return 1;
}

// ctx:Embed(text) -> float[] or nil, err
int lua_llama_embed(lua_State* L) {
	LuaLlama* llama = lua_llama_check(L, 1);
	if (!llama->context || llama->context->IsDisposed())
		return push_disposed_error(L);

	const char* text = luaL_checkstring(L, 2);

	if (!llama->context->Embed(text)) {
		lua_pushnil(L);
		std::string err = llama->context->GetError();
		if (err.empty())
			lua_pushliteral(L, "busy");
		else
			lua_pushstring(L, err.c_str());
		return 2;
	}

	const std::vector<float>& result = llama->context->GetEmbedResult();
	lua_createtable(L, (int)result.size(), 0);
	for (int i = 0; i < (int)result.size(); i++) {
		lua_pushnumber(L, (lua_Number)result[i]);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

// ctx:Info() -> table
int lua_llama_info(lua_State* L) {
	LuaLlama* llama = lua_llama_check(L, 1);
	if (!llama->context || llama->context->IsDisposed())
		return push_disposed_error(L);

	LlamaContext* ctx = llama->context;
	lua_createtable(L, 0, 2);

	// context sub-table
	lua_createtable(L, 0, 11);
	{
		LlamaStatus s = ctx->GetStatus();
		const char* status_str = "idle";
		switch (s) {
		case LlamaStatus::LOADING:    status_str = "loading"; break;
		case LlamaStatus::GENERATING: status_str = "generating"; break;
		case LlamaStatus::UNLOADING:  status_str = "unloading"; break;
		case LlamaStatus::FAILED:      status_str = "error"; break;
		default: break;
		}
		lua_pushstring(L, status_str);
		lua_setfield(L, -2, "status");

		const LlamaCtxOpts& opts = ctx->GetCtxOpts();
		lua_pushinteger(L, opts.n_ctx);
		lua_setfield(L, -2, "n_ctx");
		lua_pushinteger(L, opts.n_gpu_layers);
		lua_setfield(L, -2, "n_gpu_layers");
		lua_pushinteger(L, opts.n_threads > 0 ? opts.n_threads : (int)std::thread::hardware_concurrency());
		lua_setfield(L, -2, "n_threads");
		lua_pushinteger(L, opts.n_batch);
		lua_setfield(L, -2, "n_batch");
		lua_pushinteger(L, opts.model_ttl_ms);
		lua_setfield(L, -2, "model_ttl_ms");

		std::string mp = ctx->GetModelPath();
		if (!mp.empty())
			lua_pushstring(L, mp.c_str());
		else
			lua_pushnil(L);
		lua_setfield(L, -2, "model_path");

		std::string err = ctx->GetError();
		if (!err.empty())
			lua_pushstring(L, err.c_str());
		else
			lua_pushnil(L);
		lua_setfield(L, -2, "error");

		double last = ctx->GetSecondsSinceLastUsed();
		if (last >= 0.0)
			lua_pushnumber(L, last);
		else
			lua_pushnil(L);
		lua_setfield(L, -2, "last_used");

		lua_pushinteger(L, ctx->GetTokensUsed());
		lua_setfield(L, -2, "tokens_used");
		lua_pushinteger(L, ctx->GetTokensAvailable());
		lua_setfield(L, -2, "tokens_available");
		lua_pushinteger(L, ctx->GetLastMessagesUsed());
		lua_setfield(L, -2, "last_messages_used");
	}
	lua_setfield(L, -2, "context");

	// model sub-table (nil when not loaded)
	if (ctx->IsModelLoaded()) {
		lua_createtable(L, 0, 16);
		{
			std::string desc = ctx->GetModelDesc();
			lua_pushstring(L, desc.c_str());
			lua_setfield(L, -2, "desc");

			std::string arch = ctx->GetModelArch();
			lua_pushstring(L, arch.c_str());
			lua_setfield(L, -2, "arch");

			lua_pushinteger(L, ctx->GetModelContextLength());
			lua_setfield(L, -2, "context_length");

			lua_pushinteger(L, (lua_Integer)ctx->GetModelParamCount());
			lua_setfield(L, -2, "n_params");

			lua_pushinteger(L, ctx->GetModelEmbeddingDim());
			lua_setfield(L, -2, "n_embd");

			lua_pushinteger(L, ctx->GetModelLayerCount());
			lua_setfield(L, -2, "n_layer");

			lua_pushinteger(L, (lua_Integer)ctx->GetModelSizeBytes());
			lua_setfield(L, -2, "size_bytes");

			std::string tmpl = ctx->GetChatTemplate();
			lua_pushstring(L, tmpl.c_str());
			lua_setfield(L, -2, "chat_template");

			lua_pushinteger(L, ctx->GetCtxOpts().n_gpu_layers);
			lua_setfield(L, -2, "n_gpu_layers");

			lua_pushinteger(L, ctx->GetGpuLayerCount());
			lua_setfield(L, -2, "gpu_layer_count");

			lua_pushinteger(L, ctx->GetCpuLayerCount());
			lua_setfield(L, -2, "cpu_layer_count");

			lua_pushnumber(L, ctx->GetGpuPercent());
			lua_setfield(L, -2, "gpu_percent");

			lua_pushnumber(L, ctx->GetCpuPercent());
			lua_setfield(L, -2, "cpu_percent");

			// capabilities array
			std::vector<std::string> caps;
			ctx->GetCapabilities(caps);
			lua_createtable(L, (int)caps.size(), 0);
			for (int i = 0; i < (int)caps.size(); i++) {
				lua_pushstring(L, caps[i].c_str());
				lua_rawseti(L, -2, i + 1);
			}
			lua_setfield(L, -2, "capabilities");
		}
		lua_setfield(L, -2, "model");
	} else {
		lua_pushnil(L);
		lua_setfield(L, -2, "model");
	}

	return 1;
}

// ctx:Dispose() -> true
int lua_llama_dispose(lua_State* L) {
	LuaLlama* llama = lua_llama_check(L, 1);
	if (llama->messages_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, llama->messages_ref);
		llama->messages_ref = LUA_NOREF;
	}
	if (llama->context) {
		llama->context->Dispose();
	}
	lua_pushboolean(L, 1);
	return 1;
}

// Llama.GetLogs() -> string[]
int lua_llama_getlogs(lua_State* L) {
	std::vector<std::string> logs;
	llama_log_buffer_drain(logs);

	lua_createtable(L, (int)logs.size(), 0);
	for (int i = 0; i < (int)logs.size(); i++) {
		lua_pushlstring(L, logs[i].c_str(), logs[i].size());
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

#else
// -- Stubs when KITSUNE_LLAMA not defined ---------------------------------------

int lua_llama_new(lua_State* L) { return luaL_error(L, "llama not available"); }
int lua_llama_gc(lua_State* L) { return 0; }
int lua_llama_tostring(lua_State* L) { lua_pushliteral(L, "LlamaContext (disabled)"); return 1; }
int lua_llama_setmodel(lua_State* L) { return luaL_error(L, "llama not available"); }
int lua_llama_loadmodel(lua_State* L) { return luaL_error(L, "llama not available"); }
int lua_llama_unloadmodel(lua_State* L) { return luaL_error(L, "llama not available"); }
int lua_llama_ismodelloaded(lua_State* L) { lua_pushboolean(L, 0); return 1; }
int lua_llama_isready(lua_State* L) { lua_pushboolean(L, 0); return 1; }
int lua_llama_generate(lua_State* L) { return luaL_error(L, "llama not available"); }
int lua_llama_poll(lua_State* L) { lua_pushliteral(L, ""); lua_pushliteral(L, ""); lua_pushliteral(L, "done"); return 3; }
int lua_llama_stop(lua_State* L) { lua_pushboolean(L, 1); return 1; }
int lua_llama_reset(lua_State* L) { return luaL_error(L, "llama not available"); }
int lua_llama_embed(lua_State* L) { return luaL_error(L, "llama not available"); }
int lua_llama_info(lua_State* L) { return luaL_error(L, "llama not available"); }
int lua_llama_dispose(lua_State* L) { lua_pushboolean(L, 1); return 1; }
int lua_llama_getlogs(lua_State* L) { lua_newtable(L); return 1; }

#endif
