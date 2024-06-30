#include "luatts.h"
#include <process.h>
#include "luawchar.h"
#include "sphelper.h"

DWORD WINAPI ThreadFunc(LPVOID arg) {

	LuaTTS* tts = (LuaTTS*)arg;
	DWORD waitResult = WAIT_TIMEOUT;
	if (FAILED(CoInitialize(NULL))) {
		tts->alive = false;
		return 0;
	}

	while (tts->alive) {

		if (waitResult != WAIT_TIMEOUT && waitResult != WAIT_OBJECT_0) {
			printf("Thread %08X encountered an error: %lu\n", (DWORD)tts, GetLastError());
		}

		if (tts->speaking && tts->textToSpeak) {
			tts->pVoice->Speak(tts->textToSpeak, SPF_PURGEBEFORESPEAK | tts->flags, NULL);
			gff_free(tts->textToSpeak);
			tts->textToSpeak = NULL;
			tts->speaking = false;
		}

		DWORD waitResult = WaitForSingleObject(tts->hEvent, 5000);
	}

	CoUninitialize();
	return 0;
}

int GetIsSpeaking(lua_State* L) {

	LuaTTS* tts = lua_totts(L, -1);

	lua_pushboolean(L, tts->speaking == true && tts->alive);

	return 1;
}

int Skip(lua_State* L) {

	LuaTTS* tts = lua_totts(L, -1);

	lua_pushboolean(L, tts->pVoice->Skip(L"SENTENCE", 1, NULL) == S_OK);

	return 1;
}

int PlayPause(lua_State* L) {

	LuaTTS* tts = lua_totts(L, -1);

	if (tts->speaking) {

		if (tts->paused) {
			tts->pVoice->Resume();
			tts->paused = false;
		}
		else {
			tts->pVoice->Pause();
			tts->paused = true;
		}
	}

	return 0;
}

int GetIsPaused(lua_State* L) {

	LuaTTS* tts = lua_totts(L, -1);
	lua_pushboolean(L, tts->paused);
	return 1;
}


int SetVolume(lua_State* L) {

	LuaTTS* tts = lua_totts(L, 1);
	if (SUCCEEDED(tts->pVoice->SetVolume((USHORT)luaL_checkinteger(L, 2)))) {
		lua_pushboolean(L, true);
	}
	else {
		lua_pushboolean(L, false);
	}

	return 1;
}

int GetVolume(lua_State* L) {
	LuaTTS* tts = lua_totts(L, 1);
	USHORT rate = 0;

	if (SUCCEEDED(tts->pVoice->GetVolume(&rate))) {
		lua_pushinteger(L, rate);
	}
	else {
		lua_pushnil(L);
	}

	return 1;
}

int SetRate(lua_State* L) {

	LuaTTS* tts = lua_totts(L, 1);
	if (SUCCEEDED(tts->pVoice->SetRate(luaL_checkinteger(L, 2)))) {
		lua_pushboolean(L, true);
	}
	else {
		lua_pushboolean(L, false);
	}

	return 1;
}

int GetRate(lua_State* L) {
	LuaTTS* tts = lua_totts(L, 1);
	long rate = 0;

	if (SUCCEEDED(tts->pVoice->GetRate(&rate))) {
		lua_pushinteger(L, rate);
	}
	else {
		lua_pushnil(L);
	}

	return 1;
}

int Speak(lua_State* L) {

	LuaTTS* tts = lua_totts(L, 1);
	LuaWChar* wchar = lua_stringtowchar(L, 2);

	if (tts->speaking || !tts->alive) {
		lua_pushboolean(L, false);
	}
	else {

		if (tts->textToSpeak) {
			gff_free(tts->textToSpeak);
		}

		tts->textToSpeak = (wchar_t*)gff_calloc(wchar->len + 1, sizeof(wchar_t));

		if (!tts->textToSpeak) {
			luaL_error(L, "Out of memory");
			return 0;
		}

		tts->flags = luaL_optinteger(L, 3, SPF_IS_XML);
		memcpy(tts->textToSpeak, wchar->str, wchar->len * sizeof(wchar_t));
		tts->speaking = true;
		SetEvent(tts->hEvent);

		lua_pushboolean(L, true);
	}

	return 1;
}

int SetVoice(lua_State* L) {

	LuaTTS* tts = lua_totts(L, -2);
	LuaWChar* wchar = lua_stringtowchar(L, -1);

	if (tts->speaking || !tts->alive) {
		lua_pushboolean(L, false);
	}
	else {

		CComPtr<IEnumSpObjectTokens> cpEnum;
		HRESULT hr = SpEnumTokens(SPCAT_VOICES, NULL, NULL, &cpEnum);
		CComPtr<ISpObjectToken> cpVoiceToken;
		CComPtr<ISpDataKey> cpSpAttributesKey;
		wchar_t* data;

		while (cpEnum->Next(1, &cpVoiceToken, NULL) == S_OK)
		{
			hr = cpVoiceToken->OpenKey(L"Attributes", &cpSpAttributesKey);

			if (SUCCEEDED(hr)) {

				cpSpAttributesKey->GetStringValue(L"Name", &data);

				if (StrCmpW(data, wchar->str) == 0) {
					tts->pVoice->SetVoice(cpVoiceToken);
					CoTaskMemFree(data);
					cpSpAttributesKey.Release();

					lua_pushboolean(L, true);
					return 1;
				}

				CoTaskMemFree(data);
				cpSpAttributesKey.Release();
			}

			cpVoiceToken.Release();
		}
	}

	lua_pushboolean(L, false);
	return 1;
}

int GetVoices(lua_State* L) {

	CComPtr<IEnumSpObjectTokens> cpEnum;
	HRESULT hr = SpEnumTokens(SPCAT_VOICES, NULL, NULL, &cpEnum);
	CComPtr<ISpObjectToken> cpVoiceToken;
	CComPtr<ISpDataKey> cpSpAttributesKey;
	wchar_t* data;

	lua_createtable(L, 0, 0);
	int n = 0;
	while (cpEnum->Next(1, &cpVoiceToken, NULL) == S_OK)
	{
		hr = cpVoiceToken->OpenKey(L"Attributes", &cpSpAttributesKey);

		if (SUCCEEDED(hr)) {

			lua_createtable(L, 0, 2);

			lua_pushstring(L, "Name");
			cpSpAttributesKey->GetStringValue(L"Name", &data);
			lua_pushwchar(L, data);
			CoTaskMemFree(data);
			lua_settable(L, -3);

			lua_pushstring(L, "Gender");
			cpSpAttributesKey->GetStringValue(L"Gender", &data);
			lua_pushwchar(L, data);
			CoTaskMemFree(data);
			lua_settable(L, -3);

			lua_rawseti(L, -2, ++n);
			cpSpAttributesKey.Release();
		}

		cpVoiceToken.Release();
	}

	return 1;
}

int CreateTTS(lua_State* L) {

	LuaTTS* tts = lua_pushtts(L);

	HRESULT hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&tts->pVoice);
	if (FAILED(hr))
	{
		tts->pVoice = NULL;
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}

	tts->alive = true;
	DWORD threadId;
	tts->hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	tts->hThread = CreateThread(NULL, 0, ThreadFunc, tts, 0, &threadId);

	if (tts->hThread == INVALID_HANDLE_VALUE) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}

	return 1;
}

LuaTTS* lua_pushtts(lua_State* L) {

	LuaTTS* tts = (LuaTTS*)lua_newuserdata(L, sizeof(LuaTTS));

	if (tts == NULL) {
		luaL_error(L, "Unable to push TTS");
		return NULL;
	}

	luaL_getmetatable(L, TTS);
	lua_setmetatable(L, -2);
	memset(tts, 0, sizeof(LuaTTS));
	tts->alive = false;
	tts->hThread = INVALID_HANDLE_VALUE;
	tts->hEvent = INVALID_HANDLE_VALUE;

	return tts;
}

LuaTTS* lua_totts(lua_State* L, int index) {
	LuaTTS* tts = (LuaTTS*)luaL_checkudata(L, index, TTS);
	if (tts == NULL)
		luaL_error(L, "parameter is not a %s", TTS);
	return tts;
}

int tts_gc(lua_State* L) {

	LuaTTS* tts = lua_totts(L, 1);
	tts->alive = false;

	if (tts->pVoice) {
		tts->pVoice->Release();
		tts->pVoice = NULL;
	}

	if (tts->hThread != INVALID_HANDLE_VALUE) {

		if (tts->hEvent != INVALID_HANDLE_VALUE) {
			SetEvent(tts->hEvent);
			CloseHandle(tts->hEvent);
		}

		WaitForSingleObject(tts->hThread, INFINITE);
		CloseHandle(tts->hThread);
		tts->hThread = INVALID_HANDLE_VALUE;
		tts->hEvent = INVALID_HANDLE_VALUE;
	}

	tts->speaking = false;

	if (tts->textToSpeak) {
		gff_free(tts->textToSpeak);
		tts->textToSpeak = NULL;
	}

	return 0;
}

int tts_tostring(lua_State* L) {
	char tim[100];
	sprintf(tim, "TTS: 0x%08X", lua_totts(L, 1));
	lua_pushfstring(L, tim);
	return 1;
}