#pragma once
#ifdef KITSUNE_LLAMA

#include "lua_main_incl.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <chrono>

#include "llama.h"

// ── Type name constants ───────────────────────────────────────────────────────

static const char* LUALLAMA_CTX = "LuaLlamaContext";

// ── Global backend state ──────────────────────────────────────────────────────
// llama_backend_init is called lazily on the first CreateContext.
// llama_backend_free is a no-op in KitsuneCleanup if this is still false.

extern std::atomic<bool> g_llama_backend_initialized;

// ── Log buffer ────────────────────────────────────────────────────────────────

struct LlamaLogBuffer {
    std::mutex               mtx;
    std::vector<std::string> entries;
    static const size_t      max_entries = 500;
};

extern LlamaLogBuffer* g_log_buf;

// ── Enums ─────────────────────────────────────────────────────────────────────

enum class LlamaTask : int {
    Idle     = 0,
    Load     = 1,
    Generate = 2,
    Unload   = 3,
    ResetKV  = 4,
    Shutdown = 5,
    Embed    = 6,
};

enum class LlamaStatus : int {
    Idle      = 0,
    Loading   = 1,
    Generating = 2,
    Unloading = 3,
    Error     = 4,
};

// ── TokenEntry ────────────────────────────────────────────────────────────────
// One decoded UTF-8 token pushed by the worker into the token queue.

struct TokenEntry {
    std::string text;
    bool        is_reasoning;
};

// ── Generation options ────────────────────────────────────────────────────────
// Copied from Lua opts table into the struct before posting a GENERATE task.

struct LlamaGenOpts {
    float    temperature;
    float    top_p;
    int      top_k;
    float    min_p;
    int      seed;
    int      max_tokens;
    std::string tools_json; // serialized OpenAI-format tools array, empty if none
};

// ── Context options ───────────────────────────────────────────────────────────
// Set once at CreateContext time; shared params for ctx + model creation.

struct LlamaCtxOpts {
    int  n_gpu_layers;
    int  n_ctx;
    int  n_threads;
    int  n_batch;
    bool flash_attn;
};

// ── Chat message ──────────────────────────────────────────────────────────────

struct ChatMessage {
    std::string role;
    std::string content;
};

// ── LuaLlamaContext ───────────────────────────────────────────────────────────
// The single userdata type exposed to Lua.
// All llama.cpp API calls happen exclusively on the worker thread.

struct LuaLlamaContext {
    // ── Worker thread ─────────────────────────────────────────────────────────
    std::thread              worker;
    std::mutex               task_mtx;
    std::condition_variable  task_cv;
    LlamaTask                current_task;   // guarded by task_mtx

    // ── Token output (Lua thread reads via Poll) ──────────────────────────────
    std::mutex               token_mtx;
    std::vector<TokenEntry>  token_queue;    // worker pushes; Poll() drains

    // ── Atomic status (safe to read from Lua thread without lock) ─────────────
    std::atomic<LlamaStatus> status;
    std::atomic<bool>        stop_flag;
    std::atomic<bool>        model_loaded;
    std::atomic<bool>        has_tool_call;
    std::atomic<bool>        disposed;
    std::atomic<bool>        generation_done; // flipped to true once on transition to idle after generate

    // ── Error state (written by worker, read by Poll/Info) ────────────────────
    // Protected by token_mtx (reuse the same lock to avoid a third mutex).
    std::string              error;

    // ── llama.cpp objects (worker-owned, never touched by Lua thread) ─────────
    llama_model*             model;
    llama_context*           ctx;

    // ── Model configuration (written by SetModel / CreateContext) ─────────────
    // Read by worker on LOAD task. Safe: worker reads only after task is posted
    // and Lua thread does not modify after posting.
    std::string              model_path;
    LlamaCtxOpts             ctx_opts;

    // ── Generate inputs (written by Generate, read by worker on GENERATE task) ─
    std::vector<ChatMessage> gen_messages;
    LlamaGenOpts             gen_opts;

    // ── Accumulated output (worker-owned, cleared on ResetKV) ─────────────────
    std::string              full_content;
    std::string              full_reasoning;
    std::string              tool_call_json;

    // ── Embedding I/O (written by Lua thread before Embed task, result written by worker) ─
    std::string              embed_input;
    std::vector<float>       embed_result;  // protected by token_mtx

    // ── TTL / keep-alive ──────────────────────────────────────────────────────
    int64_t                                        model_ttl_ms;   // 0 = never
    std::chrono::steady_clock::time_point          last_used_time;
};

// ── Helpers ───────────────────────────────────────────────────────────────────

LuaLlamaContext* lua_tollama(lua_State* L, int index);
LuaLlamaContext* lua_pushllamacontext(lua_State* L);

// ── Worker entry point ────────────────────────────────────────────────────────

void llama_worker(LuaLlamaContext* c);

// ── Lua-callable functions ────────────────────────────────────────────────────

int LlamaCreateContext(lua_State* L);
int LlamaGetLogs(lua_State* L);
int LlamaSetModel(lua_State* L);
int LlamaLoadModel(lua_State* L);
int LlamaUnloadModel(lua_State* L);
int LlamaIsModelLoaded(lua_State* L);
int LlamaIsReady(lua_State* L);
int LlamaInfo(lua_State* L);
int LlamaGenerate(lua_State* L);
int LlamaEmbed(lua_State* L);
int LlamaPoll(lua_State* L);
int LlamaStop(lua_State* L);
int LlamaReset(lua_State* L);
int LlamaDispose(lua_State* L);

int llama_ctx_gc(lua_State* L);
int llama_ctx_tostring(lua_State* L);

#endif // KITSUNE_LLAMA
