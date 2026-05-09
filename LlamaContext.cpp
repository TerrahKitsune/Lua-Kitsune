#include "platform.h"
#include "LlamaContext.h"
#include <cstdlib>
#include <cstdio>

#ifdef KITSUNE_LLAMA

#include "llama.h"
#include "ggml.h"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <regex>
#include <functional>

// -- Process-wide log buffer ----------------------------------------------------

static constexpr size_t LOG_BUF_MAX = 500;

static std::mutex*               g_log_mtx = nullptr;
static std::vector<std::string>* g_log_buf = nullptr;

static void llama_log_callback(enum ggml_log_level level, const char* text, void* user_data) {
    (void)level;
    (void)user_data;
    if (!text || !g_log_mtx || !g_log_buf)
        return;
    std::lock_guard<std::mutex> lock(*g_log_mtx);
    if (g_log_buf->size() >= LOG_BUF_MAX)
        g_log_buf->erase(g_log_buf->begin());
    g_log_buf->emplace_back(text);
}

void llama_log_buffer_drain(std::vector<std::string>& out) {
    if (!g_log_mtx || !g_log_buf)
        return;
    std::lock_guard<std::mutex> lock(*g_log_mtx);
    out = std::move(*g_log_buf);
    g_log_buf->clear();
}

// -- Process-wide backend init --------------------------------------------------

static std::atomic<bool> g_llama_backend_initialized{false};

void llama_backend_init_once() {
    bool expected = false;
    if (g_llama_backend_initialized.compare_exchange_strong(expected, true)) {
        g_log_mtx = new std::mutex();
        g_log_buf = new std::vector<std::string>();
        llama_backend_init();
        llama_log_set(llama_log_callback, nullptr);
    }
}

void llama_backend_cleanup() {
    bool expected = true;
    if (g_llama_backend_initialized.compare_exchange_strong(expected, false)) {
        llama_log_set(nullptr, nullptr);
        delete g_log_buf;
        g_log_buf = nullptr;
        delete g_log_mtx;
        g_log_mtx = nullptr;
    }
}

// -- Constructor / Destructor ---------------------------------------------------

LlamaContext::LlamaContext(const LlamaCtxOpts& opts)
    : ctx_opts(opts)
    , model_ttl_ms(opts.model_ttl_ms)
{
    llama_backend_init_once();
    worker = std::thread(&LlamaContext::WorkerMain, this);
}

LlamaContext::~LlamaContext() {
    Dispose();
}

// -- Lua-thread API -------------------------------------------------------------

bool LlamaContext::SetModel(const std::string& path, int n_gpu_layers_override) {
    if (disposed.load())
        return false;
    LlamaStatus s = status.load();
    if (s == LlamaStatus::LOADING || s == LlamaStatus::GENERATING)
        return false;
    model_path = path;
    model_n_gpu_layers = n_gpu_layers_override;
    return true;
}

bool LlamaContext::LoadModel() {
    if (disposed.load())
        return false;
    if (model_loaded.load())
        return true;
    LlamaStatus s = status.load();
    if (s == LlamaStatus::GENERATING)
        return false;
    {
        std::lock_guard<std::mutex> lock(task_mtx);
        task_complete.store(false);
        current_task = LlamaTask::LOAD;
    }
    task_cv.notify_one();
    // Block cooperatively until done
    while (!task_complete.load() && !disposed.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return status.load() != LlamaStatus::FAILED;
}

bool LlamaContext::UnloadModel() {
    if (disposed.load())
        return false;
    LlamaStatus s = status.load();
    if (s == LlamaStatus::GENERATING || s == LlamaStatus::LOADING)
        return false;
    {
        std::lock_guard<std::mutex> lock(task_mtx);
        current_task = LlamaTask::UNLOAD;
    }
    task_cv.notify_one();
    return true;
}

bool LlamaContext::IsModelLoaded() const {
    return model_loaded.load();
}

bool LlamaContext::IsReady() const {
    return status.load() == LlamaStatus::IDLE && !model_path.empty();
}

bool LlamaContext::Generate(std::vector<ChatMessage>&& messages, LlamaGenOpts&& opts) {
    if (disposed.load())
        return false;
    if (status.load() != LlamaStatus::IDLE && status.load() != LlamaStatus::FAILED)
        return false;
    if (model_path.empty())
        return false;
    if (messages.empty())
        return false;

    gen_messages = std::move(messages);
    gen_opts = std::move(opts);
    stop_flag.store(false);
    generation_done.store(false);
    has_tool_call.store(false);
    full_content.clear();
    full_reasoning.clear();
    tool_call_json.clear();

    {
        std::lock_guard<std::mutex> lock(token_mtx);
        token_queue.clear();
    }

    {
        std::lock_guard<std::mutex> lock(task_mtx);
        current_task = LlamaTask::GENERATE;
    }
    task_cv.notify_one();
    return true;
}

bool LlamaContext::Stop() {
    stop_flag.store(true);
    return true;
}

bool LlamaContext::Reset() {
    if (disposed.load())
        return false;
    LlamaStatus s = status.load();
    if (s == LlamaStatus::GENERATING)
        return false;
    {
        std::lock_guard<std::mutex> lock(task_mtx);
        current_task = LlamaTask::RESET_KV;
    }
    task_cv.notify_one();
    return true;
}

bool LlamaContext::Embed(const std::string& text) {
    if (disposed.load())
        return false;
    if (status.load() != LlamaStatus::IDLE)
        return false;
    if (model_path.empty())
        return false;

    embed_input = text;
    embed_result.clear();

    {
        std::lock_guard<std::mutex> lock(task_mtx);
        task_complete.store(false);
        current_task = LlamaTask::EMBED;
    }
    task_cv.notify_one();
    while (!task_complete.load() && !disposed.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return status.load() != LlamaStatus::FAILED;
}

void LlamaContext::Dispose() {
    bool expected = false;
    if (!disposed.compare_exchange_strong(expected, true))
        return;
    stop_flag.store(true);
    {
        std::lock_guard<std::mutex> lock(task_mtx);
        current_task = LlamaTask::SHUTDOWN;
    }
    task_cv.notify_one();
    if (worker.joinable())
        worker.join();
}

// -- Poll -----------------------------------------------------------------------

bool LlamaContext::Poll(std::string& out_text, std::string& out_type) {
    out_text.clear();
    out_type.clear();

    std::lock_guard<std::mutex> lock(token_mtx);
    if (token_queue.empty()) {
        if (generation_done.load())
            return false;
        return true;
    }

    TokenEntry& front = token_queue.front();
    if (front.is_done) {
        token_queue.erase(token_queue.begin());
        generation_done.store(true);
        return false;
    }
    if (front.is_error) {
        out_text = std::move(front.text);
        out_type = "error";
        token_queue.erase(token_queue.begin());
        return false;
    }
    if (front.is_tool_calls) {
        out_text = std::move(front.text);
        out_type = "tool_calls";
        token_queue.erase(token_queue.begin());
        return true;
    }

    // Drain a batch of same-type tokens
    bool reasoning = front.is_reasoning;
    std::string batch;
    while (!token_queue.empty()) {
        TokenEntry& t = token_queue.front();
        if (t.is_done || t.is_error || t.is_tool_calls || t.is_reasoning != reasoning)
            break;
        batch += t.text;
        token_queue.erase(token_queue.begin());
    }

    out_text = std::move(batch);
    out_type = reasoning ? "reasoning" : "token";
    return true;
}

// -- Info getters ---------------------------------------------------------------

LlamaStatus LlamaContext::GetStatus() const {
    return status.load();
}

std::string LlamaContext::GetError() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(error_mtx));
    return error;
}

std::string LlamaContext::GetModelPath() const {
    return model_path;
}

bool LlamaContext::IsDisposed() const {
    return disposed.load();
}

const LlamaCtxOpts& LlamaContext::GetCtxOpts() const {
    return ctx_opts;
}

int LlamaContext::GetTokensUsed() const {
    if (!llama_ctx)
        return 0;
    auto mem = llama_get_memory(llama_ctx);
    if (!mem)
        return 0;
    llama_pos p = llama_memory_seq_pos_max(mem, 0);
    return (p >= 0) ? (int)(p + 1) : 0;
}

int LlamaContext::GetTokensAvailable() const {
    if (!llama_ctx)
        return 0;
    return ctx_opts.n_ctx - GetTokensUsed();
}

double LlamaContext::GetSecondsSinceLastUsed() const {
    if (!last_used_valid)
        return -1.0;
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - last_used_time).count();
}

std::string LlamaContext::GetModelDesc() const {
    if (!llama_mdl)
        return "";
    char buf[256] = {0};
    llama_model_desc(llama_mdl, buf, sizeof(buf));
    return buf;
}

std::string LlamaContext::GetModelArch() const {
    if (!llama_mdl)
        return "";
    char buf[64] = {0};
    int n = llama_model_meta_val_str(llama_mdl, "general.architecture", buf, sizeof(buf));
    if (n > 0)
        return std::string(buf, n);
    return "";
}

int LlamaContext::GetModelContextLength() const {
    if (!llama_mdl)
        return 0;
    return (int)llama_model_n_ctx_train(llama_mdl);
}

int64_t LlamaContext::GetModelParamCount() const {
    if (!llama_mdl)
        return 0;
    return (int64_t)llama_model_n_params(llama_mdl);
}

int LlamaContext::GetModelEmbeddingDim() const {
    if (!llama_mdl)
        return 0;
    return llama_model_n_embd(llama_mdl);
}

int LlamaContext::GetModelLayerCount() const {
    if (!llama_mdl)
        return 0;
    return (int)llama_model_n_layer(llama_mdl);
}

int64_t LlamaContext::GetModelSizeBytes() const {
    if (!llama_mdl)
        return 0;
    return (int64_t)llama_model_size(llama_mdl);
}

std::string LlamaContext::GetChatTemplate() const {
    if (!llama_mdl)
        return "";
    auto* tmpl = llama_model_chat_template(llama_mdl, nullptr);
    return tmpl ? tmpl : "";
}

int LlamaContext::GetGpuLayerCount() const {
    if (!llama_mdl)
        return 0;
    int total = GetModelLayerCount() + 1;
    int requested = (model_n_gpu_layers >= 0) ? model_n_gpu_layers : ctx_opts.n_gpu_layers;
    return (std::min)(requested, total);
}

int LlamaContext::GetCpuLayerCount() const {
    if (!llama_mdl)
        return 0;
    int total = GetModelLayerCount() + 1;
    return total - GetGpuLayerCount();
}

double LlamaContext::GetGpuPercent() const {
    if (!llama_mdl)
        return 0.0;
    int total = GetModelLayerCount() + 1;
    if (total <= 0)
        return 0.0;
    return (GetGpuLayerCount() * 100.0) / total;
}

double LlamaContext::GetCpuPercent() const {
    return 100.0 - GetGpuPercent();
}

void LlamaContext::GetCapabilities(std::vector<std::string>& out) const {
    out.clear();
    if (!llama_mdl)
        return;

    // Completion: has a decoder
    out.push_back("completion");

    // Tools: check chat template for tool markers
    std::string tmpl = GetChatTemplate();
    std::string tmpl_lower;
    tmpl_lower.resize(tmpl.size());
    std::transform(tmpl.begin(), tmpl.end(), tmpl_lower.begin(), ::tolower);

    if (tmpl_lower.find("tool") != std::string::npos ||
        tmpl_lower.find("function") != std::string::npos ||
        tmpl_lower.find("<|python_tag|>") != std::string::npos) {
        out.push_back("tools");
    }

    // Reasoning: check for think block support
    if (tmpl_lower.find("<think>") != std::string::npos ||
        tmpl_lower.find("think") != std::string::npos) {
        out.push_back("reasoning");
    }

    // Vision: check architecture for multimodal
    std::string arch = GetModelArch();
    std::string arch_lower;
    arch_lower.resize(arch.size());
    std::transform(arch.begin(), arch.end(), arch_lower.begin(), ::tolower);
    if (arch_lower.find("mllama") != std::string::npos ||
        arch_lower.find("llava") != std::string::npos ||
        arch_lower.find("minicpm") != std::string::npos) {
        out.push_back("vision");
    }

    // Embedding: check architecture
    if (arch_lower.find("bert") != std::string::npos ||
        arch_lower.find("bge") != std::string::npos ||
        arch_lower.find("nomic") != std::string::npos) {
        out.push_back("embedding");
    }
}

const std::vector<float>& LlamaContext::GetEmbedResult() const {
    return embed_result;
}

// -- Token queue helpers --------------------------------------------------------

void LlamaContext::PushToken(const std::string& text, bool is_reasoning) {
    std::lock_guard<std::mutex> lock(token_mtx);
    TokenEntry entry;
    entry.text = text;
    entry.is_reasoning = is_reasoning;
    token_queue.push_back(std::move(entry));
}

void LlamaContext::PushDone() {
    std::lock_guard<std::mutex> lock(token_mtx);
    TokenEntry entry;
    entry.is_done = true;
    token_queue.push_back(std::move(entry));
}

void LlamaContext::PushError(const std::string& msg) {
    {
        std::lock_guard<std::mutex> lock(error_mtx);
        error = msg;
    }
    std::lock_guard<std::mutex> lock(token_mtx);
    TokenEntry entry;
    entry.text = msg;
    entry.is_error = true;
    token_queue.push_back(std::move(entry));
}

// -- Worker thread --------------------------------------------------------------

void LlamaContext::WorkerMain() {
    while (true) {
        LlamaTask task;
        {
            std::unique_lock<std::mutex> lock(task_mtx);
            task_cv.wait_for(lock, std::chrono::milliseconds(500), [this] {
                return current_task != LlamaTask::IDLE;
            });
            task = current_task;
            current_task = LlamaTask::IDLE;
        }

        if (task == LlamaTask::SHUTDOWN)
            break;

        // TTL auto-unload check
        if (task == LlamaTask::IDLE && model_loaded.load() && model_ttl_ms > 0 && last_used_valid) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - last_used_time).count();
            if (elapsed >= model_ttl_ms) {
                WorkerUnload();
            }
            continue;
        }

        switch (task) {
        case LlamaTask::LOAD:
            WorkerLoad();
            task_complete.store(true);
            break;
        case LlamaTask::GENERATE:
            WorkerGenerate();
            break;
        case LlamaTask::UNLOAD:
            WorkerUnload();
            break;
        case LlamaTask::RESET_KV:
            WorkerResetKV();
            break;
        case LlamaTask::EMBED:
            WorkerEmbed();
            task_complete.store(true);
            break;
        case LlamaTask::IDLE:
        case LlamaTask::SHUTDOWN:
            break;
        }
    }

    // Cleanup on shutdown
    if (llama_ctx) {
        llama_free(llama_ctx);
        llama_ctx = nullptr;
    }
    if (llama_mdl) {
        llama_model_free(llama_mdl);
        llama_mdl = nullptr;
    }
    model_loaded.store(false);
}

void LlamaContext::WorkerLoad() {
    if (model_loaded.load())
        return;
    if (model_path.empty()) {
        PushError("no model path set");
        status.store(LlamaStatus::FAILED);
        return;
    }

    status.store(LlamaStatus::LOADING);

    auto mparams = llama_model_default_params();
    int gpu_layers = (model_n_gpu_layers >= 0) ? model_n_gpu_layers : ctx_opts.n_gpu_layers;
    mparams.n_gpu_layers = gpu_layers;

    llama_mdl = llama_model_load_from_file(model_path.c_str(), mparams);
    if (!llama_mdl) {
        PushError("failed to load model: " + model_path);
        status.store(LlamaStatus::FAILED);
        return;
    }

    auto cparams = llama_context_default_params();
    cparams.n_ctx = ctx_opts.n_ctx;
    cparams.n_batch = ctx_opts.n_batch;
    cparams.n_threads = (ctx_opts.n_threads > 0) ? ctx_opts.n_threads : std::thread::hardware_concurrency();
    cparams.n_threads_batch = cparams.n_threads;
    cparams.flash_attn_type = ctx_opts.flash_attn ? LLAMA_FLASH_ATTN_TYPE_ENABLED : LLAMA_FLASH_ATTN_TYPE_DISABLED;

    llama_ctx = llama_init_from_model(llama_mdl, cparams);
    if (!llama_ctx) {
        llama_model_free(llama_mdl);
        llama_mdl = nullptr;
        PushError("failed to create llama context");
        status.store(LlamaStatus::FAILED);        status.store(LlamaStatus::FAILED);
        return;
    }

    tool_family = DetectToolFamily();
    model_loaded.store(true);
    {
        std::lock_guard<std::mutex> lock(error_mtx);
        error.clear();
    }
    status.store(LlamaStatus::IDLE);
}

void LlamaContext::WorkerUnload() {
    status.store(LlamaStatus::UNLOADING);
    if (llama_ctx) {
        llama_free(llama_ctx);
        llama_ctx = nullptr;
    }
    if (llama_mdl) {
        llama_model_free(llama_mdl);
        llama_mdl = nullptr;
    }
    model_loaded.store(false);
    status.store(LlamaStatus::IDLE);
}

void LlamaContext::WorkerResetKV() {
    if (llama_ctx)
        llama_memory_clear(llama_get_memory(llama_ctx), true);
    {
        std::lock_guard<std::mutex> lock(token_mtx);
        token_queue.clear();
    }
    full_content.clear();
    full_reasoning.clear();
    tool_call_json.clear();
    has_tool_call.store(false);
    {
        std::lock_guard<std::mutex> lock(error_mtx);
        error.clear();
    }
}

void LlamaContext::WorkerGenerate() {
    status.store(LlamaStatus::GENERATING);

    // Auto-load if needed
    if (!model_loaded.load()) {
        WorkerLoad();
        if (!model_loaded.load()) {
            PushError("model failed to load");
            PushDone();
            status.store(LlamaStatus::FAILED);
            return;
        }
    }

    full_content.clear();
    full_reasoning.clear();
    tool_call_json.clear();
    has_tool_call.store(false);

    // Build prompt via chat template
    std::vector<llama_chat_message> chat_msgs;
    chat_msgs.reserve(gen_messages.size());

    // Check if we need tool injection
    bool has_tools = !gen_opts.tools_json.empty();
    std::string tool_injection;
    if (has_tools)
        tool_injection = BuildToolInjection(gen_opts.tools_json);

    for (auto& msg : gen_messages) {
        llama_chat_message cm;
        std::string content = msg.content;

        // Reconstruct tool-call assistant turns
        if (!msg.tool_calls_json.empty())
            content = ReconstructToolCallContent(msg.tool_calls_json);

        // Inject tools into system message
        if (has_tools && msg.role == "system" && !tool_injection.empty()) {
            content += "\n\n" + tool_injection;
            tool_injection.clear();
        }

        cm.role = msg.role.c_str();
        // Store content in the message to keep pointer alive
        msg.content = std::move(content);
        cm.content = msg.content.c_str();
        chat_msgs.push_back(cm);
    }

    // If no system message existed but we have tool injection, prepend one
    if (!tool_injection.empty()) {
        ChatMessage sys_msg;
        sys_msg.role = "system";
        sys_msg.content = tool_injection;
        gen_messages.insert(gen_messages.begin(), std::move(sys_msg));

        // Rebuild chat_msgs
        chat_msgs.clear();
        chat_msgs.reserve(gen_messages.size());
        for (auto& msg : gen_messages) {
            llama_chat_message cm;
            cm.role = msg.role.c_str();
            cm.content = msg.content.c_str();
            chat_msgs.push_back(cm);
        }
    }

    // Apply template
    const char* chat_tmpl = llama_model_chat_template(llama_mdl, nullptr);
    std::vector<char> prompt_buf(4096);
    int32_t prompt_len = llama_chat_apply_template(
        chat_tmpl,
        chat_msgs.data(), chat_msgs.size(),
        true,
        prompt_buf.data(), (int32_t)prompt_buf.size()
    );

    if (prompt_len < 0) {
        PushError("failed to apply chat template");
        PushDone();
        status.store(LlamaStatus::IDLE);
        return;
    }

    if ((size_t)prompt_len >= prompt_buf.size()) {
        prompt_buf.resize(prompt_len + 1);
        prompt_len = llama_chat_apply_template(
            chat_tmpl,
            chat_msgs.data(), chat_msgs.size(),
            true,
            prompt_buf.data(), (int32_t)prompt_buf.size()
        );
    }

    std::string prompt(prompt_buf.data(), prompt_len);

    // Tokenise
    int n_prompt_max = prompt_len + 256;
    std::vector<llama_token> tokens(n_prompt_max);
    int n_tokens = llama_tokenize(llama_model_get_vocab(llama_mdl), prompt.c_str(), (int32_t)prompt.size(),
        tokens.data(), n_prompt_max, true, true);
    if (n_tokens < 0) {
        PushError("tokenization failed");
        PushDone();
        status.store(LlamaStatus::IDLE);
        return;
    }
    tokens.resize(n_tokens);

    // Clear KV cache for fresh generation
    llama_memory_clear(llama_get_memory(llama_ctx), true);

    // Create sampler chain
    auto* smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(gen_opts.min_p, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(gen_opts.top_k));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(gen_opts.top_p, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(gen_opts.temperature));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(gen_opts.seed >= 0 ? (uint32_t)gen_opts.seed : LLAMA_DEFAULT_SEED));

    // Decode prompt batch
    llama_batch batch = llama_batch_get_one(tokens.data(), n_tokens);
    if (llama_decode(llama_ctx, batch) != 0) {
        llama_sampler_free(smpl);
        PushError("prompt decode failed");
        PushDone();
        status.store(LlamaStatus::IDLE);
        return;
    }

    // Generation loop
    bool in_think = false;
    bool in_tool_call = false;
    bool skip_leading_newline = false;
    std::string pending;
    const size_t OPEN_TAG_LEN       = 7;  // "<think>"
    const size_t CLOSE_TAG_LEN      = 8;  // "</think>"
    const size_t TOOL_OPEN_TAG_LEN  = 11; // "<tool_call>"
    const size_t TOOL_CLOSE_TAG_LEN = 12; // "</tool_call>"
    int generated = 0;
    int max_tokens = gen_opts.max_tokens;

    while (generated < max_tokens && !stop_flag.load()) {
        llama_token new_token = llama_sampler_sample(smpl, llama_ctx, -1);

        if (llama_vocab_is_eog(llama_model_get_vocab(llama_mdl), new_token))
            break;

        // Convert token to text
        char buf[256];
        int n = llama_token_to_piece(llama_model_get_vocab(llama_mdl), new_token, buf, sizeof(buf), 0, true);
        if (n < 0)
            n = 0;
        std::string piece(buf, n);

        // Track <think>...</think> blocks using a pending buffer so that
        // tags split across token boundaries are handled correctly.
        pending += piece;

        // If the previous token ended a tag, eat leading newlines of this token
        if (skip_leading_newline) {
            while (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n'))
                pending = pending.substr(1);
            skip_leading_newline = pending.empty();
        }

        while (!pending.empty()) {
            if (in_tool_call) {
                // Suppress all tokens until </tool_call>; full_content accumulates
                // the raw text so DetectToolCalls can parse it after generation ends.
                size_t tag_pos = pending.find("</tool_call>");
                if (tag_pos != std::string::npos) {
                    full_content += pending.substr(0, tag_pos + TOOL_CLOSE_TAG_LEN);
                    pending = pending.substr(tag_pos + TOOL_CLOSE_TAG_LEN);
                    if (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n')) {
                        while (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n'))
                            pending = pending.substr(1);
                    } else {
                        skip_leading_newline = true;
                    }
                    in_tool_call = false;
                } else {
                    // Keep a partial-tag suffix buffered; accumulate the safe part silently
                    size_t safe = pending.size() >= (TOOL_CLOSE_TAG_LEN - 1)
                                  ? pending.size() - (TOOL_CLOSE_TAG_LEN - 1)
                                  : 0;
                    if (safe > 0) {
                        full_content += pending.substr(0, safe);
                        pending = pending.substr(safe);
                    }
                    break;
                }
            } else if (!in_think) {
                // Check for <tool_call> first so it takes priority over content flushing
                size_t tool_pos  = pending.find("<tool_call>");
                size_t think_pos = pending.find("<think>");
                size_t first_tag = std::string::npos;
                bool   is_tool   = false;
                if (tool_pos  != std::string::npos) { first_tag = tool_pos;  is_tool = true; }
                if (think_pos != std::string::npos && think_pos < first_tag) { first_tag = think_pos; is_tool = false; }

                if (first_tag != std::string::npos) {
                    // Flush content before whichever tag comes first
                    if (first_tag > 0) {
                        std::string before = pending.substr(0, first_tag);
                        full_content += before;
                        PushToken(before, false);
                    }
                    if (is_tool) {
                        full_content += "<tool_call>";
                        pending = pending.substr(first_tag + TOOL_OPEN_TAG_LEN);
                        if (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n')) {
                            while (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n'))
                                pending = pending.substr(1);
                        } else {
                            skip_leading_newline = true;
                        }
                        in_tool_call = true;
                    } else {
                        pending = pending.substr(first_tag + OPEN_TAG_LEN);
                        if (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n')) {
                            while (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n'))
                                pending = pending.substr(1);
                        } else {
                            skip_leading_newline = true;
                        }
                        in_think = true;
                    }
                } else {
                    // No full open tag found — keep a partial-tag suffix buffered
                    size_t safe_think = pending.size() >= (OPEN_TAG_LEN - 1)
                                        ? pending.size() - (OPEN_TAG_LEN - 1)
                                        : 0;
                    size_t safe_tool  = pending.size() >= (TOOL_OPEN_TAG_LEN - 1)
                                        ? pending.size() - (TOOL_OPEN_TAG_LEN - 1)
                                        : 0;
                    size_t safe = safe_tool < safe_think ? safe_tool : safe_think;
                    if (safe > 0) {
                        std::string flush = pending.substr(0, safe);
                        full_content += flush;
                        PushToken(flush, false);
                        pending = pending.substr(safe);
                    }
                    break;
                }
            } else {
                size_t tag_pos = pending.find("</think>");
                if (tag_pos != std::string::npos) {
                    // Flush reasoning content before the close tag, stripping trailing newlines
                    if (tag_pos > 0) {
                        std::string before = pending.substr(0, tag_pos);
                        while (!before.empty() && (before.back() == '\r' || before.back() == '\n'))
                            before.pop_back();
                        if (!before.empty()) {
                            full_reasoning += before;
                            PushToken(before, true);
                        }
                    }
                    pending = pending.substr(tag_pos + CLOSE_TAG_LEN);
                    // Strip all leading newlines; if none present yet, eat them from the next token
                    if (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n')) {
                        while (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n'))
                            pending = pending.substr(1);
                    } else {
                        skip_leading_newline = true;
                    }
                    in_think = false;
                } else {
                    // No full close tag found — keep a partial-tag suffix buffered
                    size_t safe = pending.size() >= (CLOSE_TAG_LEN - 1)
                                  ? pending.size() - (CLOSE_TAG_LEN - 1)
                                  : 0;
                    if (safe > 0) {
                        std::string flush = pending.substr(0, safe);
                        full_reasoning += flush;
                        PushToken(flush, true);
                        pending = pending.substr(safe);
                    }
                    break;
                }
            }
        }

        // Decode the new token
        llama_batch next_batch = llama_batch_get_one(&new_token, 1);
        if (llama_decode(llama_ctx, next_batch) != 0) {
            PushError("decode failed during generation");
            break;
        }

        generated++;
    }

    llama_sampler_free(smpl);

    // Flush any remaining pending buffer after generation ends
    if (!pending.empty()) {
        if (in_think) {
            full_reasoning += pending;
            PushToken(pending, true);
        } else if (in_tool_call) {
            // Incomplete tool_call block — accumulate silently; DetectToolCalls will handle it
            full_content += pending;
        } else {
            full_content += pending;
            PushToken(pending, false);
        }
        pending.clear();
    }

    // Tool call detection
    if (!full_content.empty() && !gen_opts.tools_json.empty()) {
        DetectToolCalls(full_content);
    }

    // If tool calls detected, push them as a special entry
    if (has_tool_call.load()) {
        std::lock_guard<std::mutex> lock(token_mtx);
        TokenEntry entry;
        entry.text = tool_call_json;
        entry.is_tool_calls = true;
        token_queue.push_back(std::move(entry));
    }

    PushDone();

    last_used_time = std::chrono::steady_clock::now();
    last_used_valid = true;
    status.store(LlamaStatus::IDLE);
}

void LlamaContext::WorkerEmbed() {
    status.store(LlamaStatus::GENERATING);

    if (!model_loaded.load()) {
        WorkerLoad();
        if (!model_loaded.load()) {
            PushError("model failed to load for embedding");
            status.store(LlamaStatus::FAILED);
            return;
        }
    }

    int n_embd = llama_model_n_embd(llama_mdl);
    if (n_embd <= 0) {
        PushError("model does not support embeddings");
        status.store(LlamaStatus::FAILED);
        return;
    }

    // Tokenise
    int max_tokens = (int)embed_input.size() + 256;
    std::vector<llama_token> tokens(max_tokens);
    int n_tokens = llama_tokenize(llama_model_get_vocab(llama_mdl), embed_input.c_str(), (int32_t)embed_input.size(),
        tokens.data(), max_tokens, true, false);
    if (n_tokens < 0) {
        PushError("embedding tokenization failed");
        status.store(LlamaStatus::FAILED);
        return;
    }
    tokens.resize(n_tokens);

    llama_memory_clear(llama_get_memory(llama_ctx), true);

    llama_batch batch = llama_batch_get_one(tokens.data(), n_tokens);
    if (llama_decode(llama_ctx, batch) != 0) {
        PushError("embedding decode failed");
        status.store(LlamaStatus::FAILED);
        return;
    }

    // Get embeddings
    const float* embd = llama_get_embeddings_seq(llama_ctx, 0);
    if (!embd) {
        embd = llama_get_embeddings(llama_ctx);
    }
    if (!embd) {
        PushError("failed to get embeddings");
        status.store(LlamaStatus::FAILED);
        return;
    }

    // L2 normalise
    embed_result.resize(n_embd);
    float norm = 0.0f;
    for (int i = 0; i < n_embd; i++)
        norm += embd[i] * embd[i];
    norm = sqrtf(norm);
    if (norm > 0.0f) {
        for (int i = 0; i < n_embd; i++)
            embed_result[i] = embd[i] / norm;
    } else {
        for (int i = 0; i < n_embd; i++)
            embed_result[i] = embd[i];
    }

    status.store(LlamaStatus::IDLE);
}

// -- Tool system ----------------------------------------------------------------

ToolFamily LlamaContext::DetectToolFamily() const {
    std::string tmpl = GetChatTemplate();
    std::string lower;
    lower.resize(tmpl.size());
    std::transform(tmpl.begin(), tmpl.end(), lower.begin(), ::tolower);

    if (lower.find("llama3") != std::string::npos ||
        lower.find("llama-3") != std::string::npos ||
        lower.find("<|python_tag|>") != std::string::npos)
        return ToolFamily::LLAMA3;

    if (lower.find("[tool_calls]") != std::string::npos ||
        lower.find("[tool_results]") != std::string::npos ||
        lower.find("mistral") != std::string::npos)
        return ToolFamily::MISTRAL;

    if (lower.find("command-r") != std::string::npos ||
        lower.find("cohere") != std::string::npos)
        return ToolFamily::COMMAND_R;

    return ToolFamily::QWEN_HERMES;
}

std::string LlamaContext::ReconstructToolCallContent(const std::string& tool_calls_json) const {
    // tool_calls_json is an OpenAI-format JSON array:
    // [{"id":"...","type":"function","function":{"name":"...","arguments":"..."}}]
    // We reconstruct model-native text based on family.

    // Simple extraction: find name and arguments from the JSON
    // This is a lightweight parse — we just need name+arguments pairs.
    std::string result;

    // Find all function entries
    size_t pos = 0;
    while (pos < tool_calls_json.size()) {
        size_t name_key = tool_calls_json.find("\"name\"", pos);
        if (name_key == std::string::npos)
            break;

        // Extract name value
        size_t name_start = tool_calls_json.find('"', name_key + 6);
        if (name_start == std::string::npos)
            break;
        name_start++;
        size_t name_end = tool_calls_json.find('"', name_start);
        if (name_end == std::string::npos)
            break;
        std::string name = tool_calls_json.substr(name_start, name_end - name_start);

        // Extract arguments value
        size_t args_key = tool_calls_json.find("\"arguments\"", name_end);
        if (args_key == std::string::npos)
            break;

        // Arguments can be a string or object
        size_t args_val_start = tool_calls_json.find_first_of("\"{", args_key + 11);
        if (args_val_start == std::string::npos)
            break;

        std::string arguments;
        if (tool_calls_json[args_val_start] == '"') {
            // String value — extract and unescape
            args_val_start++;
            size_t args_val_end = args_val_start;
            while (args_val_end < tool_calls_json.size()) {
                if (tool_calls_json[args_val_end] == '\\') {
                    args_val_end += 2;
                    continue;
                }
                if (tool_calls_json[args_val_end] == '"')
                    break;
                args_val_end++;
            }
            arguments = tool_calls_json.substr(args_val_start, args_val_end - args_val_start);
            pos = args_val_end + 1;
        } else {
            // Object value — find matching brace
            int depth = 0;
            size_t obj_start = args_val_start;
            size_t i = obj_start;
            for (; i < tool_calls_json.size(); i++) {
                if (tool_calls_json[i] == '{')
                    depth++;
                else if (tool_calls_json[i] == '}') {
                    depth--;
                    if (depth == 0) {
                        i++;
                        break;
                    }
                }
            }
            arguments = tool_calls_json.substr(obj_start, i - obj_start);
            pos = i;
        }

        std::string call_json = "{\"name\":\"" + name + "\",\"arguments\":" + arguments + "}";

        switch (tool_family) {
        case ToolFamily::QWEN_HERMES:
            result += "<tool_call>\n" + call_json + "\n</tool_call>\n";
            break;
        case ToolFamily::LLAMA3:
            result += call_json;
            break;
        case ToolFamily::MISTRAL:
            result += "[TOOL_CALLS] [" + call_json + "]";
            break;
        case ToolFamily::COMMAND_R:
            result += "{\"tool_calls\":[" + call_json + "]}";
            break;
        }
    }

    return result;
}

std::string LlamaContext::BuildToolInjection(const std::string& tools_json) const {
    switch (tool_family) {
    case ToolFamily::QWEN_HERMES:
        return "<tools>\n" + tools_json + "\n</tools>\n\n"
            "When you need to call a tool, respond with a <tool_call> block containing "
            "a JSON object with \"name\" and \"arguments\" keys.\n"
            "Example:\n<tool_call>\n{\"name\": \"tool_name\", \"arguments\": {\"arg\": \"value\"}}\n</tool_call>";

    case ToolFamily::LLAMA3:
        return "You have access to the following tools:\n" + tools_json + "\n\n"
            "When you need to call a tool, respond with a JSON object containing "
            "\"name\" and \"arguments\" keys. Do not wrap in markdown.";

    case ToolFamily::MISTRAL:
        return "[AVAILABLE_TOOLS] " + tools_json + "[/AVAILABLE_TOOLS]";

    case ToolFamily::COMMAND_R:
        return "You have access to the following tools:\n" + tools_json + "\n\n"
            "When you need to call a tool, respond with a JSON object containing "
            "\"name\" and \"arguments\" keys.";
    }
    return "";
}

// Inject a generated "id" field into each tool-call object that lacks one.
// Input is a JSON array string like [{"name":"x","arguments":{}}].
// Returns the array with "id":"call_XXXXXXXX" added where missing.
static std::string EnsureToolCallIds(const std::string& json_array) {
    std::string result;
    result.reserve(json_array.size() + 64);

    size_t pos = 0;
    size_t len = json_array.size();

    // Copy opening '['
    while (pos < len && json_array[pos] != '[')
        result += json_array[pos++];
    if (pos < len)
        result += json_array[pos++]; // '['

    bool first = true;
    while (pos < len) {
        // Skip whitespace / commas between objects
        while (pos < len && (json_array[pos] == ' ' || json_array[pos] == '\t' ||
                              json_array[pos] == '\r' || json_array[pos] == '\n' ||
                              json_array[pos] == ','))
            pos++;
        if (pos >= len || json_array[pos] == ']')
            break;
        if (json_array[pos] != '{')
            break;

        // Find the matching closing brace
        int depth = 0;
        size_t obj_start = pos;
        size_t obj_end   = pos;
        bool   in_str    = false;
        bool   escape    = false;
        for (size_t i = pos; i < len; i++) {
            char c = json_array[i];
            if (escape) {
                escape = false;
            } else if (c == '\\' && in_str) {
                escape = true;
            } else if (c == '"') {
                in_str = !in_str;
            } else if (!in_str) {
                if (c == '{')
                    depth++;
                else if (c == '}') {
                    depth--;
                    if (depth == 0) {
                        obj_end = i;
                        break;
                    }
                }
            }
        }

        std::string obj = json_array.substr(obj_start, obj_end - obj_start + 1);
        pos = obj_end + 1;

        // Check whether the object already contains an "id" key
        if (obj.find("\"id\"") == std::string::npos) {
            // Generate a short random hex id
            char id_buf[24];
            std::snprintf(id_buf, sizeof(id_buf), "call_%08x", (unsigned int)std::rand());
            // Insert after the opening '{': {"id":"call_XXXX",...}
            obj = std::string("{\"id\":\"") + id_buf + "\"," + obj.substr(1);
        }

        if (!first)
            result += ',';
        result += obj;
        first = false;
    }

    result += ']';
    return result;
}

bool LlamaContext::DetectToolCalls(const std::string& content) {
    // Strip leading/trailing whitespace
    std::string trimmed = content;
    size_t start = trimmed.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return false;
    trimmed = trimmed.substr(start);
    size_t end = trimmed.find_last_not_of(" \t\r\n");
    if (end != std::string::npos)
        trimmed = trimmed.substr(0, end + 1);

    // 1. XML <tool_call>...</tool_call> blocks
    {
        std::string result = "[";
        bool found = false;
        size_t pos = 0;
        while (true) {
            size_t tag_start = trimmed.find("<tool_call>", pos);
            if (tag_start == std::string::npos)
                break;
            size_t json_start = tag_start + 11;
            size_t tag_end = trimmed.find("</tool_call>", json_start);
            if (tag_end == std::string::npos)
                break;
            std::string inner = trimmed.substr(json_start, tag_end - json_start);
            // Trim inner
            size_t is = inner.find_first_not_of(" \t\r\n");
            if (is != std::string::npos)
                inner = inner.substr(is);
            size_t ie = inner.find_last_not_of(" \t\r\n");
            if (ie != std::string::npos)
                inner = inner.substr(0, ie + 1);

            if (!inner.empty()) {
                if (found)
                    result += ",";
                result += inner;
                found = true;
            }
            pos = tag_end + 12;
        }
        result += "]";
        if (found) {
            tool_call_json = EnsureToolCallIds(result);
            has_tool_call.store(true);
            return true;
        }
    }

    // 2. Single JSON object with "name" and "arguments"
    if (trimmed.front() == '{' && trimmed.back() == '}') {
        if (trimmed.find("\"name\"") != std::string::npos &&
            trimmed.find("\"arguments\"") != std::string::npos) {
            tool_call_json = EnsureToolCallIds("[" + trimmed + "]");
            has_tool_call.store(true);
            return true;
        }
    }

    // 3. JSON array of objects
    if (trimmed.front() == '[' && trimmed.back() == ']') {
        if (trimmed.find("\"name\"") != std::string::npos &&
            trimmed.find("\"arguments\"") != std::string::npos) {
            tool_call_json = EnsureToolCallIds(trimmed);
            has_tool_call.store(true);
            return true;
        }
    }

    return false;
}

#else
// -- Stub when KITSUNE_LLAMA is not defined -------------------------------------

LlamaContext::LlamaContext(const LlamaCtxOpts&) {}
LlamaContext::~LlamaContext() {}

#endif
