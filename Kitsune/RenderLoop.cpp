#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef KITSUNE_IMGUI

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif

#include "RenderLoop.h"
#include "ImguiSession.h"
#include "ImguiRenderer.h"
#include "ImguiEnums.h"
#include "OpenGL.h"
#include "Font.h"
#include "ImguiHtml.h"
#include "SDLAudio.h"
#include "ResourceCache.h"
#include "SDLInput.h"

#include "Imgui/imgui.h"
#include "Imgui/imgui_internal.h"
#include "Imgui/imgui_impl_sdl2.h"
#include "Imgui/imgui_impl_opengl3.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

// ---------------------------------------------------------------------------
// Scheduled call helpers
// ---------------------------------------------------------------------------

// Single function handles both submission and completion polling.
// Calls with runningId==0 are submitted (fireAndForget=false so we own the result).
// Calls with runningId>0 are polled; finished ones invoke onError on fault then are freed.
// Long-running coroutines stay on the list until they finish.
static void drain_imgui_stack(ImguiWindowContext* ctx);

static void free_ctx(ImguiWindowContext* ctx) {
	if (!ctx)
		return;
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
	drain_imgui_stack(ctx);
	free(ctx->inputBuf);
	ctx->inputBuf = nullptr;
	ctx->inputBufSize = 0;
	free(ctx->title);
	ctx->title = nullptr;
	free(ctx);
	g_imguiCtx = nullptr;
}

static void drain_imgui_stack(ImguiWindowContext* ctx) {
	ImguiStackEntry* node = ctx->stackHead;
	while (node) {
		ImguiStackEntry* next = node->next;
		if (node->finalizer)
			node->finalizer(node->data);
		else
			free(node->data);
		free(node);
		node = next;
	}
	ctx->stackHead = nullptr;
}

bool ImguiStackPush(int id, void* data, ImguiStackEntryFinalizer fn) {
	if (!g_imguiCtx)
		return false;
	ImguiStackEntry* node = (ImguiStackEntry*)malloc(sizeof(ImguiStackEntry));
	if (!node)
		return false;
	node->id = id;
	node->data = data;
	node->finalizer = fn;
	node->prev = nullptr;
	node->next = g_imguiCtx->stackHead;
	if (g_imguiCtx->stackHead)
		g_imguiCtx->stackHead->prev = node;
	g_imguiCtx->stackHead = node;
	return true;
}

bool ImguiStackPop(int id) {
	if (!g_imguiCtx)
		return false;
	ImguiStackEntry* node = g_imguiCtx->stackHead;
	while (node) {
		if (node->id == id) {
			if (node->prev)
				node->prev->next = node->next;
			else
				g_imguiCtx->stackHead = node->next;
			if (node->next)
				node->next->prev = node->prev;
			if (node->finalizer)
				node->finalizer(node->data);
			else
				free(node->data);
			free(node);
			return true;
		}
		node = node->next;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Imgui.Start
// ---------------------------------------------------------------------------

static int ImguiStart(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* userdata) {
	if (g_imguiCtx) {
		KitsuneVariable err = {};
		const char* msg = "Imgui.Start already called";
		err.type = KITSUNE_TERROR;
		err.data = (unsigned char*)msg;
		err.length = strlen(msg);
		setter(&err);
		return 1;
	}

	if (argc < 4
		|| argv[0].type != KITSUNE_TSTRING
		|| argv[3].type != KITSUNE_TFUNCTION) {
		KitsuneVariable err = {};
		const char* msg = "Imgui.Start(title, width, height, renderFn, [context], [onError])";
		err.type = KITSUNE_TERROR;
		err.data = (unsigned char*)msg;
		err.length = strlen(msg);
		setter(&err);
		return 1;
	}

	ImguiWindowContext* ctx = (ImguiWindowContext*)calloc(1, sizeof(ImguiWindowContext));
	if (!ctx)
		return 0;

	ctx->title = (char*)malloc(argv[0].length + 1);
	if (!ctx->title) {
		free(ctx);
		return 0;
	}
	memcpy(ctx->title, argv[0].data, argv[0].length);
	ctx->title[argv[0].length] = '\0';

	ctx->width = argc > 1 ? (int)KitsuneAsInt(&argv[1], 800) : 800;
	ctx->height = argc > 2 ? (int)KitsuneAsInt(&argv[2], 600) : 600;

	ctx->renderFn = KitsuneAnchorVariable(&argv[3]);

	if (argc > 4 && argv[4].type == KITSUNE_TTABLE)
		ctx->context = KitsuneAnchorVariable(&argv[4]);

	if (argc > 5 && argv[5].type == KITSUNE_TFUNCTION)
		ctx->onError = KitsuneAnchorVariable(&argv[5]);

	ctx->inputBufSize = 256;
	ctx->inputBuf = (char*)malloc(ctx->inputBufSize);
	if (ctx->inputBuf)
		ctx->inputBuf[0] = '\0';

	g_imguiCtx = ctx;

	KitsuneVariable nilVar = {};
	nilVar.type = KITSUNE_TNIL;
	KitsuneSetVariable("Imgui.Start", &nilVar);

	return 0;
}

// ---------------------------------------------------------------------------
// RunImguiSession
// ---------------------------------------------------------------------------

void RunImguiSession() {
	ImguiWindowContext* ctx = g_imguiCtx;
	if (!ctx)
		return;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		free_ctx(ctx);
		return;
	}

	ResourceCacheInit();

	if (!SDLAudioInit())
		fprintf(stderr, "SDLAudioInit failed - audio will be unavailable\n");

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
	ctx->window = SDL_CreateWindow(ctx->title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		ctx->width, ctx->height, windowFlags);
	if (!ctx->window) {
		fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		SDL_Quit();
		free_ctx(ctx);
		return;
	}

	ctx->glContext = SDL_GL_CreateContext(ctx->window);
	if (!ctx->glContext) {
		fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
		SDL_DestroyWindow(ctx->window);
		ctx->window = nullptr;
		SDL_Quit();
		free_ctx(ctx);
		return;
	}
	SDL_GL_MakeCurrent(ctx->window, ctx->glContext);
	SDL_GL_SetSwapInterval(1);

	add_imgui_meta_bindings(&ctx->reg);
	add_imgui_bindings(&ctx->reg);
	KitsuneRegisterUserdata("ImguiRenderer", &ctx->reg);

	auto free_reg_nodes = [](KitsuneNamedFunction*& head) {
		KitsuneNamedFunction* n = head;
		while (n) {
			KitsuneNamedFunction* next = n->Next;
			free(n);
			n = next;
		}
		head = nullptr;
		};
	free_reg_nodes(ctx->reg.Functions);
	free_reg_nodes(ctx->reg.MetaTableFunctions);

	IMGUI_CHECKVERSION();
	ImGuiContext* imCtx = ImGui::CreateContext();
	ctx->imguiContext = imCtx;
	ImGui::SetCurrentContext(imCtx);
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui_ImplSDL2_InitForOpenGL(ctx->window, ctx->glContext);
	ImGui_ImplOpenGL3_Init("#version 130");

	ImGui::GetIO().ConfigErrorRecoveryEnableAssert = false;
	ImGui::GetIO().ConfigErrorRecoveryEnableDebugLog = false;

	KitsuneUserData rendererUD = {};
	rendererUD.name = (char*)"ImguiRenderer";
	rendererUD.ref = 0;
	rendererUD.userdata = ctx;
	KitsuneVariable rendererVar = {};
	rendererVar.type = KITSUNE_TUSERDATA;
	rendererVar.length = strlen(rendererUD.name);
	rendererVar.userdata = &rendererUD;
	KitsuneVariable* anchoredRenderer = KitsuneAnchorVariable(&rendererVar);

	bool running = (anchoredRenderer != nullptr);
	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL2_ProcessEvent(&event);
			if (event.type == SDL_QUIT)
				running = false;
		}

		if (!running)
			break;

		UpdateFrameTiming();

		if (FontAtlasRebuildPending()) {
			FontLoadPending();
			ImGui::GetIO().Fonts->Build();
			ImGui_ImplOpenGL3_CreateFontsTexture();
			FontClearRebuildFlag();
			HtmlInvalidateAll();
		}

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		KitsuneVariable args[2] = {};
		args[0] = *anchoredRenderer;
		if (ctx->context) {
			args[1] = *ctx->context;
		}
		else {
			args[1].type = KITSUNE_TNIL;
		}

		KitsuneVariable* result = KitsuneExecuteVariable(ctx->renderFn, 2, args);

		if (result && result->type == KITSUNE_TERROR) {
			if (ctx->onError) {
				KitsuneVariable* keepRunning = KitsuneExecuteVariable(ctx->onError, 1, result);
				if (!keepRunning || keepRunning->type == KITSUNE_TERROR) {
					fprintf(stderr, "Imgui render error: %.*s\n",
						(int)result->length, (char*)result->data);
					fprintf(stderr, "Imgui onError handler failed: %.*s\n",
						(int)(keepRunning ? keepRunning->length : 0),
						keepRunning ? (char*)keepRunning->data : "");
					KitsuneVariableFree(result);
					KitsuneVariableFree(keepRunning);
					running = false;
				}
				else {
					bool cont = keepRunning->type == KITSUNE_TBOOLEAN && keepRunning->boolean;
					if (!cont) {
						fprintf(stderr, "Imgui render error: %.*s\n",
							(int)result->length, (char*)result->data);
					}
					KitsuneVariableFree(result);
					KitsuneVariableFree(keepRunning);
					if (!cont)
						running = false;
				}
			}
			else {
				fprintf(stderr, "Imgui render error: %.*s\n",
					(int)result->length, (char*)result->data);
				KitsuneVariableFree(result);
				running = false;
			}
		}
		else if (result && result->type == KITSUNE_TBOOLEAN && !result->boolean) {
			KitsuneVariableFree(result);
			running = false;
		}
		else {
			KitsuneVariableFree(result);
		}

		drain_imgui_stack(ctx);

		ImGui::EndFrame();
		ImGui::Render();
		int displayW;
		int displayH;
		SDL_GL_GetDrawableSize(ctx->window, &displayW, &displayH);
		glViewport(0, 0, displayW, displayH);
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(ctx->window);
	}

	// Tear down ImGui and SDL before the resource cache so that font_free can
	// safely free ttfData (the atlas no longer references it after DestroyContext).
	if (ctx->imguiContext) {
		if (ctx->window && ctx->glContext)
			SDL_GL_MakeCurrent(ctx->window, ctx->glContext);
		ImGui::SetCurrentContext((ImGuiContext*)ctx->imguiContext);
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext((ImGuiContext*)ctx->imguiContext);
		ctx->imguiContext = nullptr;
	}

	// Shut down audio before ResourceCacheShutdown so finalizers run while mixer is open.
	SDLAudioShutdown();

	// Free loader callbacks before cache shutdown so no Lua calls fire during finalizers.
	ResourceCacheShutdownLoader();

	// Destroy all live HtmlDocuments before GL/resource teardown.
	HtmlShutdown();

	// Free all resources before GL teardown so finalizers can call glDeleteTextures
	ResourceCacheShutdown();

	if (anchoredRenderer) {
		KitsuneVariableFree(anchoredRenderer);
	}
	else {
		if (ctx->imguiContext) {
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
		free_ctx(ctx);
	}
}

// ---------------------------------------------------------------------------
// RegisterRenderLoopFunctions
// ---------------------------------------------------------------------------

void RegisterRenderLoopFunctions() {
	KitsuneRegisterFunction("Imgui.Start", ImguiStart, nullptr);
}

#endif // KITSUNE_IMGUI
