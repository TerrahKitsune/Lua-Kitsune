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
    if (s == LlamaStatus::GENERATING || s == LlamaStatus::UNLOADING)
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
    if (s == LlamaStatus::GENERATING || s == LlamaStatus::LOADING || s == LlamaStatus::UNLOADING)
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

bool LlamaContext::Generate(std::vector<ChatMessage> messages, LlamaGenOpts&& opts) {
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

std::string LlamaContext::GetLoadedModelPath() const {
    return loaded_model_path;
}

bool LlamaContext::IsDisposed() const {
    return disposed.load();
}

const LlamaCtxOpts& LlamaContext::GetCtxOpts() const {
    return ctx_opts;
}

uint32_t LlamaContext::GetActualNCtx() const {
    if (llama_ctx)
        return llama_n_ctx(llama_ctx);
    return (uint32_t)ctx_opts.n_ctx;
}

uint32_t LlamaContext::GetActualNBatch() const {
    if (llama_ctx)
        return llama_n_batch(llama_ctx);
    return (uint32_t)ctx_opts.n_batch;
}

int32_t LlamaContext::GetActualNThreads() const {
    if (llama_ctx)
        return llama_n_threads(llama_ctx);
    return ctx_opts.n_threads > 0 ? ctx_opts.n_threads : (int32_t)std::thread::hardware_concurrency();
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
    return (int)GetActualNCtx() - GetTokensUsed();
}

double LlamaContext::GetSecondsSinceLastUsed() const {
    if (!last_used_valid)
        return -1.0;
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - last_used_time).count();
}

int LlamaContext::GetLastMessagesUsed() const {
    return last_messages_used;
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
    if (requested < 0) return total; // negative means all layers
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

void llama_model_get_capabilities(const llama_model* mdl, std::vector<std::string>& out) {
    out.clear();
    if (!mdl)
        return;

    // Completion: has a decoder
    if (llama_model_has_decoder(mdl))
        out.push_back("completion");

    // Embedding: encoder-only or known embedding architectures
    char arch_buf[64] = {0};
    llama_model_meta_val_str(mdl, "general.architecture", arch_buf, sizeof(arch_buf));
    std::string arch = arch_buf;
    std::string arch_lower(arch.size(), 0);
    std::transform(arch.begin(), arch.end(), arch_lower.begin(), ::tolower);

    if (!llama_model_has_decoder(mdl) && llama_model_has_encoder(mdl))
        out.push_back("embedding");
    else if (arch_lower.find("bert") != std::string::npos ||
             arch_lower.find("nomic") != std::string::npos)
        out.push_back("embedding");

    // Vision: known multimodal architectures
    if (arch_lower.find("mllama") != std::string::npos ||
        arch_lower.find("llava") != std::string::npos ||
        arch_lower.find("minicpm") != std::string::npos ||
        arch_lower.find("qwen2vl") != std::string::npos ||
        arch_lower.find("gemma3") != std::string::npos)
        out.push_back("vision");

    // Tools + Reasoning: inspect embedded chat template
    const char* tmpl_raw = llama_model_chat_template(mdl, nullptr);
    std::string tmpl = tmpl_raw ? tmpl_raw : "";
    std::string tmpl_lower(tmpl.size(), 0);
    std::transform(tmpl.begin(), tmpl.end(), tmpl_lower.begin(), ::tolower);

    if (tmpl_lower.find("tool") != std::string::npos ||
        tmpl_lower.find("function") != std::string::npos ||
        tmpl_lower.find("<|python_tag|>") != std::string::npos)
        out.push_back("tools");

    if (tmpl_lower.find("<think>") != std::string::npos ||
        tmpl_lower.find("/think>") != std::string::npos)
        out.push_back("reasoning");

    // Recurrent state models
    if (llama_model_is_recurrent(mdl))
        out.push_back("recurrent");
}

void LlamaContext::GetCapabilities(std::vector<std::string>& out) const {
    out.clear();
    if (!llama_mdl)
        return;
    llama_model_get_capabilities(llama_mdl, out);
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
    loaded_model_path.clear();
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
    mparams.use_mmap  = ctx_opts.use_mmap;
    mparams.use_mlock = ctx_opts.use_mlock;

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
    cparams.offload_kqv = ctx_opts.offload_kqv;

    llama_ctx = llama_init_from_model(llama_mdl, cparams);
    if (!llama_ctx) {
        llama_model_free(llama_mdl);
        llama_mdl = nullptr;
        PushError("failed to create llama context");
        status.store(LlamaStatus::FAILED);
        return;
    }

    chat_template = ChatTemplate::Detect(llama_mdl);
    loaded_model_path = model_path;
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
    loaded_model_path.clear();
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

// -- ChatTemplate ---------------------------------------------------------------

// Helper: fill ct with Gemma 4 tags and return it.
static ChatTemplate make_gemma4() {
    ChatTemplate ct;
    ct.kind        = ChatTemplate::Kind::GEMMA4;
    ct.think_open  = "<|channel>thought";
    ct.think_close = "<channel|>";
    ct.tool_open   = "<|tool_call>";
    ct.tool_close  = "<tool_call|>";
    return ct;
}

// Helper: fill ct with Gemma 2/3 tags and return it.
static ChatTemplate make_gemma2() {
    ChatTemplate ct;
    ct.kind        = ChatTemplate::Kind::GEMMA2;
    ct.think_open  = "<think>";
    ct.think_close = "</think>";
    ct.tool_open   = "<tool_call>";
    ct.tool_close  = "</tool_call>";
    return ct;
}

// Helper: fill ct with the standard think/tool tags used by most models.
static ChatTemplate make_standard(ChatTemplate::Kind kind) {
    ChatTemplate ct;
    ct.kind        = kind;
    ct.think_open  = "<think>";
    ct.think_close = "</think>";
    ct.tool_open   = "<tool_call>";
    ct.tool_close  = "</tool_call>";
    return ct;
}

ChatTemplate ChatTemplate::Detect(const llama_model* mdl) {
    if (!mdl)
        return make_standard(Kind::QWEN_HERMES);

    // ── Primary: general.architecture from GGUF metadata ─────────────────
    char arch[64] = {};
    llama_model_meta_val_str(mdl, "general.architecture", arch, sizeof(arch));

    std::string a(arch);
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);

    if (a == "gemma4")
        return make_gemma4();

    if (a == "gemma3" || a == "gemma2" || a == "gemma") {
        // Gemma 4 models may report "gemma3" arch via llama.cpp.
        // Check the Jinja template for confirmed Gemma 4 control tokens.
        const char* tmpl = llama_model_chat_template(mdl, nullptr);
        if (tmpl && std::strstr(tmpl, "<|channel>") != nullptr)
            return make_gemma4();
        return make_gemma2();
    }

    if (a.find("llama") != std::string::npos)
        return make_standard(Kind::LLAMA3);

    if (a.find("mistral") != std::string::npos || a.find("mixtral") != std::string::npos)
        return make_standard(Kind::MISTRAL);

    if (a.find("command") != std::string::npos || a.find("cohere") != std::string::npos)
        return make_standard(Kind::COMMAND_R);

    // ── Fallback: scan the Jinja template for known patterns ─────────────
    // Only reached when general.architecture is missing or unrecognised.
    const char* tmpl = llama_model_chat_template(mdl, nullptr);
    if (tmpl) {
        if (std::strstr(tmpl, "<|channel>") != nullptr)
            return make_gemma4();
        if (std::strstr(tmpl, "<start_of_turn>") != nullptr)
            return make_gemma2();
    }

    return make_standard(Kind::QWEN_HERMES);
}

std::string ChatTemplate::MapRole(const std::string& role) const {
    if (kind == Kind::GEMMA4 || kind == Kind::GEMMA2) {
        if (role == "assistant")
            return "model";
    }
    return role;
}

std::string ChatTemplate::FormatPrompt(
    const llama_model* mdl,
    const llama_chat_message* chat,
    size_t n_msg,
    bool add_ass) const
{
    // Try llama.cpp's built-in template engine first (handles most models)
    const char* tmpl = llama_model_chat_template(mdl, nullptr);
    std::vector<char> buf(4096);
    int32_t len = llama_chat_apply_template(tmpl, chat, n_msg, add_ass, buf.data(), (int32_t)buf.size());
    if (len >= 0) {
        if ((size_t)len >= buf.size()) {
            buf.resize((size_t)len + 1);
            len = llama_chat_apply_template(tmpl, chat, n_msg, add_ass, buf.data(), (int32_t)buf.size());
        }
        return std::string(buf.data(), len);
    }

    // Built-in path failed — construct manually using our known format
    std::string prompt;
    prompt.reserve(4096);

    switch (kind) {
    case Kind::GEMMA4:
        for (size_t i = 0; i < n_msg; ++i) {
            prompt += "<|turn>";
            prompt += MapRole(chat[i].role);
            prompt += "\n";
            prompt += chat[i].content;
            prompt += "<turn|>\n";
        }
        if (add_ass)
            prompt += "<|turn>model\n";
        break;

    case Kind::GEMMA2:
        prompt += "<bos>";
        for (size_t i = 0; i < n_msg; ++i) {
            prompt += "<start_of_turn>";
            prompt += MapRole(chat[i].role);
            prompt += "\n";
            prompt += chat[i].content;
            prompt += "<end_of_turn>\n";
        }
        if (add_ass)
            prompt += "<start_of_turn>model\n";
        break;

    default:
        // All other families are in llama_chat_apply_template's built-in list,
        // so we should never reach this path. Return empty to signal failure.
        break;
    }

    return prompt;
}

std::string ChatTemplate::BuildToolInjection(const std::string& tools_json) const {
    switch (kind) {
    case Kind::QWEN_HERMES:
        return "<tools>\n" + tools_json + "\n</tools>\n\n"
            "When you need to call a tool, respond with a <tool_call> block containing "
            "a JSON object with \"name\" and \"arguments\" keys.\n"
            "Example:\n<tool_call>\n{\"name\": \"tool_name\", \"arguments\": {\"arg\": \"value\"}}\n</tool_call>";

    case Kind::LLAMA3:
        return "You have access to the following tools:\n" + tools_json + "\n\n"
            "When you need to call a tool, respond with a JSON object containing "
            "\"name\" and \"arguments\" keys. Do not wrap in markdown.";

    case Kind::MISTRAL:
        return "[AVAILABLE_TOOLS] " + tools_json + "[/AVAILABLE_TOOLS]";

    case Kind::COMMAND_R:
        return "You have access to the following tools:\n" + tools_json + "\n\n"
            "When you need to call a tool, respond with a JSON object containing "
            "\"name\" and \"arguments\" keys.";

    case Kind::GEMMA2:
        return "You have access to the following tools:\n" + tools_json + "\n\n"
            "When you need to call a tool, respond with a <tool_call> block containing "
            "a JSON object with \"name\" and \"arguments\" keys.\n"
            "Example:\n<tool_call>\n{\"name\": \"tool_name\", \"arguments\": {\"arg\": \"value\"}}\n</tool_call>";

    case Kind::GEMMA4:
        return "You have access to the following tools:\n" + tools_json + "\n\n"
            "When you need to call a tool, respond with a <|tool_call> block containing "
            "a JSON object with \"name\" and \"arguments\" keys.\n"
            "Example:\n<|tool_call>\n{\"name\": \"tool_name\", \"arguments\": {\"arg\": \"value\"}}\n<tool_call|>";
    }
    return "";
}

std::string ChatTemplate::ReconstructToolCall(const std::vector<ToolCall>& tool_calls) const {
    std::string result;
    for (const ToolCall& tc : tool_calls) {
        // arguments may be a JSON object string or already-serialized string
        std::string call_json = "{\"name\":\"" + tc.name + "\",\"arguments\":" + tc.arguments + "}";

        switch (kind) {
        case Kind::QWEN_HERMES:
        case Kind::GEMMA2:
            result += "<tool_call>\n" + call_json + "\n</tool_call>\n";
            break;
        case Kind::GEMMA4:
            result += "<|tool_call>\n" + call_json + "\n<tool_call|>\n";
            break;
        case Kind::LLAMA3:
            result += call_json;
            break;
        case Kind::MISTRAL:
            result += "[TOOL_CALLS] [" + call_json + "]";
            break;
        case Kind::COMMAND_R:
            result += "{\"tool_calls\":[" + call_json + "]}";
            break;
        }
    }
    return result;
}

void LlamaContext::WorkerGenerate() {
    status.store(LlamaStatus::GENERATING);

    // Auto-load if needed, or reload if model path has changed
    if (!model_loaded.load() || loaded_model_path != model_path) {
        if (model_loaded.load())
            WorkerUnload();
        WorkerLoad();
        if (!model_loaded.load()) {
            PushError("model failed to load");
            PushDone();
            status.store(LlamaStatus::FAILED);
            return;
        }
        WorkerResetKV();
    }

    full_content.clear();
    full_reasoning.clear();
    tool_call_json.clear();
    has_tool_call.store(false);

    // Detect the model's template family once per generation using arch metadata
    chat_template = ChatTemplate::Detect(llama_mdl);

    // Build prompt via chat template
    std::vector<llama_chat_message> chat_msgs;
    chat_msgs.reserve(gen_messages.size());

    // Check if we need tool injection
    bool has_tools = !gen_opts.tools_json.empty();
    std::string tool_injection;
    if (has_tools)
        tool_injection = chat_template.BuildToolInjection(gen_opts.tools_json);

    for (auto& msg : gen_messages) {
        llama_chat_message cm;
        std::string content = msg.content;

        // Reconstruct tool-call assistant turns from structured vector
        if (!msg.tool_calls.empty())
            content = chat_template.ReconstructToolCall(msg.tool_calls);

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

    // Format prompt using the detected template
    std::string prompt = chat_template.FormatPrompt(llama_mdl, chat_msgs.data(), chat_msgs.size(), true);
    if (prompt.empty()) {
        PushError("failed to apply chat template");
        PushDone();
        status.store(LlamaStatus::IDLE);
        return;
    }

    // Tokenise
    int n_prompt_max = (int)prompt.size() + 256;
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

    // Auto-trim: if the prompt exceeds the context window, drop the oldest
    // non-system messages one at a time until the prompt fits.
    // The system message (index 0) is always preserved.
    while (n_tokens > ctx_opts.n_ctx) {
        bool has_system = !gen_messages.empty() && gen_messages[0].role == "system";
        int drop_idx = has_system ? 1 : 0;
        if (drop_idx >= (int)gen_messages.size()) {
            // Even the system prompt alone is too long — nothing left to cut
            PushError("prompt exceeds context window even after trimming (" +
                      std::to_string(n_tokens) + " tokens > n_ctx " +
                      std::to_string(ctx_opts.n_ctx) + "). Increase n_ctx.");
            PushDone();
            status.store(LlamaStatus::IDLE);
            return;
        }
        gen_messages.erase(gen_messages.begin() + drop_idx);

        // Rebuild chat_msgs from the trimmed message list.
        chat_msgs.clear();
        chat_msgs.reserve(gen_messages.size());
        for (auto& msg : gen_messages) {
            llama_chat_message cm;
            cm.role    = msg.role.c_str();
            cm.content = msg.content.c_str();
            chat_msgs.push_back(cm);
        }

        // Re-format prompt
        prompt = chat_template.FormatPrompt(llama_mdl, chat_msgs.data(), chat_msgs.size(), true);
        if (prompt.empty()) {
            PushError("failed to apply chat template during trim");
            PushDone();
            status.store(LlamaStatus::IDLE);
            return;
        }

        // Re-tokenize the trimmed prompt
        n_prompt_max = (int)prompt.size() + 256;
        tokens.resize(n_prompt_max);
        n_tokens = llama_tokenize(llama_model_get_vocab(llama_mdl), prompt.c_str(), (int32_t)prompt.size(),
            tokens.data(), n_prompt_max, true, true);
        if (n_tokens < 0) {
            PushError("tokenization failed during trim");
            PushDone();
            status.store(LlamaStatus::IDLE);
            return;
        }
        tokens.resize(n_tokens);
    }

    // Record how many messages were actually included after trimming
    last_messages_used = (int)gen_messages.size();

    // Clear KV cache for fresh generation
    llama_memory_clear(llama_get_memory(llama_ctx), true);

    // Create sampler chain
    auto* smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(gen_opts.min_p, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(gen_opts.top_k));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(gen_opts.top_p, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(gen_opts.temperature));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(gen_opts.seed >= 0 ? (uint32_t)gen_opts.seed : LLAMA_DEFAULT_SEED));

    // Decode prompt in n_batch-sized chunks so n_batch can be tuned
    // independently of n_ctx without hitting llama_decode's batch assert.
    int n_decoded = 0;
    while (n_decoded < n_tokens) {
        int chunk = (std::min)(ctx_opts.n_batch, n_tokens - n_decoded);
        llama_batch batch = llama_batch_get_one(tokens.data() + n_decoded, chunk);
        if (llama_decode(llama_ctx, batch) != 0) {
            llama_sampler_free(smpl);
            PushError("prompt decode failed");
            PushDone();
            status.store(LlamaStatus::IDLE);
            return;
        }
        n_decoded += chunk;
    }

    // Generation loop
    bool in_think = false;
    bool in_tool_call = false;
    bool skip_leading_newline = false;
    std::string pending;
    const std::string& THINK_OPEN  = chat_template.think_open;
    const std::string& THINK_CLOSE = chat_template.think_close;
    const std::string& TOOL_OPEN   = chat_template.tool_open;
    const std::string& TOOL_CLOSE  = chat_template.tool_close;
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

        // Track thinking and tool-call blocks using a pending buffer so that
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
                // Accumulate silently until tool close tag; DetectToolCalls parses it afterwards.
                size_t tag_pos = pending.find(TOOL_CLOSE);
                if (tag_pos != std::string::npos) {
                    full_content += pending.substr(0, tag_pos + TOOL_CLOSE.size());
                    pending = pending.substr(tag_pos + TOOL_CLOSE.size());
                    if (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n')) {
                        while (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n'))
                            pending = pending.substr(1);
                    } else {
                        skip_leading_newline = true;
                    }
                    in_tool_call = false;
                } else {
                    // Keep a partial-tag suffix buffered
                    size_t safe = pending.size() >= (TOOL_CLOSE.size() - 1)
                                  ? pending.size() - (TOOL_CLOSE.size() - 1)
                                  : 0;
                    if (safe > 0) {
                        full_content += pending.substr(0, safe);
                        pending = pending.substr(safe);
                    }
                    break;
                }
            } else if (!in_think) {
                // Check for tool open and think open; whichever comes first wins
                size_t tool_pos  = TOOL_OPEN.empty()  ? std::string::npos : pending.find(TOOL_OPEN);
                size_t think_pos = THINK_OPEN.empty() ? std::string::npos : pending.find(THINK_OPEN);
                size_t first_tag = std::string::npos;
                bool   is_tool   = false;
                if (tool_pos  != std::string::npos) { first_tag = tool_pos;  is_tool = true; }
                if (think_pos != std::string::npos && think_pos < first_tag) { first_tag = think_pos; is_tool = false; }

                if (first_tag != std::string::npos) {
                    if (first_tag > 0) {
                        std::string before = pending.substr(0, first_tag);
                        full_content += before;
                        PushToken(before, false);
                    }
                    if (is_tool) {
                        full_content += TOOL_OPEN;
                        pending = pending.substr(first_tag + TOOL_OPEN.size());
                        if (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n')) {
                            while (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n'))
                                pending = pending.substr(1);
                        } else {
                            skip_leading_newline = true;
                        }
                        in_tool_call = true;
                    } else {
                        pending = pending.substr(first_tag + THINK_OPEN.size());
                        if (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n')) {
                            while (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n'))
                                pending = pending.substr(1);
                        } else {
                            skip_leading_newline = true;
                        }
                        in_think = true;
                    }
                } else {
                    // No full open tag yet — keep a partial-tag suffix buffered.
                    // safe = how many bytes are safe to flush (can't be the start of either tag).
                    // If pending is shorter than the tag, buffer everything (safe = 0).
                    size_t safe_think = (!THINK_OPEN.empty() && pending.size() >= (THINK_OPEN.size() - 1))
                                        ? pending.size() - (THINK_OPEN.size() - 1)
                                        : 0;
                    size_t safe_tool  = (!TOOL_OPEN.empty() && pending.size() >= (TOOL_OPEN.size() - 1))
                                        ? pending.size() - (TOOL_OPEN.size() - 1)
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
                size_t tag_pos = THINK_CLOSE.empty() ? std::string::npos : pending.find(THINK_CLOSE);
                if (tag_pos != std::string::npos) {
                    if (tag_pos > 0) {
                        std::string before = pending.substr(0, tag_pos);
                        while (!before.empty() && (before.back() == '\r' || before.back() == '\n'))
                            before.pop_back();
                        if (!before.empty()) {
                            full_reasoning += before;
                            PushToken(before, true);
                        }
                    }
                    pending = pending.substr(tag_pos + THINK_CLOSE.size());
                    if (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n')) {
                        while (!pending.empty() && (pending[0] == '\r' || pending[0] == '\n'))
                            pending = pending.substr(1);
                    } else {
                        skip_leading_newline = true;
                    }
                    in_think = false;
                } else {
                    // Keep a partial close-tag suffix buffered
                    size_t safe = (!THINK_CLOSE.empty() && pending.size() >= (THINK_CLOSE.size() - 1))
                                  ? pending.size() - (THINK_CLOSE.size() - 1)
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

    int n_decoded = 0;
    while (n_decoded < n_tokens) {
        int chunk = (std::min)(ctx_opts.n_batch, n_tokens - n_decoded);
        llama_batch batch = llama_batch_get_one(tokens.data() + n_decoded, chunk);
        if (llama_decode(llama_ctx, batch) != 0) {
            PushError("embedding decode failed");
            status.store(LlamaStatus::FAILED);
            return;
        }
        n_decoded += chunk;
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

    last_used_time = std::chrono::steady_clock::now();
    last_used_valid = true;
    status.store(LlamaStatus::IDLE);
}

// -- Tool system ----------------------------------------------------------------
// ChatTemplate::BuildToolInjection and ChatTemplate::ReconstructToolCall
// contain all model-family-specific tool formatting. See ChatTemplate::Detect.

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
            char id_buf[24];
            std::snprintf(id_buf, sizeof(id_buf), "call_%08x", (unsigned int)std::rand());
            // Insert after the opening '{'
            std::string afterBrace = obj.substr(1);
            // Trim leading whitespace
            size_t ns = afterBrace.find_first_not_of(" \t\r\n");
            if (ns == std::string::npos || afterBrace[ns] == '}') {
                // Empty (or whitespace-only) object — no trailing comma
                obj = std::string("{\"id\":\"") + id_buf + "\"}";
            } else {
                obj = std::string("{\"id\":\"") + id_buf + "\"," + afterBrace;
            }
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

    // 0. Gemma 4: <|tool_call>call:name{args}<tool_call|> blocks
    // Inner content is "call:name{json_args}" or "call:name{}" for no args.
    {
        std::string result = "[";
        bool found = false;
        size_t pos = 0;
        const std::string G4_OPEN  = "<|tool_call>";
        const std::string G4_CLOSE = "<tool_call|>";
        while (true) {
            size_t tag_start = trimmed.find(G4_OPEN, pos);
            if (tag_start == std::string::npos)
                break;
            size_t inner_start = tag_start + G4_OPEN.size();
            size_t tag_end = trimmed.find(G4_CLOSE, inner_start);
            if (tag_end == std::string::npos)
                break;
            std::string inner = trimmed.substr(inner_start, tag_end - inner_start);
            // Trim whitespace
            size_t is = inner.find_first_not_of(" \t\r\n");
            if (is != std::string::npos)
                inner = inner.substr(is);
            size_t ie = inner.find_last_not_of(" \t\r\n");
            if (ie != std::string::npos)
                inner = inner.substr(0, ie + 1);

            // Parse "call:name{args}" format
            if (inner.substr(0, 5) == "call:") {
                std::string rest = inner.substr(5);
                size_t brace = rest.find('{');
                if (brace != std::string::npos) {
                    std::string name = rest.substr(0, brace);
                    std::string args = rest.substr(brace);
                    // Trim name
                    size_t ne = name.find_last_not_of(" \t\r\n");
                    if (ne != std::string::npos)
                        name = name.substr(0, ne + 1);
                    if (!name.empty()) {
                        if (found)
                            result += ",";
                        result += "{\"name\":\"" + name + "\",\"arguments\":" + args + "}";
                        found = true;
                    }
                }
            } else if (!inner.empty()) {
                // Already JSON — pass through
                if (found)
                    result += ",";
                result += inner;
                found = true;
            }
            pos = tag_end + G4_CLOSE.size();
        }
        result += "]";
        if (found) {
            tool_call_json = EnsureToolCallIds(result);
            has_tool_call.store(true);
            return true;
        }
    }

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

// Parse tool_call_json (array of {id, name, arguments}) into structured ToolCall vector.
// The JSON produced by DetectToolCalls has the shape: [{"id":"...","name":"...","arguments":{...}}]
std::vector<ToolCall> LlamaContext::GetToolCalls() const {
    std::vector<ToolCall> out;
    const std::string& json = tool_call_json;
    size_t pos = 0;
    while (pos < json.size()) {
        size_t obj_start = json.find('{', pos);
        if (obj_start == std::string::npos)
            break;
        // Find the matching closing brace
        int depth = 0;
        bool in_str = false;
        bool escape = false;
        size_t obj_end = obj_start;
        for (size_t i = obj_start; i < json.size(); i++) {
            char c = json[i];
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
        std::string obj = json.substr(obj_start, obj_end - obj_start + 1);
        pos = obj_end + 1;

        ToolCall tc;

        auto extract = [&](const char* key) -> std::string {
            std::string k = std::string("\"") + key + "\"";
            size_t kp = obj.find(k);
            if (kp == std::string::npos)
                return {};
            size_t vp = kp + k.size();
            while (vp < obj.size() && (obj[vp] == ' ' || obj[vp] == ':'))
                vp++;
            if (vp >= obj.size())
                return {};
            if (obj[vp] == '"') {
                vp++;
                std::string val;
                bool esc = false;
                while (vp < obj.size()) {
                    char c = obj[vp++];
                    if (esc) {
                        val += c;
                        esc = false;
                    } else if (c == '\\') {
                        esc = true;
                    } else if (c == '"') {
                        break;
                    } else {
                        val += c;
                    }
                }
                return val;
            }
            // Object/number value — grab until matching depth or comma/}
            if (obj[vp] == '{') {
                int d = 0;
                size_t start = vp;
                for (; vp < obj.size(); vp++) {
                    if (obj[vp] == '{') d++;
                    else if (obj[vp] == '}') {
                        d--;
                        if (d == 0) {
                            vp++;
                            break;
                        }
                    }
                }
                return obj.substr(start, vp - start);
            }
            size_t start = vp;
            while (vp < obj.size() && obj[vp] != ',' && obj[vp] != '}')
                vp++;
            return obj.substr(start, vp - start);
        };

        tc.id        = extract("id");
        tc.name      = extract("name");
        tc.arguments = extract("arguments");
        if (tc.arguments.empty())
            tc.arguments = "{}";

        if (!tc.name.empty())
            out.push_back(std::move(tc));
    }
    return out;
}

#else
// -- Stub when KITSUNE_LLAMA is not defined -------------------------------------

LlamaContext::LlamaContext(const LlamaCtxOpts&) {}
LlamaContext::~LlamaContext() {}

#endif
