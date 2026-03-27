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

	int ret = 0;

	const char* file = argc > 1 ? argv[1] : "main.lua";
	ret = KitsuneExecuteFile(file, argc, (const char**)argv);

	const char* err = KitsuneGetError();
	if (err)
		fprintf(stderr, "%s\n", err);

	size_t resultLen = KitsuneHasResult();
	if (resultLen > 0) {
		char* result = (char*)malloc(resultLen + 1);
		if (result) {
			KitsuneGetResult(result, resultLen + 1);
			printf("%.*s\n", (int)resultLen, result);
			free(result);
		}
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
