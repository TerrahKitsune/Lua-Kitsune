#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstdint>

struct llama_model;
struct llama_context;

// ── Enums ──────────────────────────────────────────────────────────────────────

enum class LlamaTask {
	IDLE,
	LOAD,
	GENERATE,
	UNLOAD,
	RESET_KV,
	EMBED,
	SHUTDOWN
};

enum class LlamaStatus {
	IDLE,
	LOADING,
	GENERATING,
	UNLOADING,
	FAILED
};

enum class ToolFamily {
	QWEN_HERMES,
	LLAMA3,
	MISTRAL,
	COMMAND_R
};

// ── Data structs ───────────────────────────────────────────────────────────────

struct TokenEntry {
	std::string text;
	bool        is_reasoning   = false;
	bool        is_done        = false;
	bool        is_error       = false;
	bool        is_tool_calls  = false;
};

struct ChatMessage {
	std::string role;
	std::string content;
	std::string tool_calls_json;
	std::string tool_call_id;
};

struct LlamaCtxOpts {
	int     n_gpu_layers  = 99;
	int     n_ctx         = 4096;
	int     n_threads     = 0;
	int     n_batch       = 512;
	bool    flash_attn    = false;
	int64_t model_ttl_ms  = 300000;
};

struct LlamaGenOpts {
	float       temperature = 0.8f;
	float       top_p       = 0.95f;
	int         top_k       = 40;
	float       min_p       = 0.05f;
	int         seed        = -1;
	int         max_tokens  = 2048;
	std::string tools_json;
};

// ── Process-wide llama.cpp log buffer ──────────────────────────────────────────

void        llama_log_buffer_drain(std::vector<std::string>& out);

// ── Process-wide backend init ──────────────────────────────────────────────────

void        llama_backend_init_once();
void        llama_backend_cleanup();

// ── LlamaContext ───────────────────────────────────────────────────────────────

class LlamaContext {
public:
	explicit LlamaContext(const LlamaCtxOpts& opts);
	~LlamaContext();

	LlamaContext(const LlamaContext&) = delete;
	LlamaContext& operator=(const LlamaContext&) = delete;

	// ── Lua-thread API (called from the main / Lua thread) ─────────────────

	bool        SetModel(const std::string& path, int n_gpu_layers_override);
	bool        LoadModel();
	bool        UnloadModel();
	bool        IsModelLoaded() const;
	bool        IsReady() const;
	bool        Generate(std::vector<ChatMessage>&& messages, LlamaGenOpts&& opts);
	bool        Stop();
	bool        Reset();
	bool        Embed(const std::string& text);
	void        Dispose();

	// ── Poll (called from Lua thread, non-blocking) ────────────────────────

	// Drains next batch of same-type tokens.
	// Returns false when generation is done and queue fully drained.
	// out_text / out_type are set when data is available.
	bool        Poll(std::string& out_text, std::string& out_type);

	// ── Info (called from Lua thread, non-blocking) ────────────────────────

	LlamaStatus GetStatus() const;
	std::string GetError() const;
	std::string GetModelPath() const;
	bool        IsDisposed() const;

	// Context info
	const LlamaCtxOpts& GetCtxOpts() const;
	int         GetTokensUsed() const;
	int         GetTokensAvailable() const;
	double      GetSecondsSinceLastUsed() const;
	int         GetLastMessagesUsed() const;

	// Model info (only valid when model loaded)
	std::string GetModelDesc() const;
	std::string GetModelArch() const;
	int         GetModelContextLength() const;
	int64_t     GetModelParamCount() const;
	int         GetModelEmbeddingDim() const;
	int         GetModelLayerCount() const;
	int64_t     GetModelSizeBytes() const;
	std::string GetChatTemplate() const;
	int         GetGpuLayerCount() const;
	int         GetCpuLayerCount() const;
	double      GetGpuPercent() const;
	double      GetCpuPercent() const;
	void        GetCapabilities(std::vector<std::string>& out) const;

	// Generation result accessors (valid after Poll returns ok=false)
	const std::string& GetFullContent() const   { return full_content; }
	const std::string& GetToolCallJson() const  { return tool_call_json; }
	bool               HasToolCall() const       { return has_tool_call.load(); }

	// Embed result (valid after EMBED task completes)
	const std::vector<float>& GetEmbedResult() const;

private:
	// ── Worker thread ──────────────────────────────────────────────────────

	void        WorkerMain();
	void        WorkerLoad();
	void        WorkerUnload();
	void        WorkerGenerate();
	void        WorkerResetKV();
	void        WorkerEmbed();

	void        PushToken(const std::string& text, bool is_reasoning);
	void        PushDone();
	void        PushError(const std::string& msg);

	// ── Tool system helpers ────────────────────────────────────────────────

	ToolFamily  DetectToolFamily() const;
	std::string ReconstructToolCallContent(const std::string& tool_calls_json) const;
	std::string BuildToolInjection(const std::string& tools_json) const;
	bool        DetectToolCalls(const std::string& content);

	// ── Fields ─────────────────────────────────────────────────────────────

	// Worker
	std::thread                 worker;
	std::mutex                  task_mtx;
	std::condition_variable     task_cv;
	LlamaTask                   current_task = LlamaTask::IDLE;

	// Token queue
	std::mutex                  token_mtx;
	std::vector<TokenEntry>     token_queue;

	// Atomics
	std::atomic<LlamaStatus>    status{LlamaStatus::IDLE};
	std::atomic<bool>           stop_flag{false};
	std::atomic<bool>           model_loaded{false};
	std::atomic<bool>           generation_done{false};
	std::atomic<bool>           disposed{false};
	std::atomic<bool>           task_complete{false};

	// Error
	std::string                 error;
	std::mutex                  error_mtx;

	// llama.cpp handles
	llama_model*                llama_mdl  = nullptr;
	llama_context*              llama_ctx  = nullptr;

	// Model path and options
	std::string                 model_path;
	LlamaCtxOpts                ctx_opts;
	int                         model_n_gpu_layers = -1;

	// Generation state
	std::vector<ChatMessage>    gen_messages;
	LlamaGenOpts                gen_opts;
	std::string                 full_content;
	std::string                 full_reasoning;
	std::string                 tool_call_json;
	std::atomic<bool>           has_tool_call{false};
	ToolFamily                  tool_family = ToolFamily::QWEN_HERMES;

	// Embed state
	std::string                 embed_input;
	std::vector<float>          embed_result;

	// TTL
	int64_t                     model_ttl_ms = 300000;
	std::chrono::steady_clock::time_point last_used_time;
	bool                        last_used_valid = false;

	// Last generation stats
	int                         last_messages_used = 0;
};
