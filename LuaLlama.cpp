#include "platform.h"
#ifdef KITSUNE_LLAMA

#include "LuaLlama.h"
#include "lua_main_incl.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

// ── Global backend flag ───────────────────────────────────────────────────────

std::atomic<bool> g_llama_backend_initialized{ false };

// ── Log buffer ────────────────────────────────────────────────────────────────
// Allocated on first CreateContext so it is never constructed at static-init
// time (before the allocator is ready).

LlamaLogBuffer* g_log_buf = nullptr;

static void llama_log_cb(ggml_log_level /*level*/, const char* text, void* user_data) {
    LlamaLogBuffer* buf = static_cast<LlamaLogBuffer*>(user_data);
    if (!buf || !text || text[0] == '\0')
        return;
    std::string entry(text);
    if (!entry.empty() && entry.back() == '\n')
        entry.pop_back();
    if (entry.empty())
        return;
    std::unique_lock<std::mutex> lk(buf->mtx);
    if (buf->entries.size() >= LlamaLogBuffer::max_entries)
        buf->entries.erase(buf->entries.begin());
    buf->entries.push_back(std::move(entry));
}

// ── Internal helpers ──────────────────────────────────────────────────────────

static const char* status_str(LlamaStatus s) {
    switch (s) {
    case LlamaStatus::Idle:       return "idle";
    case LlamaStatus::Loading:    return "loading";
    case LlamaStatus::Generating: return "generating";
    case LlamaStatus::Unloading:  return "unloading";
    case LlamaStatus::Error:      return "error";
    default:                      return "unknown";
    }
}

// ── Forward declarations for helpers defined later ────────────────────────────

static std::string get_model_arch(llama_model* model);
static bool arch_has_embedding(const std::string& arch);

static void post_task(LuaLlamaContext* c, LlamaTask task) {
    std::unique_lock<std::mutex> lk(c->task_mtx);
    c->current_task = task;
    lk.unlock();
    c->task_cv.notify_one();
}

static bool post_task_sync(LuaLlamaContext* c, LlamaTask task, int timeout_ms = 30000) {
    post_task(c, task);
    // Wait until the worker has moved out of Idle (i.e. picked up the task).
    std::chrono::steady_clock::time_point pickup_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    while (c->status.load() == LlamaStatus::Idle) {
        if (std::chrono::steady_clock::now() > pickup_deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Now wait until the worker returns to Idle or Error.
    std::chrono::steady_clock::time_point finish_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        LlamaStatus s = c->status.load();
        if (s == LlamaStatus::Idle || s == LlamaStatus::Error)
            return s == LlamaStatus::Idle;
        if (std::chrono::steady_clock::now() > finish_deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// ── Minimal JSON helpers ──────────────────────────────────────────────────────

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
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
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else {
                out += (char)c;
            }
            break;
        }
    }
    return out;
}

static std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string strip_think_block(const std::string& s) {
    std::string t = trim(s);
    if (t.substr(0, 7) != "<think>")
        return t;
    size_t end = t.find("</think>");
    if (end == std::string::npos)
        return t;
    return trim(t.substr(end + 8));
}

static bool json_is_tool_call(const std::string& s, std::string& name, std::string& args) {
    size_t p = s.find('{');
    if (p == std::string::npos)
        return false;

    size_t name_k = s.find("\"name\"", p);
    size_t args_k = s.find("\"arguments\"", p);
    if (name_k == std::string::npos || args_k == std::string::npos)
        return false;

    size_t nv = s.find('"', name_k + 6);
    if (nv == std::string::npos)
        return false;
    nv++;
    size_t nv_end = s.find('"', nv);
    if (nv_end == std::string::npos)
        return false;
    name = s.substr(nv, nv_end - nv);
    if (name.empty())
        return false;

    size_t av = s.find('{', args_k + 11);
    if (av == std::string::npos)
        return false;
    int depth = 0;
    size_t av_end = av;
    for (size_t i = av; i < s.size(); i++) {
        if (s[i] == '{') {
            depth++;
        }
        else if (s[i] == '}') {
            depth--;
            if (depth == 0) {
                av_end = i;
                break;
            }
        }
    }
    if (depth != 0)
        return false;
    args = s.substr(av, av_end - av + 1);
    return true;
}

static std::string build_tool_calls_json(const std::vector<std::pair<std::string, std::string>>& calls) {
    std::string out = "[";
    for (size_t i = 0; i < calls.size(); i++) {
        if (i > 0)
            out += ",";
        out += "{\"name\":\"" + json_escape(calls[i].first) + "\",\"arguments\":" + calls[i].second + "}";
    }
    out += "]";
    return out;
}

static std::string detect_tool_calls(const std::string& raw_content) {
    std::string s = strip_think_block(raw_content);

    // XML <tool_call>...</tool_call> tags (Mistral, Hermes).
    std::vector<std::pair<std::string, std::string>> xml_calls;
    size_t pos = 0;
    while (true) {
        size_t open = s.find("<tool_call>", pos);
        if (open == std::string::npos)
            break;
        size_t close = s.find("</tool_call>", open + 11);
        if (close == std::string::npos)
            break;
        std::string inner = trim(s.substr(open + 11, close - open - 11));
        std::string n;
        std::string a;
        if (json_is_tool_call(inner, n, a))
            xml_calls.push_back({ n, a });
        pos = close + 12;
    }
    if (!xml_calls.empty())
        return build_tool_calls_json(xml_calls);

    // Single JSON object.
    std::string name;
    std::string args;
    if (json_is_tool_call(s, name, args))
        return build_tool_calls_json({ {name, args} });

    // JSON array of tool call objects.
    size_t arr = s.find('[');
    if (arr != std::string::npos) {
        std::vector<std::pair<std::string, std::string>> arr_calls;
        size_t i = arr + 1;
        while (i < s.size()) {
            size_t ob = s.find('{', i);
            if (ob == std::string::npos)
                break;
            int depth = 0;
            size_t ob_end = ob;
            for (size_t j = ob; j < s.size(); j++) {
                if (s[j] == '{') {
                    depth++;
                }
                else if (s[j] == '}') {
                    depth--;
                    if (depth == 0) {
                        ob_end = j;
                        break;
                    }
                }
            }
            if (depth != 0)
                break;
            std::string elem = s.substr(ob, ob_end - ob + 1);
            std::string en;
            std::string ea;
            if (json_is_tool_call(elem, en, ea))
                arr_calls.push_back({ en, ea });
            i = ob_end + 1;
        }
        if (!arr_calls.empty())
            return build_tool_calls_json(arr_calls);
    }

    return "";
}

static std::string serialize_tools_from_lua(lua_State* L, int idx) {
    if (lua_isnoneornil(L, idx))
        return "";
    if (!lua_istable(L, idx))
        return "";

    lua_getglobal(L, "Json");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return "";
    }
    lua_getfield(L, -1, "Encode");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return "";
    }
    lua_pushvalue(L, idx < 0 ? idx - 2 : idx);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        lua_pop(L, 2);
        return "";
    }
    std::string result;
    if (lua_isstring(L, -1))
        result = lua_tostring(L, -1);
    lua_pop(L, 2);
    return result;
}

// ── Worker task functions ─────────────────────────────────────────────────────

static void worker_do_load(LuaLlamaContext* c) {
    c->status.store(LlamaStatus::Loading);

    if (c->ctx) {
        llama_free(c->ctx);
        c->ctx = nullptr;
    }
    if (c->model) {
        llama_model_free(c->model);
        c->model = nullptr;
    }
    c->model_loaded.store(false);

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = c->ctx_opts.n_gpu_layers;

    c->model = llama_model_load_from_file(c->model_path.c_str(), mparams);
    if (!c->model) {
        std::unique_lock<std::mutex> lk(c->token_mtx);
        c->error = "Failed to load model: " + c->model_path;
        lk.unlock();
        c->status.store(LlamaStatus::Error);
        return;
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx        = (uint32_t)c->ctx_opts.n_ctx;
    cparams.n_threads    = (uint32_t)c->ctx_opts.n_threads;
    cparams.n_batch      = (uint32_t)c->ctx_opts.n_batch;
    cparams.flash_attn_type = c->ctx_opts.flash_attn
        ? LLAMA_FLASH_ATTN_TYPE_ENABLED
        : LLAMA_FLASH_ATTN_TYPE_DISABLED;

    // Enable embedding mode for known embedding-only architectures so that
    // llama_get_embeddings* returns valid data.
    cparams.embeddings = arch_has_embedding(get_model_arch(c->model));

    c->ctx = llama_init_from_model(c->model, cparams);
    if (!c->ctx) {
        llama_model_free(c->model);
        c->model = nullptr;
        std::unique_lock<std::mutex> lk(c->token_mtx);
        c->error = "Failed to create llama context";
        lk.unlock();
        c->status.store(LlamaStatus::Error);
        return;
    }

    c->model_loaded.store(true);
    c->status.store(LlamaStatus::Idle);
}

static void worker_do_unload(LuaLlamaContext* c) {
    c->status.store(LlamaStatus::Unloading);
    if (c->ctx) {
        llama_free(c->ctx);
        c->ctx = nullptr;
    }
    if (c->model) {
        llama_model_free(c->model);
        c->model = nullptr;
    }
    c->model_loaded.store(false);
    c->status.store(LlamaStatus::Idle);
}

static void worker_do_reset_kv(LuaLlamaContext* c) {
    if (c->ctx)
        llama_memory_clear(llama_get_memory(c->ctx), false);
    std::unique_lock<std::mutex> lk(c->token_mtx);
    c->token_queue.clear();
    c->full_content.clear();
    c->full_reasoning.clear();
    c->tool_call_json.clear();
    c->error.clear();
    lk.unlock();
    c->has_tool_call.store(false);
}

static void worker_do_generate(LuaLlamaContext* c) {
    if (!c->model_loaded.load()) {
        worker_do_load(c);
        if (c->status.load() == LlamaStatus::Error)
            return;
    }

    c->status.store(LlamaStatus::Generating);

    {
        std::unique_lock<std::mutex> lk(c->token_mtx);
        c->full_content.clear();
        c->full_reasoning.clear();
        c->tool_call_json.clear();
        c->error.clear();
        c->token_queue.clear();
        lk.unlock();
    }
    c->has_tool_call.store(false);

    // Build chat message array for template application.
    std::vector<llama_chat_message> chat_msgs;
    chat_msgs.reserve(c->gen_messages.size());
    for (ChatMessage& m : c->gen_messages)
        chat_msgs.push_back({ m.role.c_str(), m.content.c_str() });

    // Inject tools into a leading system message if provided.
    std::string injected_system;
    std::vector<llama_chat_message> all_msgs;
    if (!c->gen_opts.tools_json.empty()) {
        injected_system = "You have access to the following tools. "
            "To call a tool, respond with a JSON object with \"name\" and \"arguments\" keys.\n\n"
            "Tools:\n" + c->gen_opts.tools_json;
        all_msgs.push_back({ "system", injected_system.c_str() });
        for (llama_chat_message& m : chat_msgs)
            all_msgs.push_back(m);
    }
    else {
        all_msgs = chat_msgs;
    }

    // Apply chat template.
    const char* tmpl = llama_model_chat_template(c->model, nullptr);
    std::vector<char> prompt_buf(4096);
    int prompt_len = llama_chat_apply_template(
        tmpl,
        all_msgs.data(),
        all_msgs.size(),
        true,
        prompt_buf.data(),
        (int32_t)prompt_buf.size()
    );
    if (prompt_len > (int)prompt_buf.size()) {
        prompt_buf.resize(prompt_len + 1);
        prompt_len = llama_chat_apply_template(
            tmpl,
            all_msgs.data(),
            all_msgs.size(),
            true,
            prompt_buf.data(),
            (int32_t)prompt_buf.size()
        );
    }
    if (prompt_len < 0) {
        std::unique_lock<std::mutex> lk(c->token_mtx);
        c->error = "llama_chat_apply_template failed";
        lk.unlock();
        c->status.store(LlamaStatus::Error);
        return;
    }
    std::string prompt(prompt_buf.data(), prompt_len);

    // Tokenize.
    const llama_vocab* vocab = llama_model_get_vocab(c->model);
    std::vector<llama_token> tokens(prompt.size() + 64);
    int n_tokens = llama_tokenize(
        vocab,
        prompt.c_str(),
        (int32_t)prompt.size(),
        tokens.data(),
        (int32_t)tokens.size(),
        true,
        true
    );
    if (n_tokens < 0) {
        tokens.resize(-n_tokens);
        n_tokens = llama_tokenize(
            vocab,
            prompt.c_str(),
            (int32_t)prompt.size(),
            tokens.data(),
            (int32_t)tokens.size(),
            true,
            true
        );
    }
    if (n_tokens < 0) {
        std::unique_lock<std::mutex> lk(c->token_mtx);
        c->error = "Tokenization failed";
        lk.unlock();
        c->status.store(LlamaStatus::Error);
        return;
    }
    tokens.resize(n_tokens);

    // Build sampler chain.
    llama_sampler* sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(sampler, llama_sampler_init_min_p(c->gen_opts.min_p, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_k(c->gen_opts.top_k));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(c->gen_opts.top_p, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(c->gen_opts.temperature));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(
        c->gen_opts.seed < 0 ? LLAMA_DEFAULT_SEED : (uint32_t)c->gen_opts.seed));

    // Decode prompt batch.
    llama_batch batch = llama_batch_get_one(tokens.data(), (int32_t)tokens.size());
    if (llama_decode(c->ctx, batch) != 0) {
        llama_sampler_free(sampler);
        std::unique_lock<std::mutex> lk(c->token_mtx);
        c->error = "llama_decode failed on prompt";
        lk.unlock();
        c->status.store(LlamaStatus::Error);
        return;
    }

    // Generation loop.
    bool in_think = false;
    int  n_generated = 0;
    std::string piece_buf(256, '\0');

    while (!c->stop_flag.load()) {
        llama_token token_id = llama_sampler_sample(sampler, c->ctx, -1);

        if (llama_vocab_is_eog(vocab, token_id))
            break;

        int piece_len = llama_token_to_piece(
            vocab,
            token_id,
            &piece_buf[0],
            (int32_t)piece_buf.size(),
            0,
            true
        );
        if (piece_len < 0) {
            piece_buf.resize(-piece_len);
            piece_len = llama_token_to_piece(
                vocab,
                token_id,
                &piece_buf[0],
                (int32_t)piece_buf.size(),
                0,
                true
            );
        }
        if (piece_len > 0) {
            std::string piece(piece_buf.data(), piece_len);

            if (!in_think && c->full_content.size() + piece.size() > 6) {
                std::string combined = c->full_content + piece;
                if (combined.find("<think>") != std::string::npos)
                    in_think = true;
            }

            bool this_is_reasoning = in_think;

            if (in_think && piece.find("</think>") != std::string::npos) {
                in_think = false;
                this_is_reasoning = true; // </think> itself belongs to reasoning
            }

            if (this_is_reasoning) {
                c->full_reasoning += piece;
            }
            else {
                c->full_content += piece;
            }

            TokenEntry entry;
            entry.text = std::move(piece);
            entry.is_reasoning = this_is_reasoning;
            {
                std::unique_lock<std::mutex> lk(c->token_mtx);
                c->token_queue.push_back(std::move(entry));
            }
        }

        n_generated++;
        if (c->gen_opts.max_tokens > 0 && n_generated >= c->gen_opts.max_tokens)
            break;

        llama_batch next_batch = llama_batch_get_one(&token_id, 1);
        if (llama_decode(c->ctx, next_batch) != 0)
            break;
    }

    llama_sampler_free(sampler);

    std::string tool_json = detect_tool_calls(c->full_content);
    if (!tool_json.empty()) {
        std::unique_lock<std::mutex> lk(c->token_mtx);
        c->tool_call_json = std::move(tool_json);
        lk.unlock();
        c->has_tool_call.store(true);
    }

    c->last_used_time = std::chrono::steady_clock::now();
    c->generation_done.store(true);
    c->status.store(LlamaStatus::Idle);
}

static void worker_do_embed(LuaLlamaContext* c) {
    c->status.store(LlamaStatus::Generating);

    std::string input;
    {
        std::unique_lock<std::mutex> lk(c->token_mtx);
        input = c->embed_input;
        c->embed_result.clear();
    }

    if (!c->model || !c->ctx) {
        std::unique_lock<std::mutex> lk(c->token_mtx);
        c->error = "No model loaded";
        lk.unlock();
        c->status.store(LlamaStatus::Error);
        c->generation_done.store(true);
        return;
    }

    // Tokenize.
    std::vector<llama_token> tokens(input.size() + 16);
    int n = llama_tokenize(
        llama_model_get_vocab(c->model),
        input.c_str(), (int32_t)input.size(),
        tokens.data(), (int32_t)tokens.size(),
        /*add_special=*/true, /*parse_special=*/false);
    if (n < 0) {
        std::unique_lock<std::mutex> lk(c->token_mtx);
        c->error = "Tokenization failed";
        lk.unlock();
        c->status.store(LlamaStatus::Error);
        c->generation_done.store(true);
        return;
    }
    tokens.resize((size_t)n);

    llama_memory_clear(llama_get_memory(c->ctx), false);

    llama_batch batch = llama_batch_get_one(tokens.data(), (int32_t)tokens.size());
    if (llama_decode(c->ctx, batch) != 0) {
        std::unique_lock<std::mutex> lk(c->token_mtx);
        c->error = "llama_decode failed during embed";
        lk.unlock();
        c->status.store(LlamaStatus::Error);
        c->generation_done.store(true);
        return;
    }

    int32_t n_embd = llama_model_n_embd(c->model);
    float* raw = llama_get_embeddings_seq(c->ctx, 0);
    if (!raw) {
        // Fall back to last-token embedding.
        raw = llama_get_embeddings_ith(c->ctx, batch.n_tokens - 1);
    }

    std::vector<float> result;
    if (raw && n_embd > 0) {
        // L2-normalise.
        float sum = 0.0f;
        for (int32_t i = 0; i < n_embd; i++)
            sum += raw[i] * raw[i];
        float norm = (sum > 0.0f) ? 1.0f / std::sqrt(sum) : 1.0f;
        result.resize((size_t)n_embd);
        for (int32_t i = 0; i < n_embd; i++)
            result[(size_t)i] = raw[i] * norm;
    }
    else {
        std::unique_lock<std::mutex> lk(c->token_mtx);
        c->error = "No embeddings returned — is this an embedding model?";
        lk.unlock();
        c->status.store(LlamaStatus::Error);
        c->generation_done.store(true);
        return;
    }

    {
        std::unique_lock<std::mutex> lk(c->token_mtx);
        c->embed_result = std::move(result);
    }

    c->last_used_time = std::chrono::steady_clock::now();
    c->generation_done.store(true);
    c->status.store(LlamaStatus::Idle);
}

// ── Worker thread entry ───────────────────────────────────────────────────────

void llama_worker(LuaLlamaContext* c) {
    while (true) {
        LlamaTask task;
        {
            std::unique_lock<std::mutex> lk(c->task_mtx);

            // Use a short-tick wait so TTL can fire even when no task is posted.
            // The predicate only wakes on an explicit task; stop_flag is only
            // read inside worker_do_generate to interrupt the token loop.
            // Dispose wakes us by posting an explicit Shutdown task.
            std::chrono::milliseconds tick(500);
            c->task_cv.wait_for(lk, tick, [c] {
                return c->current_task != LlamaTask::Idle;
            });
            task = c->current_task;
            c->current_task = LlamaTask::Idle;
        }

        if (task == LlamaTask::Idle) {
            // Woken by timeout: check TTL.
            if (c->model_ttl_ms > 0 && c->model_loaded.load()) {
                std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
                int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - c->last_used_time).count();
                if (elapsed >= c->model_ttl_ms)
                    worker_do_unload(c);
            }
            continue;
        }

        switch (task) {
        case LlamaTask::Load:
            worker_do_load(c);
            break;
        case LlamaTask::Generate:
            worker_do_generate(c);
            break;
        case LlamaTask::Unload:
            worker_do_unload(c);
            break;
        case LlamaTask::ResetKV:
            worker_do_reset_kv(c);
            c->status.store(LlamaStatus::Idle);
            break;
        case LlamaTask::Embed:
            worker_do_embed(c);
            break;
        case LlamaTask::Shutdown:
            worker_do_unload(c);
            return;
        default:
            break;
        }
    }
}

// ── Userdata helpers ──────────────────────────────────────────────────────────

LuaLlamaContext* lua_tollama(lua_State* L, int index) {
    LuaLlamaContext* c = (LuaLlamaContext*)lua_touserdata(L, index);
    if (!c)
        luaL_error(L, "parameter is not a %s", LUALLAMA_CTX);
    return c;
}

LuaLlamaContext* lua_pushllamacontext(lua_State* L) {
    LuaLlamaContext* c = (LuaLlamaContext*)lua_newuserdata(L, sizeof(LuaLlamaContext));
    if (!c)
        luaL_error(L, "Unable to create LuaLlamaContext");
    luaL_getmetatable(L, LUALLAMA_CTX);
    lua_setmetatable(L, -2);

    new (&c->worker)          std::thread();
    new (&c->task_mtx)        std::mutex();
    new (&c->task_cv)         std::condition_variable();
    new (&c->token_mtx)       std::mutex();
    new (&c->token_queue)     std::vector<TokenEntry>();
    new (&c->error)           std::string();
    new (&c->model_path)      std::string();
    new (&c->gen_messages)    std::vector<ChatMessage>();
    new (&c->gen_opts)        LlamaGenOpts();
    new (&c->ctx_opts)        LlamaCtxOpts();
    new (&c->full_content)    std::string();
    new (&c->full_reasoning)  std::string();
    new (&c->tool_call_json)  std::string();
    new (&c->last_used_time)  std::chrono::steady_clock::time_point();

    c->current_task = LlamaTask::Idle;
    c->status.store(LlamaStatus::Idle);
    c->stop_flag.store(false);
    c->model_loaded.store(false);
    c->has_tool_call.store(false);
    c->disposed.store(false);
    c->generation_done.store(true);
    c->model_ttl_ms = 300000;
    c->model = nullptr;
    c->ctx = nullptr;
    return c;
}

static int cpu_thread_count() {
    int n = (int)std::thread::hardware_concurrency();
    return n > 0 ? n : 4;
}

// ── Lua API ───────────────────────────────────────────────────────────────────

int LlamaCreateContext(lua_State* L) {
    if (!g_llama_backend_initialized.load()) {
        llama_backend_init();
        g_log_buf = new LlamaLogBuffer();
        llama_log_set(llama_log_cb, g_log_buf);
        g_llama_backend_initialized.store(true);
    }

    int     n_gpu_layers = 99;
    int     n_ctx        = 4096;
    int     n_threads    = cpu_thread_count();
    int     n_batch      = 512;
    bool    flash_attn   = false;
    int64_t model_ttl    = 300000;

    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "n_gpu_layers");
        if (!lua_isnil(L, -1))
            n_gpu_layers = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "n_ctx");
        if (!lua_isnil(L, -1))
            n_ctx = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "n_threads");
        if (!lua_isnil(L, -1))
            n_threads = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "n_batch");
        if (!lua_isnil(L, -1))
            n_batch = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, 1, "flash_attn");
        if (!lua_isnil(L, -1))
            flash_attn = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);

        lua_getfield(L, 1, "model_ttl_ms");
        if (!lua_isnil(L, -1))
            model_ttl = (int64_t)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, lua_gettop(L));

    LuaLlamaContext* c = lua_pushllamacontext(L);
    c->ctx_opts.n_gpu_layers = n_gpu_layers;
    c->ctx_opts.n_ctx        = n_ctx;
    c->ctx_opts.n_threads    = n_threads;
    c->ctx_opts.n_batch      = n_batch;
    c->ctx_opts.flash_attn   = flash_attn;
    c->model_ttl_ms          = model_ttl;
    c->last_used_time        = std::chrono::steady_clock::now();

    c->worker = std::thread(llama_worker, c);
    return 1;
}

int LlamaSetModel(lua_State* L) {
    LuaLlamaContext* c = lua_tollama(L, 1);
    if (c->disposed.load()) {
        lua_pushnil(L);
        lua_pushstring(L, "disposed");
        return 2;
    }

    LlamaStatus s = c->status.load();
    if (s == LlamaStatus::Loading || s == LlamaStatus::Generating) {
        lua_pushnil(L);
        lua_pushstring(L, "busy");
        return 2;
    }

    const char* path = luaL_checkstring(L, 2);
    int n_gpu_layers = c->ctx_opts.n_gpu_layers;

    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "n_gpu_layers");
        if (!lua_isnil(L, -1))
            n_gpu_layers = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, lua_gettop(L));

    c->model_path            = path;
    c->ctx_opts.n_gpu_layers = n_gpu_layers;
    c->model_loaded.store(false);

    lua_pushboolean(L, 1);
    return 1;
}

int LlamaLoadModel(lua_State* L) {
    LuaLlamaContext* c = lua_tollama(L, 1);
    if (c->disposed.load()) {
        lua_pushnil(L);
        lua_pushstring(L, "disposed");
        return 2;
    }

    if (c->model_loaded.load()) {
        lua_pushboolean(L, 1);
        return 1;
    }

    LlamaStatus s = c->status.load();
    if (s == LlamaStatus::Generating || s == LlamaStatus::Loading) {
        lua_pushnil(L);
        lua_pushstring(L, "busy");
        return 2;
    }

    if (c->model_path.empty()) {
        lua_pushnil(L);
        lua_pushstring(L, "no model set");
        return 2;
    }
    lua_pop(L, lua_gettop(L));

    bool ok = post_task_sync(c, LlamaTask::Load, 60000);
    if (!ok) {
        std::unique_lock<std::mutex> lk(c->token_mtx);
        std::string err = c->error.empty() ? "load timed out or failed" : c->error;
        lk.unlock();
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int LlamaUnloadModel(lua_State* L) {
    LuaLlamaContext* c = lua_tollama(L, 1);
    if (c->disposed.load()) {
        lua_pushnil(L);
        lua_pushstring(L, "disposed");
        return 2;
    }

    LlamaStatus s = c->status.load();
    if (s == LlamaStatus::Loading || s == LlamaStatus::Generating) {
        lua_pushnil(L);
        lua_pushstring(L, "busy");
        return 2;
    }
    lua_pop(L, lua_gettop(L));
    post_task(c, LlamaTask::Unload);
    lua_pushboolean(L, 1);
    return 1;
}

int LlamaIsModelLoaded(lua_State* L) {
    LuaLlamaContext* c = lua_tollama(L, 1);
    lua_pushboolean(L, c->disposed.load() ? 0 : (c->model_loaded.load() ? 1 : 0));
    return 1;
}

int LlamaIsReady(lua_State* L) {
    LuaLlamaContext* c = lua_tollama(L, 1);
    if (c->disposed.load()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    bool ready = c->status.load() == LlamaStatus::Idle && !c->model_path.empty();
    lua_pushboolean(L, ready ? 1 : 0);
    return 1;
}

// ── Capability probing helpers ────────────────────────────────────────────────

static std::string get_model_arch(llama_model* model) {
    char buf[128] = {};
    int r = llama_model_meta_val_str(model, "general.architecture", buf, sizeof(buf));
    if (r < 0)
        return "";
    return std::string(buf, (size_t)r);
}

// Known vision-capable architectures (encoder-decoder models with a visual
// encoder, or multimodal decoder-only models).
static bool arch_has_vision(const std::string& arch, bool has_encoder) {
    if (has_encoder)
        return true;
    // Decoder-only multimodal families that embed image tokens.
    static const char* vision_archs[] = {
        "mllama", "llava", "qwen2vl", "qwen2_vl", "minicpmv",
        "internvl", "gemma3", "moondream", nullptr
    };
    for (int i = 0; vision_archs[i]; i++) {
        if (arch.find(vision_archs[i]) != std::string::npos)
            return true;
    }
    return false;
}

static bool arch_has_reasoning(const std::string& arch, const std::string& tmpl) {
    static const char* think_keys[] = {
        "deepseek", "qwq", "think", "r1", nullptr
    };
    for (int i = 0; think_keys[i]; i++) {
        if (arch.find(think_keys[i]) != std::string::npos)
            return true;
        if (tmpl.find(think_keys[i]) != std::string::npos)
            return true;
    }
    return false;
}

static bool arch_has_tools(const std::string& arch, const std::string& tmpl) {
    static const char* tool_keys[] = {
        "tool", "llama3", "mistral", "qwen", "command-r", nullptr
    };
    for (int i = 0; tool_keys[i]; i++) {
        if (tmpl.find(tool_keys[i]) != std::string::npos)
            return true;
    }
    (void)arch;
    return false;
}

static bool arch_has_embedding(const std::string& arch) {
    static const char* embed_archs[] = {
        "bert", "nomic", "e5", "bge", "gte", "minilm",
        "roberta", "xlm", nullptr
    };
    for (int i = 0; embed_archs[i]; i++) {
        if (arch.find(embed_archs[i]) != std::string::npos)
            return true;
    }
    return false;
}

// Pushes a Lua array of capability strings onto the stack.
static void push_capabilities(lua_State* L, llama_model* model) {
    const char* tmpl_raw = llama_model_chat_template(model, nullptr);
    std::string tmpl = tmpl_raw ? tmpl_raw : "";
    std::string arch = get_model_arch(model);
    bool has_encoder = llama_model_has_encoder(model);
    bool has_decoder = llama_model_has_decoder(model);

    std::vector<const char*> caps;

    // Decoder models can do text completion.
    if (has_decoder)
        caps.push_back("completion");

    if (arch_has_tools(arch, tmpl))
        caps.push_back("tools");

    if (arch_has_reasoning(arch, tmpl))
        caps.push_back("reasoning");

    if (arch_has_vision(arch, has_encoder))
        caps.push_back("vision");

    if (arch_has_embedding(arch))
        caps.push_back("embedding");

    lua_createtable(L, (int)caps.size(), 0);
    for (size_t i = 0; i < caps.size(); i++) {
        lua_pushstring(L, caps[i]);
        lua_rawseti(L, -2, (int)(i + 1));
    }
}

int LlamaInfo(lua_State* L) {
    LuaLlamaContext* c = lua_tollama(L, 1);
    if (c->disposed.load()) {
        lua_pushnil(L);
        lua_pushstring(L, "disposed");
        return 2;
    }
    lua_pop(L, lua_gettop(L));

    lua_newtable(L);

    lua_newtable(L);
    lua_pushstring(L, status_str(c->status.load()));
    lua_setfield(L, -2, "status");
    lua_pushinteger(L, c->ctx_opts.n_ctx);
    lua_setfield(L, -2, "n_ctx");
    lua_pushinteger(L, c->ctx_opts.n_gpu_layers);
    lua_setfield(L, -2, "n_gpu_layers");
    lua_pushinteger(L, c->ctx_opts.n_threads);
    lua_setfield(L, -2, "n_threads");
    lua_pushinteger(L, c->ctx_opts.n_batch);
    lua_setfield(L, -2, "n_batch");
    lua_pushinteger(L, (lua_Integer)c->model_ttl_ms);
    lua_setfield(L, -2, "model_ttl_ms");

    if (!c->model_path.empty()) {
        lua_pushstring(L, c->model_path.c_str());
    }
    else {
        lua_pushnil(L);
    }
    lua_setfield(L, -2, "model_path");

    {
        std::unique_lock<std::mutex> lk(c->token_mtx);
        std::string err = c->error;
        lk.unlock();
        if (!err.empty()) {
            lua_pushstring(L, err.c_str());
        }
        else {
            lua_pushnil(L);
        }
    }
    lua_setfield(L, -2, "error");

    if (c->model_loaded.load()) {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(now - c->last_used_time).count();
        lua_pushnumber(L, (lua_Number)secs);
    }
    else {
        lua_pushnil(L);
    }
    lua_setfield(L, -2, "last_used");

    // KV cache usage — only meaningful when a model is loaded and the context exists.
    if (c->model_loaded.load() && c->ctx) {
        llama_memory_t mem = llama_get_memory(c->ctx);
        llama_pos pos_max = llama_memory_seq_pos_max(mem, 0);
        int32_t tokens_used = (pos_max >= 0) ? (int32_t)(pos_max + 1) : 0;
        int32_t tokens_avail = c->ctx_opts.n_ctx - tokens_used;
        if (tokens_avail < 0)
            tokens_avail = 0;
        lua_pushinteger(L, (lua_Integer)tokens_used);
        lua_setfield(L, -2, "tokens_used");
        lua_pushinteger(L, (lua_Integer)tokens_avail);
        lua_setfield(L, -2, "tokens_available");
    }
    else {
        lua_pushinteger(L, 0);
        lua_setfield(L, -2, "tokens_used");
        lua_pushinteger(L, (lua_Integer)c->ctx_opts.n_ctx);
        lua_setfield(L, -2, "tokens_available");
    }

    lua_setfield(L, -2, "context");

    if (c->model_loaded.load() && c->model) {
        lua_newtable(L);

        char desc_buf[256] = {};
        llama_model_desc(c->model, desc_buf, sizeof(desc_buf));
        lua_pushstring(L, desc_buf);
        lua_setfield(L, -2, "desc");

        std::string arch = get_model_arch(c->model);
        lua_pushstring(L, arch.c_str());
        lua_setfield(L, -2, "arch");

        lua_pushinteger(L, (lua_Integer)llama_model_n_ctx_train(c->model));
        lua_setfield(L, -2, "context_length");
        lua_pushinteger(L, (lua_Integer)llama_model_n_params(c->model));
        lua_setfield(L, -2, "n_params");
        lua_pushinteger(L, (lua_Integer)llama_model_n_embd(c->model));
        lua_setfield(L, -2, "n_embd");
        lua_pushinteger(L, (lua_Integer)llama_model_n_layer(c->model));
        lua_setfield(L, -2, "n_layer");
        lua_pushinteger(L, (lua_Integer)llama_model_size(c->model));
        lua_setfield(L, -2, "size_bytes");

        const char* tmpl = llama_model_chat_template(c->model, nullptr);
        lua_pushstring(L, tmpl ? tmpl : "");
        lua_setfield(L, -2, "chat_template");

        lua_pushinteger(L, c->ctx_opts.n_gpu_layers);
        lua_setfield(L, -2, "n_gpu_layers");

        // Compute actual layer placement percentages, matching how Ollama reports them.
        // llama.cpp offloads min(n_gpu_layers, n_layer) transformer layers to GPU.
        // The output layer (vocab projection) is an additional layer that only goes to
        // GPU when all transformer layers are offloaded (n_gpu_layers > n_layer).
        {
            int32_t n_layer = llama_model_n_layer(c->model);
            int32_t requested = c->ctx_opts.n_gpu_layers;
            int32_t total_layers = n_layer + 1; // +1 for output layer
            int32_t gpu_layers = 0;
            if (requested >= total_layers) {
                gpu_layers = total_layers;
            }
            else if (requested > 0) {
                gpu_layers = requested; // output layer stays on CPU
            }
            lua_Number gpu_pct = (total_layers > 0)
                ? (lua_Number)gpu_layers / (lua_Number)total_layers * 100.0
                : 0.0;
            lua_Number cpu_pct = 100.0 - gpu_pct;
            lua_pushnumber(L, gpu_pct);
            lua_setfield(L, -2, "gpu_percent");
            lua_pushnumber(L, cpu_pct);
            lua_setfield(L, -2, "cpu_percent");
            lua_pushinteger(L, (lua_Integer)gpu_layers);
            lua_setfield(L, -2, "gpu_layer_count");
            lua_pushinteger(L, (lua_Integer)(total_layers - gpu_layers));
            lua_setfield(L, -2, "cpu_layer_count");
        }

        push_capabilities(L, c->model);
        lua_setfield(L, -2, "capabilities");

        lua_setfield(L, -2, "model");
    }
    else {
        lua_pushnil(L);
        lua_setfield(L, -2, "model");
    }

    return 1;
}

int LlamaGetLogs(lua_State* L) {
    lua_pop(L, lua_gettop(L));
    std::vector<std::string> entries;
    if (g_log_buf) {
        std::unique_lock<std::mutex> lk(g_log_buf->mtx);
        entries.swap(g_log_buf->entries);
    }
    lua_createtable(L, (int)entries.size(), 0);
    for (size_t i = 0; i < entries.size(); i++) {
        lua_pushstring(L, entries[i].c_str());
        lua_rawseti(L, -2, (int)(i + 1));
    }
    return 1;
}

int LlamaGenerate(lua_State* L) {
    LuaLlamaContext* c = lua_tollama(L, 1);
    if (c->disposed.load()) {
        lua_pushnil(L);
        lua_pushstring(L, "disposed");
        return 2;
    }

    if (c->status.load() != LlamaStatus::Idle) {
        lua_pushnil(L);
        lua_pushstring(L, "already running");
        return 2;
    }
    if (c->model_path.empty()) {
        lua_pushnil(L);
        lua_pushstring(L, "no model set");
        return 2;
    }

    // Read messages table (arg 2).
    if (!lua_istable(L, 2)) {
        lua_pushnil(L);
        lua_pushstring(L, "messages must be a table");
        return 2;
    }

    std::vector<ChatMessage> messages;
    int n = (int)lua_rawlen(L, 2);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 2, i);
        if (lua_istable(L, -1)) {
            ChatMessage msg;
            lua_getfield(L, -1, "role");
            if (lua_isstring(L, -1))
                msg.role = lua_tostring(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "content");
            if (lua_isstring(L, -1))
                msg.content = lua_tostring(L, -1);
            lua_pop(L, 1);
            if (!msg.role.empty())
                messages.push_back(std::move(msg));
        }
        lua_pop(L, 1);
    }
    if (messages.empty()) {
        lua_pushnil(L);
        lua_pushstring(L, "messages table is empty");
        return 2;
    }

    // Read opts table (arg 3).
    LlamaGenOpts opts;
    opts.temperature = 0.8f;
    opts.top_p       = 0.95f;
    opts.top_k       = 40;
    opts.min_p       = 0.05f;
    opts.seed        = -1;
    opts.max_tokens  = 2048;

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

        lua_getfield(L, 3, "tools");
        opts.tools_json = serialize_tools_from_lua(L, -1);
        lua_pop(L, 1);
    }

    lua_pop(L, lua_gettop(L));

    c->gen_messages = std::move(messages);
    c->gen_opts     = std::move(opts);
    c->stop_flag.store(false);
    c->generation_done.store(false); // in-flight until worker sets true

    post_task(c, LlamaTask::Generate);

    lua_pushboolean(L, 1);
    return 1;
}

int LlamaEmbed(lua_State* L) {
    LuaLlamaContext* c = lua_tollama(L, 1);

    if (c->disposed.load()) {
        lua_pushnil(L);
        lua_pushstring(L, "disposed");
        return 2;
    }

    if (c->status.load() != LlamaStatus::Idle) {
        lua_pushnil(L);
        lua_pushstring(L, "context is busy");
        return 2;
    }

    size_t len = 0;
    const char* text = luaL_checklstring(L, 2, &len);
    if (!text || len == 0) {
        lua_pushnil(L);
        lua_pushstring(L, "text is empty");
        return 2;
    }

    {
        std::unique_lock<std::mutex> lk(c->token_mtx);
        c->embed_input = std::string(text, len);
        c->embed_result.clear();
        c->error.clear();
    }

    c->generation_done.store(false);
    post_task_sync(c, LlamaTask::Embed);

    // Wait for worker to finish (embed is fast, so a tight poll is fine).
    while (!c->generation_done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (c->status.load() == LlamaStatus::Error) {
        std::string err;
        {
            std::unique_lock<std::mutex> lk(c->token_mtx);
            err = c->error;
        }
        lua_pushnil(L);
        lua_pushstring(L, err.c_str());
        return 2;
    }

    std::vector<float> result;
    {
        std::unique_lock<std::mutex> lk(c->token_mtx);
        result = c->embed_result;
    }

    lua_createtable(L, (int)result.size(), 0);
    for (size_t i = 0; i < result.size(); i++) {
        lua_pushnumber(L, (lua_Number)result[i]);
        lua_rawseti(L, -2, (int)(i + 1));
    }
    return 1;
}

int LlamaPoll(lua_State* L) {
    LuaLlamaContext* c = lua_tollama(L, 1);
    lua_pop(L, lua_gettop(L));

    if (c->disposed.load()) {
        // Context disposed — treat as done.
        lua_pushboolean(L, 0);
        return 1;
    }

    LlamaStatus s = c->status.load();
    bool done = c->generation_done.load();

    // Check for error first — emit as a data packet then caller will see ok=false next call.
    if (s == LlamaStatus::Error) {
        std::string err;
        {
            std::unique_lock<std::mutex> lk(c->token_mtx);
            err = c->error;
        }
        lua_pushboolean(L, 1);
        lua_createtable(L, 0, 2);
        lua_pushstring(L, err.c_str());
        lua_setfield(L, -2, "text");
        lua_pushstring(L, "error");
        lua_setfield(L, -2, "type");
        return 2;
    }

    // Drain one batch of same-type tokens from the queue.
    std::string text;
    bool is_reasoning = false;
    {
        std::unique_lock<std::mutex> lk(c->token_mtx);
        if (!c->token_queue.empty()) {
            is_reasoning = c->token_queue[0].is_reasoning;
            size_t i = 0;
            while (i < c->token_queue.size() && c->token_queue[i].is_reasoning == is_reasoning) {
                text += c->token_queue[i].text;
                i++;
            }
            c->token_queue.erase(c->token_queue.begin(), c->token_queue.begin() + i);
        }
    }

    // If the queue yielded data, return it.
    if (!text.empty()) {
        const char* type_str = is_reasoning ? "reasoning" : "token";
        lua_pushboolean(L, 1);
        lua_createtable(L, 0, 2);
        lua_pushstring(L, text.c_str());
        lua_setfield(L, -2, "text");
        lua_pushstring(L, type_str);
        lua_setfield(L, -2, "type");
        return 2;
    }

    // Queue empty — check if generation is done.
    if (done) {
        // Emit pending tool call data before signalling done.
        if (c->has_tool_call.load()) {
            std::string tj;
            {
                std::unique_lock<std::mutex> lk(c->token_mtx);
                tj = c->tool_call_json;
            }
            c->has_tool_call.store(false);
            lua_pushboolean(L, 1);
            lua_createtable(L, 0, 2);
            lua_pushstring(L, tj.c_str());
            lua_setfield(L, -2, "text");
            lua_pushstring(L, "tool_calls");
            lua_setfield(L, -2, "type");
            return 2;
        }
        // Nothing left — signal done.
        lua_pushboolean(L, 0);
        return 1;
    }

    // Still generating, nothing ready yet.
    lua_pushboolean(L, 1);
    lua_pushnil(L);
    return 2;
}

int LlamaStop(lua_State* L) {
    LuaLlamaContext* c = lua_tollama(L, 1);
    c->stop_flag.store(true);
    lua_pushboolean(L, 1);
    return 1;
}

int LlamaReset(lua_State* L) {
    LuaLlamaContext* c = lua_tollama(L, 1);
    if (c->disposed.load()) {
        lua_pushnil(L);
        lua_pushstring(L, "disposed");
        return 2;
    }

    if (c->status.load() == LlamaStatus::Generating) {
        lua_pushnil(L);
        lua_pushstring(L, "busy");
        return 2;
    }
    lua_pop(L, lua_gettop(L));
    post_task(c, LlamaTask::ResetKV);
    lua_pushboolean(L, 1);
    return 1;
}

int LlamaDispose(lua_State* L) {
    LuaLlamaContext* c = lua_tollama(L, 1);
    if (c->disposed.exchange(true))
        goto done;

    c->stop_flag.store(true);
    post_task(c, LlamaTask::Shutdown);
    if (c->worker.joinable())
        c->worker.join();

    c->worker.~thread();
    c->task_mtx.~mutex();
    c->task_cv.~condition_variable();
    c->token_mtx.~mutex();
    c->token_queue.~vector();
    c->error.~basic_string();
    c->model_path.~basic_string();
    c->gen_messages.~vector();
    c->gen_opts.~LlamaGenOpts();
    c->ctx_opts.~LlamaCtxOpts();
    c->full_content.~basic_string();
    c->full_reasoning.~basic_string();
    c->tool_call_json.~basic_string();
    c->last_used_time.~time_point();

done:
    lua_pushboolean(L, 1);
    return 1;
}

int llama_ctx_gc(lua_State* L) {
    LlamaDispose(L);
    return 0;
}

int llama_ctx_tostring(lua_State* L) {
    LuaLlamaContext* c = lua_tollama(L, 1);
    char buf[128];
    if (c->disposed.load()) {
        snprintf(buf, sizeof(buf), "LlamaContext: disposed");
    }
    else {
        snprintf(buf, sizeof(buf), "LlamaContext: %s [%s]",
            c->model_path.empty() ? "(no model)" : c->model_path.c_str(),
            status_str(c->status.load()));
    }
    lua_pushstring(L, buf);
    return 1;
}

#endif // KITSUNE_LLAMA
