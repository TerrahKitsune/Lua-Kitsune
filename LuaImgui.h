#pragma once
#include "lua_main_incl.h"
#include <Windows.h>
#include <assert.h>

#define ImTextureID ImU64
#include "./Imgui/imgui.h"

typedef struct ImguiElement;

//--- Interface

#include "./Imgui/imgui_impl_win32.h"
#include "./Imgui/imgui_impl_dx12.h"

#include "d3d12.h"
#include "dxgi1_4.h"
#include <tchar.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#pragma comment(lib,"d3d12.lib")
#pragma comment(lib, "dxgi")

static const char* IMGUI = "IMGUI";
int const NUM_FRAMES_IN_FLIGHT = 3;
int const NUM_BACK_BUFFERS = 3;

struct FrameContext
{
	ID3D12CommandAllocator* CommandAllocator;
	UINT64                  FenceValue;
};

typedef struct LuaImgui {

	//--- Interface
	HWND hWnd;

	FrameContext frameContext[NUM_FRAMES_IN_FLIGHT] = {};
	UINT frameIndex = 0;

	ID3D12Device* pd3dDevice = NULL;
	ID3D12DescriptorHeap* pd3dRtvDescHeap = NULL;
	ID3D12DescriptorHeap* pd3dSrvDescHeap = NULL;
	ID3D12CommandQueue* pd3dCommandQueue = NULL;
	ID3D12GraphicsCommandList* pd3dCommandList = NULL;
	ID3D12Fence* fence = NULL;
	HANDLE fenceEvent = NULL;
	UINT64 fenceLastSignaledValue = 0;
	IDXGISwapChain3* pSwapChain = NULL;
	HANDLE hSwapChainWaitableObject = NULL;
	ID3D12Resource* mainRenderTargetResource[NUM_BACK_BUFFERS] = {};
	D3D12_CPU_DESCRIPTOR_HANDLE mainRenderTargetDescriptor[NUM_BACK_BUFFERS] = {};
	WNDCLASSEX wc;

	//--- Interface	end

	bool isInRender;
	int renderFuncRef;
	char* backgroundTag;

	ImguiElement* Elements;

	size_t stringArraySize;
	size_t stringArrayLen;
	const char** stringArray;

} LuaImgui;

//--- Interface end

bool CreateDeviceD3D(LuaImgui* ui);
void CleanupDeviceD3D(LuaImgui* ui);
void CreateRenderTarget(LuaImgui* ui);
void CleanupRenderTarget(LuaImgui* ui);
void WaitForLastSubmittedFrame(LuaImgui* ui);
FrameContext* WaitForNextFrameResources(LuaImgui* ui);
LRESULT WINAPI ImguiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int MainLoopQuit(lua_State* L);
int CreateImguiWindow(lua_State* L);
int MainloopImguiWindow(lua_State* L);

extern LuaImgui* g_currentImgui;
extern bool windowExists;

#define IMGUI_TYPE_ANY 0
#define IMGUI_TYPE_BOOL 1
#define IMGUI_TYPE_FLOAT 2
#define IMGUI_TYPE_VEC4 3
#define IMGUI_TYPE_INT 4
#define IMGUI_TYPE_STRING 5
#define IMGUI_TYPE_DOUBLE 6
#define IMGUI_TYPE_MAX 6

typedef struct ImguiElement;
typedef void (*ImguiElementFree) (ImguiElement*);

int LuaImGuiInputTextCallback(ImGuiInputTextCallbackData* data);

struct ImguiElement {

	ImguiElement* Next;
	char* Name;
	int Type;
	void* Data;
	size_t Size;
	size_t Len;
	ImguiElementFree freeFunc;
};

int LuaImguiCalcTextSize(lua_State* L);
int LuaImguiBeginDisabled(lua_State* L);
int LuaImguiEndDisabled(lua_State* L);
int LuaImguiIndent(lua_State* L);
int LuaImguiUnindent(lua_State* L);
int LuaImguiBeginMainMenuBar(lua_State* L);
int LuaImguiEndMainMenuBar(lua_State* L);
int LuaImguiEndTable(lua_State* L);
int LuaImguiBeginTable(lua_State* L);
int LuaImguiTableNextColumn(lua_State* L);
int LuaImguiCollapsingHeader(lua_State* L);
int LuaImguiProgressBar(lua_State* L);
int LuaImguiGetTextLineHeight(lua_State* L);
int LuaImguiNextWindowContentSize(lua_State* L);
int LuaImguiGetWindowSize(lua_State* L);
int LuaImguiInputTextMultiline(lua_State* L);
int LuaImguiListBox(lua_State* L);
int LuaImguiSliderInt(lua_State* L);
int LuaImguiInputDouble(lua_State* L);
int LuaImguiInputFloat(lua_State* L);
int LuaImguiInputInt(lua_State* L);
int LuaImguiInputText(lua_State* L);
int LuaImguiHelpMarker(lua_State* L);
int LuaImguiCombo(lua_State* L);
int LuaImguiLabelText(lua_State* L);
int LuaImguiBeginTooltip(lua_State* L);
int LuaImguiEndTooltip(lua_State* L);
int LuaImguiIsItemHovered(lua_State* L);
int LuaImguiPushButtonRepeat(lua_State* L);
int LuaImguiPopButtonRepeat(lua_State* L);
int LuaImguiArrowButton(lua_State* L);
int LuaImguiAlignTextToFramePadding(lua_State* L);
int LuaImguiPushStyleColor(lua_State* L);
int LuaImguiPopStyleColor(lua_State* L);
int LuaImguiPushId(lua_State* L);
int LuaImguiPopId(lua_State* L);
int LuaImguiRadioButton(lua_State* L);
int LuaImguiTextColored(lua_State* L);
int LuaImguiTextWrapped(lua_State* L);
int LuaImguiBeginTabItem(lua_State* L);
int LuaImguiEndTabBarItem(lua_State* L);
int LuaImguiBeginTabBar(lua_State* L);
int LuaImguiEndTabBar(lua_State* L);
int LuaImguiGetFrameHeightWithSpacing(lua_State* L);
int LuaImguiBeginGroup(lua_State* L);
int LuaImguiEndGroup(lua_State* L);
int LuaImguiEndChild(lua_State* L);
int LuaImguiBeginChild(lua_State* L);
int LuaImguiMenuItem(lua_State* L);
int LuaImguiBeginMenu(lua_State* L);
int LuaImguiEndMenu(lua_State* L);
int LuaImguiBeginMenuBar(lua_State* L);
int LuaImguiEndMenuBar(lua_State* L);
int LuaImguiSetNextWindowSize(lua_State* L);
int LuaImguiSelectable(lua_State* L);
int LuaImguiSeparator(lua_State* L);
int LuaImguiSameLine(lua_State* L);
int LuaImguiButton(lua_State* L);
int LuaImguiColorEdit3(lua_State* L);
int LuaImguiSliderFloat(lua_State* L);
int LuaImguiText(lua_State* L);
int LuaImguiCheckbox(lua_State* L);
int LuaImguiShowDemoWindow(lua_State* L);
int LuaImguiBegin(lua_State* L);
int LuaImguiEnd(lua_State* L);

ImVec2 lua_toimvec2(lua_State* L, int idx);
void lua_pushimvec2(lua_State* L, ImVec2 vec);
void lua_pushimvec4(lua_State* L, ImVec4 vec);
ImVec4 lua_toimvec4(lua_State* L, int idx);
LuaImgui* lua_pushimgui(lua_State* L);
LuaImgui* lua_toimgui(lua_State* L, int index);

void LoadTableIntoStringArray(lua_State* L, LuaImgui* ui, int idx);
int Vec4ToRGB(lua_State* L);
int RGBToVec4(lua_State* L);
int GetImguiInfo(lua_State* L);
int GetValueFromTag(lua_State* L);
int SetValueFromTag(lua_State* L);

int imgui_gc(lua_State* L);
int imgui_tostring(lua_State* L);
int ClearMemory(lua_State* L);
int GetAllValues(lua_State* L);

ImguiElement* AddElement(LuaImgui* ui, const char* name, int type);
ImguiElement* GetElement(LuaImgui* ui, const char* name, int type);
bool RemoveElement(LuaImgui* ui, const char* name, int type);