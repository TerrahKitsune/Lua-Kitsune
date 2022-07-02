#pragma once
#include "lua_main_incl.h"
#include <Windows.h>

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

} LuaImgui;

//--- Interface end

bool CreateDeviceD3D(LuaImgui* ui);
void CleanupDeviceD3D(LuaImgui* ui);
void CreateRenderTarget(LuaImgui* ui);
void CleanupRenderTarget(LuaImgui* ui);
void WaitForLastSubmittedFrame(LuaImgui* ui);
FrameContext* WaitForNextFrameResources(LuaImgui* ui);
LRESULT WINAPI ImguiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern LuaImgui* g_currentImgui;
extern bool windowExists;

#define IMGUI_TYPE_ANY 0
#define IMGUI_TYPE_BOOL 1
#define IMGUI_TYPE_FLOAT 2
#define IMGUI_TYPE_VEC4 3
#define IMGUI_TYPE_MAX 3

struct ImguiElement {

	char* Name;
	int Type;
	void* Data;

	ImguiElement* Next;
};

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

LuaImgui* lua_pushimgui(lua_State* L);
LuaImgui* lua_toimgui(lua_State* L, int index);

int GetImguiInfo(lua_State* L);
int CreateImguiWindow(lua_State* L);
int MainloopImguiWindow(lua_State* L);
int GetValueFromTag(lua_State* L);
int SetValueFromTag(lua_State* L);

int imgui_gc(lua_State* L);
int imgui_tostring(lua_State* L);

ImguiElement* AddElement(LuaImgui* ui, const char* name, int type);
ImguiElement* GetElement(LuaImgui* ui, const char* name, int type);
bool RemoveElement(LuaImgui* ui, const char* name, int type);