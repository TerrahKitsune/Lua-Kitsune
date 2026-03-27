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
	int id = KitsuneExecuteFile(file, argc, (const char**)argv);

	if (id < 0) {
		fprintf(stderr, "Failed to start %s\n", file);
		KitsuneCleanup();
		return -1;
	}

	// Block until the coroutine finishes and get the result length.
	size_t resultLen = 0;
	while (!KitsuneHasResult(id, &resultLen))
		Sleep(1);

	const char* err = KitsuneGetError(id);
	if (err)
		fprintf(stderr, "%s\n", err);

	int ret = err ? 1 : 0;

	if (resultLen > 0) {
		char* result = (char*)malloc(resultLen + 1);
		if (result) {
			KitsuneGetResult(id, result, resultLen + 1);  // also releases the slot
			printf("%.*s\n", (int)resultLen, result);
			free(result);
		} else {
			KitsuneGetResult(id, nullptr, 0);  // release the slot even on malloc failure
		}
	} else {
		KitsuneGetResult(id, nullptr, 0);  // always release the slot
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
