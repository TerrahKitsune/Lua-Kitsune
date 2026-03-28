#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <Windows.h>
#include <crtdbg.h>
#include <cstdio>
#include "KitsuneEngine.h"

int main(int argc, char *argv[]) {

#ifdef _DEBUG
	_CrtMemState sOld;
	_CrtMemState sNew;
	_CrtMemState sDiff;
	_CrtMemCheckpoint(&sOld);
#endif

	SetConsoleOutputCP(65001);

	if (!KitsuneInit()) {
		fprintf(stderr, "KitsuneInit failed\n");
		return -1;
	}

	const char* file = argc > 1 ? argv[1] : "main.lua";

	// Convert extra command-line args (argv[2..]) to KitsuneVariable array.
	int extraArgc = argc > 2 ? argc - 2 : 0;
	KitsuneVariable* vars = nullptr;
	if (extraArgc > 0) {
		vars = new KitsuneVariable[extraArgc]();
		for (int i = 0; i < extraArgc; i++) {
			vars[i].type   = KITSUNE_TSTRING;
			vars[i].length = strlen(argv[i + 2]);
			vars[i].data   = (unsigned char*)argv[i + 2];
		}
	}
	int id = KitsuneExecuteFile(file, extraArgc, vars);
	delete[] vars;

	if (id < 0) {
		fprintf(stderr, "Failed to start %s\n", file);
		KitsuneCleanup();
		return -1;
	}

	// Block until the coroutine finishes.
	while (!KitsuneHasResult(id, nullptr))
		Sleep(1);

	const char* err = KitsuneGetError(id);
	if (err)
		fprintf(stderr, "%s\n", err);

	int ret = err ? 1 : 0;

	KitsuneVariable* result = KitsuneGetResult(id);
	if (result) {
		if (result->type == KITSUNE_TSTRING && result->data && result->length > 0)
			printf("%.*s\n", (int)result->length, (char*)result->data);
		KitsuneVariableFree(result);
	}

	KitsuneCleanup();

#ifdef _DEBUG
	_CrtMemCheckpoint(&sNew);
	if (_CrtMemDifference(&sDiff, &sOld, &sNew)) {
		OutputDebugString("-----------_CrtMemDumpStatistics ---------");
		_CrtMemDumpStatistics(&sDiff);
		OutputDebugString("-----------_CrtMemDumpAllObjectsSince ---------");
		_CrtMemDumpAllObjectsSince(&sOld);
		OutputDebugString("-----------_CrtDumpMemoryLeaks ---------");
		_CrtDumpMemoryLeaks();
		DebugBreak();
	}
#endif

	return ret;
}
