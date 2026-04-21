#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef KITSUNE_IMGUI

#include "ImguiRenderer.h"
#include "ImguiMarkdown.h"
#include "OpenGL.h"
#include "Font.h"
#include "ImguiHtml.h"

#include "Imgui/imgui.h"
#include "Imgui/imgui_impl_sdl2.h"
#include "Imgui/imgui_impl_opengl3.h"
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

// argv[0] is always the renderer userdata (self) from Lua colon-call syntax.
// Use these macros in every kitsune_CFunction in this file.
#define IMGUI_ARGC  (argc - 1)
#define IMGUI_ARGV  (argc > 0 ? argv + 1 : argv)
// Extract ImguiWindowContext* from self (argv[0]) — works even when ud is null
// because hand-written overrides are registered with userdata=nullptr.
#define IMGUI_CTX   ((ImguiWindowContext*)(argc > 0 && argv[0].type == KITSUNE_TUSERDATA && argv[0].userdata ? argv[0].userdata->userdata : ud))
// Guard: return a TERROR if ctx is null.
#define IMGUI_REQUIRE_CTX(ctx) \
    do { if (!(ctx)) { \
        KitsuneVariable _e = {}; const char* _m = "renderer: invalid context"; \
        _e.type = KITSUNE_TERROR; _e.data = (unsigned char*)_m; _e.length = strlen(_m); \
        setter(&_e); return 1; \
    } } while(0)

// ---------------------------------------------------------------------------
// Metamethods
// ---------------------------------------------------------------------------

static int imgui_gc(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	ImguiWindowContext* ctx = IMGUI_CTX;
	if (!ctx)
		return 0;

	// Drain any remaining scheduled calls
	while (ctx->scheduledHead) {
		ImguiScheduledCall* call = ctx->scheduledHead;
		ctx->scheduledHead = call->next;
		for (int i = 0; i < call->argc; i++)
			KitsuneVariableFree(call->argv[i]);
		KitsuneVariableFree(call->fn);
		free(call->argv);
		free(call);
	}

	// Release anchored Lua variables
	if (ctx->renderFn) {
		KitsuneVariableFree(ctx->renderFn);
		ctx->renderFn = nullptr;
	}
	if (ctx->context) {
		KitsuneVariableFree(ctx->context);
		ctx->context = nullptr;
	}
	if (ctx->onError) {
		KitsuneVariableFree(ctx->onError);
		ctx->onError = nullptr;
	}

	// SDL2 / ImGui teardown
	if (ctx->imguiContext) {
		if (ctx->window && ctx->glContext)
			SDL_GL_MakeCurrent(ctx->window, ctx->glContext);
		ImGui::SetCurrentContext((ImGuiContext*)ctx->imguiContext);
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext((ImGuiContext*)ctx->imguiContext);
		ctx->imguiContext = nullptr;
	}

	if (ctx->glContext) {
		SDL_GL_DeleteContext(ctx->glContext);
		ctx->glContext = nullptr;
	}

	if (ctx->window) {
		SDL_DestroyWindow(ctx->window);
		ctx->window = nullptr;
	}

	SDL_Quit();

	// Free heap buffers (may already be null if free_ctx ran first)
	FreeMarkdownCache(ctx);
	free(ctx->inputBuf);  ctx->inputBuf = nullptr;
	free(ctx->title);     ctx->title = nullptr;
	free(ctx);

	extern ImguiWindowContext* g_imguiCtx;
	g_imguiCtx = nullptr;

	return 0;
}

static int imgui_tostring(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	char buf[64];
	snprintf(buf, sizeof(buf), "OpenGL3 / Dear ImGui %s", ImGui::GetVersion());
	KitsuneVariable r = {};
	r.type = KITSUNE_TSTRING;
	r.data = (unsigned char*)buf;
	r.length = strlen(buf);
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// Hand-written overrides: Text variants
// These take a format string + varargs in C++ but in Lua we just take the
// already-formatted string and pass it to the Unformatted variants.
// ---------------------------------------------------------------------------

static int ImguiRenderer_Text(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	const int _argc = IMGUI_ARGC;
	const KitsuneVariable* _argv = IMGUI_ARGV;
	if (_argc < 1) return 0;
	KitsuneVariable* owned = nullptr;
	const char* str = nullptr;
	if (_argv[0].type == KITSUNE_TSTRING) {
		str = (const char*)_argv[0].data;
	}
	else {
		owned = KitsuneToString(&_argv[0]);
		str = owned ? (const char*)owned->data : "";
	}
	ImGui::TextUnformatted(str);
	if (owned) KitsuneVariableFree(owned);
	return 0;
}

static int ImguiRenderer_TextColored(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	const int _argc = IMGUI_ARGC;
	const KitsuneVariable* _argv = IMGUI_ARGV;
	if (_argc < 5) return 0;
	ImVec4 col(KitsuneAsFloat(&_argv[0], 1.0f), KitsuneAsFloat(&_argv[1], 1.0f),
		KitsuneAsFloat(&_argv[2], 1.0f), KitsuneAsFloat(&_argv[3], 1.0f));
	KitsuneVariable* owned = nullptr;
	const char* str = nullptr;
	if (_argv[4].type == KITSUNE_TSTRING) {
		str = (const char*)_argv[4].data;
	}
	else {
		owned = KitsuneToString(&_argv[4]);
		str = owned ? (const char*)owned->data : "";
	}
	ImGui::TextColored(col, "%s", str);
	if (owned) KitsuneVariableFree(owned);
	return 0;
}

static int ImguiRenderer_TextDisabled(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	const int _argc = IMGUI_ARGC;
	const KitsuneVariable* _argv = IMGUI_ARGV;
	if (_argc < 1) return 0;
	KitsuneVariable* owned = nullptr;
	const char* str = nullptr;
	if (_argv[0].type == KITSUNE_TSTRING) {
		str = (const char*)_argv[0].data;
	}
	else {
		owned = KitsuneToString(&_argv[0]);
		str = owned ? (const char*)owned->data : "";
	}
	ImGui::TextDisabled("%s", str);
	if (owned) KitsuneVariableFree(owned);
	return 0;
}

static int ImguiRenderer_TextWrapped(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	const int _argc = IMGUI_ARGC;
	const KitsuneVariable* _argv = IMGUI_ARGV;
	if (_argc < 1) return 0;
	KitsuneVariable* owned = nullptr;
	const char* str = nullptr;
	if (_argv[0].type == KITSUNE_TSTRING) {
		str = (const char*)_argv[0].data;
	}
	else {
		owned = KitsuneToString(&_argv[0]);
		str = owned ? (const char*)owned->data : "";
	}
	ImGui::TextWrapped("%s", str);
	if (owned) KitsuneVariableFree(owned);
	return 0;
}

static int ImguiRenderer_BulletText(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	const int _argc = IMGUI_ARGC;
	const KitsuneVariable* _argv = IMGUI_ARGV;
	if (_argc < 1) return 0;
	KitsuneVariable* owned = nullptr;
	const char* str = nullptr;
	if (_argv[0].type == KITSUNE_TSTRING) {
		str = (const char*)_argv[0].data;
	}
	else {
		owned = KitsuneToString(&_argv[0]);
		str = owned ? (const char*)owned->data : "";
	}
	ImGui::BulletText("%s", str);
	if (owned) KitsuneVariableFree(owned);
	return 0;
}

// ---------------------------------------------------------------------------
// Hand-written override: InputText
// ---------------------------------------------------------------------------

static int ImguiRenderer_InputText(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	ImguiWindowContext* ctx = IMGUI_CTX;
	IMGUI_REQUIRE_CTX(ctx);
	const int _argc = IMGUI_ARGC;
	const KitsuneVariable* _argv = IMGUI_ARGV;

	if (_argc < 2) {
		KitsuneVariable err = {};
		const char* msg = "InputText requires label and value arguments";
		err.type = KITSUNE_TERROR; err.data = (unsigned char*)msg; err.length = strlen(msg);
		setter(&err);
		return 1;
	}

	size_t needed = (_argc > 2 ? (size_t)_argv[2].integer : 256) + 1;

	const char* value = nullptr;
	KitsuneVariable* valueOwned = nullptr;
	if (_argv[1].type == KITSUNE_TSTRING) {
		value = (const char*)_argv[1].data;
		if (_argv[1].length + 1 > needed) needed = _argv[1].length + 1;
	}
	else {
		valueOwned = KitsuneToString(&_argv[1]);
		value = valueOwned ? (const char*)valueOwned->data : "";
		if (valueOwned && valueOwned->length + 1 > needed) needed = valueOwned->length + 1;
	}

	if (needed > ctx->inputBufSize) {
		char* grown = (char*)realloc(ctx->inputBuf, needed);
		if (!grown) { if (valueOwned) KitsuneVariableFree(valueOwned); return 0; }
		ctx->inputBuf = grown;
		ctx->inputBufSize = needed;
	}

	size_t vlen = value ? strlen(value) : 0;
	if (vlen >= ctx->inputBufSize) vlen = ctx->inputBufSize - 1;
	if (vlen > 0) memcpy(ctx->inputBuf, value, vlen);
	ctx->inputBuf[vlen] = '\0';
	if (valueOwned) KitsuneVariableFree(valueOwned);

	const char* label = _argv[0].type == KITSUNE_TSTRING
		? (const char*)_argv[0].data : "";
	KitsuneVariable* labelOwned = nullptr;
	if (_argv[0].type != KITSUNE_TSTRING) {
		labelOwned = KitsuneToString(&_argv[0]);
		label = labelOwned ? (const char*)labelOwned->data : "";
	}

	bool changed = ImGui::InputText(label, ctx->inputBuf, ctx->inputBufSize);

	if (labelOwned) KitsuneVariableFree(labelOwned);

	KitsuneVariable r1 = {}; r1.type = KITSUNE_TBOOLEAN; r1.boolean = changed;
	KitsuneVariable r2 = {}; r2.type = KITSUNE_TSTRING;
	r2.data = (unsigned char*)ctx->inputBuf;
	r2.length = strlen(ctx->inputBuf);
	setter(&r1);
	setter(&r2);
	return 2;
}

// ---------------------------------------------------------------------------
// Hand-written override: InputTextMultiline
// ---------------------------------------------------------------------------

static int ImguiRenderer_InputTextMultiline(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	ImguiWindowContext* ctx = IMGUI_CTX;
	IMGUI_REQUIRE_CTX(ctx);
	const int _argc = IMGUI_ARGC;
	const KitsuneVariable* _argv = IMGUI_ARGV;

	if (_argc < 2) {
		KitsuneVariable err = {};
		const char* msg = "InputTextMultiline requires label and value arguments";
		err.type = KITSUNE_TERROR; err.data = (unsigned char*)msg; err.length = strlen(msg);
		setter(&err);
		return 1;
	}

	size_t needed = (_argc > 4 ? (size_t)_argv[4].integer : 256) + 1;

	const char* value = nullptr;
	KitsuneVariable* valueOwned = nullptr;
	if (_argv[1].type == KITSUNE_TSTRING) {
		value = (const char*)_argv[1].data;
		if (_argv[1].length + 1 > needed) needed = _argv[1].length + 1;
	}
	else {
		valueOwned = KitsuneToString(&_argv[1]);
		value = valueOwned ? (const char*)valueOwned->data : "";
		if (valueOwned && valueOwned->length + 1 > needed) needed = valueOwned->length + 1;
	}

	if (needed > ctx->inputBufSize) {
		char* grown = (char*)realloc(ctx->inputBuf, needed);
		if (!grown) { if (valueOwned) KitsuneVariableFree(valueOwned); return 0; }
		ctx->inputBuf = grown;
		ctx->inputBufSize = needed;
	}

	size_t vlen = value ? strlen(value) : 0;
	if (vlen >= ctx->inputBufSize) vlen = ctx->inputBufSize - 1;
	if (vlen > 0) memcpy(ctx->inputBuf, value, vlen);
	ctx->inputBuf[vlen] = '\0';
	if (valueOwned) KitsuneVariableFree(valueOwned);

	float w = _argc > 2 ? KitsuneAsFloat(&_argv[2], 0.0f) : 0.0f;
	float h = _argc > 3 ? KitsuneAsFloat(&_argv[3], 0.0f) : 0.0f;

	const char* label = nullptr;
	KitsuneVariable* labelOwned = nullptr;
	if (_argv[0].type == KITSUNE_TSTRING) {
		label = (const char*)_argv[0].data;
	}
	else {
		labelOwned = KitsuneToString(&_argv[0]);
		label = labelOwned ? (const char*)labelOwned->data : "";
	}

	bool changed = ImGui::InputTextMultiline(label, ctx->inputBuf, ctx->inputBufSize,
		ImVec2(w, h));

	if (labelOwned) KitsuneVariableFree(labelOwned);

	KitsuneVariable r1 = {}; r1.type = KITSUNE_TBOOLEAN; r1.boolean = changed;
	KitsuneVariable r2 = {}; r2.type = KITSUNE_TSTRING;
	r2.data = (unsigned char*)ctx->inputBuf;
	r2.length = strlen(ctx->inputBuf);
	setter(&r1);
	setter(&r2);
	return 2;
}

// ---------------------------------------------------------------------------
// Hand-written override: PlotLines
// ---------------------------------------------------------------------------

static int ImguiRenderer_PlotLines(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	const int _argc = IMGUI_ARGC;
	const KitsuneVariable* _argv = IMGUI_ARGV;
	if (_argc < 2)
		return 0;

	const char* label = nullptr;
	KitsuneVariable* labelOwned = nullptr;
	if (_argv[0].type == KITSUNE_TSTRING) {
		label = (const char*)_argv[0].data;
	}
	else {
		labelOwned = KitsuneToString(&_argv[0]);
		label = labelOwned ? (const char*)labelOwned->data : "";
	}

	// _argv[1] must be a table of floats
	int values_count = 0;
	float* values = nullptr;
	if (_argv[1].type == KITSUNE_TTABLE) {
		KitsuneVariable* lenVar = KitsuneGetLength(&_argv[1]);
		values_count = lenVar ? (int)lenVar->integer : 0;
		KitsuneVariableFree(lenVar);
		if (values_count > 0) {
			values = (float*)alloca(values_count * sizeof(float));
			for (int k = 0; k < values_count; k++) {
				KitsuneVariable ki = {}; ki.type = KITSUNE_TINTEGER; ki.integer = k + 1;
				KitsuneVariable* kv = KitsuneGetIndex(&_argv[1], &ki);
				values[k] = kv ? KitsuneAsFloat(kv, 0.0f) : 0.0f;
				KitsuneVariableFree(kv);
			}
		}
	}

	const char* overlay = _argc > 2 && _argv[2].type == KITSUNE_TSTRING ? (const char*)_argv[2].data : nullptr;
	ImGui::PlotLines(label, values, values_count, 0, overlay);

	if (labelOwned) KitsuneVariableFree(labelOwned);
	return 0;
}

// ---------------------------------------------------------------------------
// Hand-written override: Combo (string list variant)
// ---------------------------------------------------------------------------

static int ImguiRenderer_Combo(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	const int _argc = IMGUI_ARGC;
	const KitsuneVariable* _argv = IMGUI_ARGV;
	if (_argc < 3)
		return 0;

	const char* label = nullptr;
	KitsuneVariable* labelOwned = nullptr;
	if (_argv[0].type == KITSUNE_TSTRING) {
		label = (const char*)_argv[0].data;
	}
	else {
		labelOwned = KitsuneToString(&_argv[0]);
		label = labelOwned ? (const char*)labelOwned->data : "";
	}

	int currentItem = (int)KitsuneAsInt(&_argv[1], 0);

	int count = 0;
	KitsuneVariable** itemVars = nullptr;
	const char** items = nullptr;
	if (_argv[2].type == KITSUNE_TTABLE) {
		KitsuneVariable* lenVar = KitsuneGetLength(&_argv[2]);
		count = lenVar ? (int)lenVar->integer : 0;
		KitsuneVariableFree(lenVar);
		if (count > 0) {
			itemVars = (KitsuneVariable**)alloca(count * sizeof(KitsuneVariable*));
			items = (const char**)alloca(count * sizeof(const char*));
			for (int k = 0; k < count; k++) {
				KitsuneVariable ki = {}; ki.type = KITSUNE_TINTEGER; ki.integer = k + 1;
				itemVars[k] = KitsuneGetIndex(&_argv[2], &ki);
				items[k] = (itemVars[k] && itemVars[k]->type == KITSUNE_TSTRING)
					? (const char*)itemVars[k]->data : "";
			}
		}
	}

	bool changed = ImGui::Combo(label, &currentItem, items, count);

	for (int k = 0; k < count; k++)
		KitsuneVariableFree(itemVars[k]);

	if (labelOwned)
		KitsuneVariableFree(labelOwned);

	KitsuneVariable r1 = {}; r1.type = KITSUNE_TBOOLEAN; r1.boolean = changed;
	KitsuneVariable r2 = {}; r2.type = KITSUNE_TINTEGER;  r2.integer = currentItem;
	setter(&r1);
	setter(&r2);
	return 2;
}

// ---------------------------------------------------------------------------
// Hand-written override: ListBox (string list variant)
// ---------------------------------------------------------------------------

static int ImguiRenderer_ListBox(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	const int _argc = IMGUI_ARGC;
	const KitsuneVariable* _argv = IMGUI_ARGV;
	if (_argc < 3)
		return 0;

	const char* label = nullptr;
	KitsuneVariable* labelOwned = nullptr;
	if (_argv[0].type == KITSUNE_TSTRING) {
		label = (const char*)_argv[0].data;
	}
	else {
		labelOwned = KitsuneToString(&_argv[0]);
		label = labelOwned ? (const char*)labelOwned->data : "";
	}

	int currentItem = (int)KitsuneAsInt(&_argv[1], 0);

	int count = 0;
	KitsuneVariable** itemVars = nullptr;
	const char** items = nullptr;
	if (_argv[2].type == KITSUNE_TTABLE) {
		KitsuneVariable* lenVar = KitsuneGetLength(&_argv[2]);
		count = lenVar ? (int)lenVar->integer : 0;
		KitsuneVariableFree(lenVar);
		if (count > 0) {
			itemVars = (KitsuneVariable**)alloca(count * sizeof(KitsuneVariable*));
			items = (const char**)alloca(count * sizeof(const char*));
			for (int k = 0; k < count; k++) {
				KitsuneVariable ki = {}; ki.type = KITSUNE_TINTEGER; ki.integer = k + 1;
				itemVars[k] = KitsuneGetIndex(&_argv[2], &ki);
				items[k] = (itemVars[k] && itemVars[k]->type == KITSUNE_TSTRING)
					? (const char*)itemVars[k]->data : "";
			}
		}
	}

	bool changed = ImGui::ListBox(label, &currentItem, items, count);

	for (int k = 0; k < count; k++)
		KitsuneVariableFree(itemVars[k]);

	if (labelOwned) KitsuneVariableFree(labelOwned);

	KitsuneVariable r1 = {}; r1.type = KITSUNE_TBOOLEAN; r1.boolean = changed;
	KitsuneVariable r2 = {}; r2.type = KITSUNE_TINTEGER;  r2.integer = currentItem;
	setter(&r1);
	setter(&r2);
	return 2;
}

// ---------------------------------------------------------------------------
// MarkdownRender
// renderer:MarkdownRender(stream [, refresh [, w [, h]]])
// ---------------------------------------------------------------------------

static int ImguiRenderer_MarkdownRender(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	ImguiWindowContext* ctx = IMGUI_CTX;
	IMGUI_REQUIRE_CTX(ctx);

	// argv[0] = self, argv[1] = stream
	if (IMGUI_ARGC < 1 || argv[1].type != KITSUNE_TUSERDATA || !argv[1].userdata
		|| !argv[1].userdata->name || strcmp(argv[1].userdata->name, "STREAM") != 0) {
		KitsuneVariable e = {}; const char* m = "MarkdownRender: expected stream userdata";
		e.type = KITSUNE_TERROR; e.data = (unsigned char*)m; e.length = strlen(m);
		setter(&e); return 1;
	}

	// Validate it has Seek and Read by calling stream:Id() — if Id() returns a number
	// we know it's a stream. Also gives us the cache key.
	KitsuneVariable* idVar = KitsuneCallMethod(&argv[1], "Id", 0, nullptr);
	if (!idVar || (idVar->type != KITSUNE_TINTEGER && idVar->type != KITSUNE_TNUMBER)) {
		KitsuneVariableFree(idVar);
		KitsuneVariable e = {}; const char* m = "MarkdownRender: argument is not a stream";
		e.type = KITSUNE_TERROR; e.data = (unsigned char*)m; e.length = strlen(m);
		setter(&e); return 1;
	}
	uint64_t id = (idVar->type == KITSUNE_TINTEGER)
		? (uint64_t)idVar->integer
		: (uint64_t)idVar->number;
	KitsuneVariableFree(idVar);

	bool  refresh = IMGUI_ARGC >= 2 && argv[2].type == KITSUNE_TBOOLEAN && argv[2].boolean;
	float w = IMGUI_ARGC >= 3 ? (float)KitsuneAsDouble(&argv[3], 0.0) : 0.0f;
	float h = IMGUI_ARGC >= 4 ? (float)KitsuneAsDouble(&argv[4], 0.0) : 0.0f;

	if (refresh || id != ctx->mdCacheId) {
		FreeMarkdownCache(ctx);
		ReadStreamIntoCache(&argv[1], ctx);
		if (!ctx->mdCacheError)
			ParseContentIntoNodes(ctx);
		ctx->mdCacheId = id;
	}

	RenderFromNodes(ctx, w, h);
	return 0;
}

// ---------------------------------------------------------------------------
// Registration helpers
// ---------------------------------------------------------------------------

static void prepend_meta(KitsuneUserDataRegistration* reg,
	const char* name, kitsune_CFunction func) {
	KitsuneNamedFunction* node = (KitsuneNamedFunction*)calloc(1, sizeof(KitsuneNamedFunction));
	if (!node) return;
	node->name = (char*)name;
	node->func = func;
	node->userdata = nullptr;
	node->Next = reg->MetaTableFunctions;
	reg->MetaTableFunctions = node;
}

static void prepend_fn(KitsuneUserDataRegistration* reg,
	const char* name, kitsune_CFunction func) {
	KitsuneNamedFunction* node = (KitsuneNamedFunction*)calloc(1, sizeof(KitsuneNamedFunction));
	if (!node) return;
	node->name = (char*)name;
	node->func = func;
	node->userdata = nullptr;
	node->Next = reg->Functions;
	reg->Functions = node;
}

static int ImguiRenderer_PushFont(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	const int _argc = argc - 1;
	const KitsuneVariable* _argv = argc > 0 ? argv + 1 : argv;
	ImFont* font = nullptr;
	if (_argc >= 1 && _argv[0].type == KITSUNE_TINTEGER) {
		int luaId = (int)KitsuneAsInt(&_argv[0], 0);
		if (luaId > 0) {
			FontResource* res = (FontResource*)ResourceCacheGetById(luaId, RESOURCE_FONT);
			if (res)
				font = res->imFont;
		}
	}
	FontPush(font);
	return 0;
}

void add_imgui_meta_bindings(KitsuneUserDataRegistration* reg) {
	prepend_meta(reg, "__gc", imgui_gc);
	prepend_meta(reg, "__tostring", imgui_tostring);

	prepend_fn(reg, "Text", ImguiRenderer_Text);
	prepend_fn(reg, "TextColored", ImguiRenderer_TextColored);
	prepend_fn(reg, "TextDisabled", ImguiRenderer_TextDisabled);
	prepend_fn(reg, "TextWrapped", ImguiRenderer_TextWrapped);
	prepend_fn(reg, "BulletText", ImguiRenderer_BulletText);
	prepend_fn(reg, "InputText", ImguiRenderer_InputText);
	prepend_fn(reg, "InputTextMultiline", ImguiRenderer_InputTextMultiline);
	prepend_fn(reg, "PlotLines", ImguiRenderer_PlotLines);
	prepend_fn(reg, "Combo", ImguiRenderer_Combo);
	prepend_fn(reg, "ListBox", ImguiRenderer_ListBox);
	prepend_fn(reg, "PushFont", ImguiRenderer_PushFont);
	prepend_fn(reg, "Html", ImguiRenderer_Html);
	prepend_fn(reg, "MarkdownRender", ImguiRenderer_MarkdownRender);
	prepend_fn(reg, "Image", ImguiRenderer_Image);
	prepend_fn(reg, "ImageFrame", ImguiRenderer_ImageFrame);
}

// ---------------------------------------------------------------------------
// renderer:Image(handle, width, height [, uv0x, uv0y, uv1x, uv1y])
// ---------------------------------------------------------------------------

int ImguiRenderer_Image(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	const int              _argc = IMGUI_ARGC;
	const KitsuneVariable* _argv = IMGUI_ARGV;
	ImguiWindowContext* ctx = IMGUI_CTX;
	IMGUI_REQUIRE_CTX(ctx);
	if (_argc < 3)
		return 0;

	int   luaId = (int)KitsuneAsInt(&_argv[0], 0);
	float w = KitsuneAsFloat(&_argv[1], 0.0f);
	float h = KitsuneAsFloat(&_argv[2], 0.0f);

	unsigned int glId = ResolveTextureGlId(luaId);
	ImVec2 uv0(
		_argc > 4 ? KitsuneAsFloat(&_argv[3], 0.0f) : 0.0f,
		_argc > 4 ? KitsuneAsFloat(&_argv[4], 0.0f) : 0.0f);
	ImVec2 uv1(
		_argc > 6 ? KitsuneAsFloat(&_argv[5], 1.0f) : 1.0f,
		_argc > 6 ? KitsuneAsFloat(&_argv[6], 1.0f) : 1.0f);
	ImGui::Image((ImTextureID)(uintptr_t)glId, ImVec2(w, h), uv0, uv1);
	return 0;
}

// ---------------------------------------------------------------------------
// renderer:ImageFrame(id, w, h, frameIndex, cols, rows [, flipX [, flipY]])
// ---------------------------------------------------------------------------

int ImguiRenderer_ImageFrame(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	const int              _argc = IMGUI_ARGC;
	const KitsuneVariable* _argv = IMGUI_ARGV;
	ImguiWindowContext* ctx = IMGUI_CTX;
	IMGUI_REQUIRE_CTX(ctx);
	if (_argc < 6)
		return 0;

	int   luaId = (int)KitsuneAsInt(&_argv[0], 0);
	float w = KitsuneAsFloat(&_argv[1], 0.0f);
	float h = KitsuneAsFloat(&_argv[2], 0.0f);
	int   frameIndex = (int)KitsuneAsInt(&_argv[3], 1);
	int   cols = (int)KitsuneAsInt(&_argv[4], 1);
	int   rows = (int)KitsuneAsInt(&_argv[5], 1);
	bool  flipX = _argc > 6 ? KitsuneAsBool(&_argv[6]) : false;
	bool  flipY = _argc > 7 ? KitsuneAsBool(&_argv[7]) : false;

	if (cols <= 0 || rows <= 0 || frameIndex < 1 || frameIndex > cols * rows)
		return 0;

	int col = (frameIndex - 1) % cols;
	int row = (frameIndex - 1) / cols;

	float u0 = (float)col / (float)cols;
	float v0 = (float)row / (float)rows;
	float u1 = (float)(col + 1) / (float)cols;
	float v1 = (float)(row + 1) / (float)rows;

	if (flipX) {
		float tmp = u0;
		u0 = u1;
		u1 = tmp;
	}
	if (flipY) {
		float tmp = v0;
		v0 = v1;
		v1 = tmp;
	}

	unsigned int glId = ResolveTextureGlId(luaId);
	ImGui::Image((ImTextureID)(uintptr_t)glId, ImVec2(w, h), ImVec2(u0, v0), ImVec2(u1, v1));
	return 0;
}

#endif // KITSUNE_IMGUI
