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
struct llama_chat_message;
class  LlamaPrompt;

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

// ── Data structs ───────────────────────────────────────────────────────────────

struct TokenEntry {
	std::string text;
	bool        is_reasoning   = false;
	bool        is_done        = false;
	bool        is_error       = false;
	bool        is_tool_calls  = false;
};

struct ToolCall {
	std::string id;
	std::string name;
	std::string arguments; // JSON string
};

struct ChatMessage {
	uint32_t                id           = 0;
	std::string             role;
	std::string             content;
	std::string             reasoning;   // stored but never sent to model
	std::string             tool_call_id; // for role="tool" result messages
	std::vector<ToolCall>   tool_calls;  // for role="assistant" with calls
};

// ── ChatTemplate ───────────────────────────────────────────────────────────────
//
// Encapsulates all model-family-specific formatting logic:
//   - Prompt construction (role names, delimiters, BOS token)
//   - Thinking block tags (e.g. <think> / <|channel>thought)
//   - Tool-call tags and injection format
//
// Detected once per generation from GGUF general.architecture metadata.
// Adding support for a new model family only requires touching this struct.

struct ChatTemplate {
	enum class Kind {
		QWEN_HERMES,  // default / fallback — Qwen, Hermes, most open models
		LLAMA3,
		MISTRAL,
		COMMAND_R,
		GEMMA2,       // Gemma 2 / Gemma 3  (arch: "gemma2" / "gemma3")
		GEMMA4,       // Gemma 4            (arch: "gemma4")
	};

	Kind        kind         = Kind::QWEN_HERMES;

	// Thinking-block delimiters (empty = model has no thinking mode)
	std::string think_open;   // e.g. "<think>"  or  "<|channel>thought"
	std::string think_close;  // e.g. "</think>" or  "<channel|>"

	// Tool-call delimiters used natively by this model
	std::string tool_open;    // e.g. "<tool_call>"
	std::string tool_close;   // e.g. "</tool_call>"

	// Detect the right template from the GGUF model's general.architecture
	// metadata key. Falls back to the Jinja template string if the arch is
	// not recognised, and ultimately to QWEN_HERMES.
	static ChatTemplate Detect(const llama_model* mdl);

	// Map an OpenAI-convention role name to this model's role name.
	// e.g. "assistant" -> "model" for Gemma 4.
	std::string MapRole(const std::string& role) const;

	// Build a full prompt string from messages.
	// Falls back to llama_chat_apply_template first; constructs manually only
	// when that returns -1 (i.e. the model's Jinja is not in the built-in list).
	std::string FormatPrompt(
		const llama_model*        mdl,
		const llama_chat_message* chat,
		size_t                    n_msg,
		bool                      add_ass) const;

	// Build a tool-list injection string to prepend to the system message.
	std::string BuildToolInjection(const std::string& tools_json) const;

	// Re-serialize a structured tool-call vector into the native text
	// representation that this model was trained on.
	std::string ReconstructToolCall(const std::vector<ToolCall>& tool_calls) const;
};

// ── Context options ────────────────────────────────────────────────────────────

struct LlamaCtxOpts {
	int     n_gpu_layers  = 99;
	int     n_ctx         = 4096;
	int     n_threads     = 0;
	int     n_batch       = 512;
	bool    flash_attn    = false;
	int64_t model_ttl_ms  = 300000;
	bool    use_mmap      = true;
	bool    use_mlock     = false;
	bool    offload_kqv   = true;   // false = KV cache in RAM (frees VRAM for long context)
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
void        llama_model_get_capabilities(const struct llama_model* mdl, std::vector<std::string>& out);

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
	std::string GetLoadedModelPath() const;
	bool        IsReady() const;
	bool        Generate(std::vector<ChatMessage> messages, LlamaGenOpts&& opts);
	LlamaPrompt* TrimPrompt(const LlamaPrompt& prompt);
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
	uint32_t    GetActualNCtx() const;
	uint32_t    GetActualNBatch() const;
	int32_t     GetActualNThreads() const;
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
	const std::string& GetFullReasoning() const { return full_reasoning; }
	const std::string& GetToolCallJson() const  { return tool_call_json; }
	bool               HasToolCall() const       { return has_tool_call.load(); }
	// Parse tool_call_json into a structured vector for prompt storage.
	std::vector<ToolCall> GetToolCalls() const;

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
	std::string                 loaded_model_path;
	LlamaCtxOpts                ctx_opts;
	int                         model_n_gpu_layers = -1;

	// Generation state
	std::vector<ChatMessage>    gen_messages;
	LlamaGenOpts                gen_opts;
	std::string                 full_content;
	std::string                 full_reasoning;
	std::string                 tool_call_json;
	std::atomic<bool>           has_tool_call{false};
	ChatTemplate                chat_template;

	// Embed state
	std::string                 embed_input;
	std::vector<float>          embed_result;

	// TTL
	int64_t                     model_ttl_ms = 300000;
	std::chrono::steady_clock::time_point last_used_time;
	bool                        last_used_valid = false;

	// Last generation stats
	int                         last_messages_used = 0;

	// KV-cache reuse — store the token sequence decoded in the last generation.
	// On the next call, compare prefix to find where to start decoding from.
	std::vector<int32_t>        last_tokens_;
};
