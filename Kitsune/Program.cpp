#if defined(_WIN32) && defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define KITSUNE_ALL

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>
#include "platform.h"
#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif
#include "Session.h"
#ifdef KITSUNE_IMGUI
#include "ImguiSession.h"
#include "ImguiHtml.h"
#include "litehtml.h"
#endif

// ---------------------------------------------------------------------------
// Global operator new / delete overrides
// Route all C++ allocations in the exe through malloc/free so they are
// tracked by the CRT heap and consistent with the engine's allocator.
// ---------------------------------------------------------------------------
#include <new>
#include <cstdlib>
#ifdef _MSC_VER
#define KITSUNE_ALLOCATOR __declspec(allocator)
#else
#define KITSUNE_ALLOCATOR
#endif
KITSUNE_ALLOCATOR void* operator new(size_t size) { void* p = malloc(size); if (!p) throw std::bad_alloc(); return p; }
KITSUNE_ALLOCATOR void* operator new[](size_t size) { void* p = malloc(size); if (!p) throw std::bad_alloc(); return p; }
KITSUNE_ALLOCATOR void* operator new(size_t size, std::nothrow_t const&) noexcept { return malloc(size); }
KITSUNE_ALLOCATOR void* operator new[](size_t size, std::nothrow_t const&) noexcept { return malloc(size); }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }
void operator delete(void* p, std::nothrow_t const&) noexcept { free(p); }
void operator delete[](void* p, std::nothrow_t const&) noexcept { free(p); }

#ifdef _DEBUG
int Test(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter resultSetter, void* userdata) {

	for (int n = 0; n < argc; n++) {
		if (argv[n].type == KITSUNE_TSTRING && argv[n].data && argv[n].length > 0)
			printf("arg %d: %.*s\n", n, (int)argv[n].length, (char*)argv[n].data);
		else if (argv[n].type == KITSUNE_TNUMBER)
			printf("arg %d: %f\n", n, argv[n].number);
		else if (argv[n].type == KITSUNE_TBOOLEAN)
			printf("arg %d: %s\n", n, argv[n].boolean ? "true" : "false");
		else
			printf("arg %d: (type %d)\n", n, argv[n].type);
	}

	const char* testResult = "This is a test result";

	KitsuneVariable* result = (KitsuneVariable*)malloc(sizeof(KitsuneVariable));
	if (!result) {
		return 0;
	}

	result->type = KITSUNE_TSTRING;
	result->length = strlen(testResult);
	result->data = (unsigned char*)malloc(result->length + 1);
	strcpy((char*)result->data, testResult);

	resultSetter(result);

	if (result->data) {
		free(result->data);
	}
	free(result);

	return 1;
}
#endif

static std::atomic<long> g_exitSignaled{ 0 };

#ifdef _WIN32
BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
	switch (ctrlType) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		g_exitSignaled.store(1);
		KitsuneInterrupt();
		return TRUE;
	}
	return FALSE;
}
#else
static void SigIntHandler(int) {
	g_exitSignaled.store(1);
	KitsuneInterrupt();
}
#endif

int main(int argc, char* argv[]) {

#if defined(_WIN32) && defined(_DEBUG)
	_CrtMemState sOld;
	_CrtMemState sNew;
	_CrtMemState sDiff;
	_CrtMemCheckpoint(&sOld);

#endif

#ifdef _WIN32
	SetConsoleOutputCP(65001);
#endif

	KitsuneInternals* internals = KitsuneGetInternals();

	if (!KitsuneInit()) {
		fprintf(stderr, "KitsuneInit failed\n");
		return -1;
	}
	else {
		RegisterSessionFunctions();
#ifdef KITSUNE_IMGUI
		RegisterImguiFunctions();
#endif
	}
#ifdef _WIN32
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
	signal(SIGINT, SigIntHandler);
	signal(SIGTERM, SigIntHandler);
#endif

	const char* file = argc > 1 ? argv[1] : "main.lua";

	// Convert extra command-line args (argv[2..]) to KitsuneVariable array.
	int extraArgc = argc > 2 ? argc - 2 : 0;
	KitsuneVariable* vars = nullptr;
	if (extraArgc > 0) {
		vars = new KitsuneVariable[extraArgc]();
		for (int i = 0; i < extraArgc; i++) {
			vars[i].type = KITSUNE_TSTRING;
			vars[i].length = strlen(argv[i + 2]);
			vars[i].data = (unsigned char*)argv[i + 2];
		}
	}

#ifdef _DEBUG
	KitsuneRegisterFunction("Test", Test);
#endif

	int id = KitsuneExecuteFileAsync(file, extraArgc, vars);
	delete[] vars;

	if (id < 0) {
		fprintf(stderr, "Failed to start %s\n", file);
		KitsuneCleanup();
		return -1;
	}

	// Block until the coroutine finishes or an exit signal is received.
	while (!KitsuneHasResult(id, nullptr) && !g_exitSignaled.load()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	// If the signal fired before the coroutine reported done, wait for the
	// interrupt to propagate so KitsuneGetError / KitsuneGetResult are valid.
	if (!KitsuneHasResult(id, nullptr))
		KitsuneWait();

	size_t errLen = KitsuneGetError(id, nullptr, 0);
	int ret = errLen > 0 ? 1 : 0;
	if (errLen > 0) {
		char* errBuf = new char[errLen + 1];
		KitsuneGetError(id, errBuf, errLen + 1);
		fprintf(stderr, "%s\n", errBuf);
		delete[] errBuf;
	}

#ifdef KITSUNE_IMGUI
	if (g_imguiCtx)
		RunImguiSession();
	else {
		KitsuneVariable* result = KitsuneGetResult(id);
		if (result) {
			if (result->type == KITSUNE_TSTRING && result->data && result->length > 0)
				printf("%.*s\n", (int)result->length, (char*)result->data);
			else if (result->type == KITSUNE_TNUMBER)
				printf("%f\n", result->number);
			else if (result->type == KITSUNE_TBOOLEAN)
				printf("%s\n", result->boolean ? "true" : "false");
			else if (result->type != KITSUNE_TNIL || result->type != KITSUNE_TNONE)
				printf("(type %d)\n", result->type);
			KitsuneVariableFree(result);
		}
	}
#else
	KitsuneVariable* result = KitsuneGetResult(id);
	if (result) {
		if (result->type == KITSUNE_TSTRING && result->data && result->length > 0)
			printf("%.*s\n", (int)result->length, (char*)result->data);
		else if (result->type == KITSUNE_TNUMBER)
			printf("%f\n", result->number);
		else if (result->type == KITSUNE_TBOOLEAN)
			printf("%s\n", result->boolean ? "true" : "false");
		else if (result->type != KITSUNE_TNIL || result->type != KITSUNE_TNONE)
			printf("(type %d)\n", result->type);
		KitsuneVariableFree(result);
	}
#endif

	KitsuneCleanup();

	if (internals && internals->MongoDbCleanUp)
		internals->MongoDbCleanUp();

#if defined(_WIN32) && defined(_DEBUG) 
	_CrtMemCheckpoint(&sNew);
	if (_CrtMemDifference(&sDiff, &sOld, &sNew)) {
		OutputDebugString("-----------_CrtMemDumpStatistics ---------");
		_CrtMemDumpStatistics(&sDiff);
		OutputDebugString("-----------_CrtMemDumpAllObjectsSince ---------");
		_CrtMemDumpAllObjectsSince(&sOld);
		OutputDebugString("-----------_CrtDumpMemoryLeaks ---------");
		_CrtDumpMemoryLeaks();

		// Several system components allocate per-process global state that is
		// freed on process exit (not on explicit cleanup), producing false-positive
		// CRT leak reports:
		//   - GPU display drivers (e.g. nvoglv64.dll) on first wglCreateContext.
		//   - DXGI (dxgi.dll) on first CreateDXGIFactory / CreateDXGIFactory1 call
		//     (used by LuaHardware GPU queries); its internal adapter cache and COM
		//     apartment state are reference-counted and released at process exit.
		// Only break if the leaks are large enough to indicate a real problem.
		constexpr ptrdiff_t k_driverLeakThreshold = 4 * 1024 * 1024; // 4 MB
		if ((ptrdiff_t)sDiff.lSizes[_NORMAL_BLOCK] > (ptrdiff_t)k_driverLeakThreshold)
			DebugBreak();
	}
#endif

	return ret;
}
