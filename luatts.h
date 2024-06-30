#pragma once
#include "lua_main_incl.h"
#include <Windows.h>
static const char* TTS = "TTS";
#define _ATL_APARTMENT_THREADED

#include <atlbase.h>
extern CComModule _Module;
#include <atlcom.h>

#include <sapi.h>
#pragma comment(lib, "sapi.lib")

typedef struct LuaTTS {

	ISpVoice* pVoice;
	bool alive;
	bool speaking;
	bool paused;
	HANDLE hThread;
	HANDLE hEvent;
	wchar_t* textToSpeak;
	DWORD flags;

} LuaTTS;


LuaTTS* lua_pushtts(lua_State* L);
LuaTTS* lua_totts(lua_State* L, int index);

int GetVoices(lua_State* L);
int CreateTTS(lua_State* L);
int GetIsSpeaking(lua_State* L);
int Speak(lua_State* L);
int Skip(lua_State* L);
int SetVoice(lua_State* L);
int PlayPause(lua_State* L);
int GetIsPaused(lua_State* L);
int GetRate(lua_State* L);
int SetRate(lua_State* L);
int GetVolume(lua_State* L);
int SetVolume(lua_State* L);

int tts_gc(lua_State* L);
int tts_tostring(lua_State* L);