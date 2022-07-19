#include "LuaImgui.h"

LuaImgui* g_currentImgui = NULL;
bool windowExists = false;

int MainLoopQuit(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	PostQuitMessage(luaL_optinteger(L, 2, 0));
	return 0;
}

int MainloopImguiWindow(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	lua_settop(L, 1);

	if (imgui->isInRender) {
		luaL_error(L, "Cannot call Tick from within renderer");
		return 0;
	}
	else if (imgui->hWnd == INVALID_HANDLE_VALUE) {
		lua_pushboolean(L, FALSE);
		return 1;
	}

	bool done = false;

	g_currentImgui = imgui;

	MSG msg;
	while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		if (msg.message == WM_QUIT) {
			done = true;
		}
	}

	g_currentImgui = NULL;

	if (done) {

		imgui_gc(L);

		lua_pushboolean(L, FALSE);
		return 1;
	}

	// Start the Dear ImGui frame
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	lua_rawgeti(L, LUA_REGISTRYINDEX, imgui->renderFuncRef);
	lua_pushvalue(L, 1);

	imgui->isInRender = true;
	int result = lua_pcall(L, 1, 0, NULL);
	imgui->isInRender = false;

	// Rendering
	ImGui::Render();

	RECT rect;
	GetWindowRect(imgui->hWnd, &rect);

	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;

	FrameContext* frameCtx = WaitForNextFrameResources(imgui);
	UINT backBufferIdx = imgui->pSwapChain->GetCurrentBackBufferIndex();
	frameCtx->CommandAllocator->Reset();

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = imgui->mainRenderTargetResource[backBufferIdx];
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	imgui->pd3dCommandList->Reset(frameCtx->CommandAllocator, NULL);
	imgui->pd3dCommandList->ResourceBarrier(1, &barrier);

	ImVec4 vec = ImVec4(0, 0, 0, 0);
	ImguiElement* background = GetElement(imgui, imgui->backgroundTag, IMGUI_TYPE_VEC4);

	if (background) {
		memcpy(&vec, background->Data, sizeof(ImVec4));
	}

	// Render Dear ImGui graphics
	const float clear_color_with_alpha[4] = { vec.x * vec.w, vec.y * vec.w, vec.z * vec.w, vec.w };
	imgui->pd3dCommandList->ClearRenderTargetView(imgui->mainRenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, NULL);
	imgui->pd3dCommandList->OMSetRenderTargets(1, &imgui->mainRenderTargetDescriptor[backBufferIdx], FALSE, NULL);
	imgui->pd3dCommandList->SetDescriptorHeaps(1, &imgui->pd3dSrvDescHeap);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), imgui->pd3dCommandList);
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	imgui->pd3dCommandList->ResourceBarrier(1, &barrier);
	imgui->pd3dCommandList->Close();

	imgui->pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&imgui->pd3dCommandList);

	imgui->pSwapChain->Present(1, 0); // Present with vsync
	//g_pSwapChain->Present(0, 0); // Present without vsync

	UINT64 fenceValue = imgui->fenceLastSignaledValue + 1;
	imgui->pd3dCommandQueue->Signal(imgui->fence, fenceValue);
	imgui->fenceLastSignaledValue = fenceValue;
	frameCtx->FenceValue = fenceValue;

	if (result) {
		lua_error(L);
	}

	lua_pushboolean(L, TRUE);

	return 1;
}

static void LoadAllFonts(ImGuiIO& io, const char* folder) {

	char ext[4] = { 0 };
	const char* strmatch;
	char path[MAX_PATH + 1];
	int size;
	int numbs;
	WIN32_FIND_DATA fdFile;
	HANDLE hFind = NULL;

	io.Fonts->AddFontDefault();

	if (strlen(folder) >= MAX_PATH-10) {
		return;
	}
	else {
		strcpy(path, folder);
		strcat(path, "*.*");
	}

	if ((hFind = FindFirstFile(path, &fdFile)) == INVALID_HANDLE_VALUE)
	{
		return;
	}

	do
	{
		strmatch = strstr(fdFile.cFileName, ".");

		if (strcmp(fdFile.cFileName, ".") != 0
			&& strcmp(fdFile.cFileName, "..") != 0
			&& (fdFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0
			&& strmatch && strlen(strmatch) == 4)
		{
			strcpy(ext, strmatch + 1);

			for (int n = 0; n < 3; n++) {
				ext[n] = tolower(ext[n]);
			}

			if (strcmp(ext, "otf") == 0 || strcmp(ext, "ttf") == 0) {

				size = 0;
				numbs = 0;
				
				while (isdigit((int)*(--strmatch))) {
					numbs++;
				}

				if (numbs > 0) {
					sscanf(strmatch+1, "%d", &size);
				}

				if (size <= 0) {
					size = 13;
				}

				strcpy(path, folder);
				strcat(path, fdFile.cFileName);
				io.Fonts->AddFontFromFileTTF(path, size);
			}
		}
	} while (FindNextFile(hFind, &fdFile));

	FindClose(hFind);
}

int CreateImguiWindow(lua_State* L) {

	size_t len;
	const char* title = luaL_checklstring(L, 1, &len);
	const char* tag = luaL_checkstring(L, 2);
	int width = luaL_checkinteger(L, 3);
	int height = luaL_checkinteger(L, 4);

	if (width <= 10 || height <= 10) {
		luaL_error(L, "Width or height too small");
		return 0;
	}
	else if (!title || len <= 0) {
		luaL_error(L, "Invalid title");
		return 0;
	}

	luaL_checktype(L, 5, LUA_TFUNCTION);

	WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, ImguiWndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("dx12_kitsune_window"), NULL };
	RegisterClassEx(&wc);
	HWND hwnd = CreateWindow(wc.lpszClassName, _T(title), WS_OVERLAPPEDWINDOW, 100, 100, width, height, NULL, NULL, wc.hInstance, NULL);

	if (hwnd == INVALID_HANDLE_VALUE) {
		UnregisterClass(wc.lpszClassName, wc.hInstance);
		lua_settop(L, 0);
		return 0;
	}

	LuaImgui* ui = lua_pushimgui(L);

	ui->hWnd = hwnd;
	ui->wc = wc;

	if (!CreateDeviceD3D(ui))
	{
		CleanupDeviceD3D(ui);
		DestroyWindow(ui->hWnd);
		UnregisterClass(wc.lpszClassName, wc.hInstance);
		ui->hWnd = (HWND)INVALID_HANDLE_VALUE;
		lua_settop(L, 0);
		return 0;
	}

	ui->backgroundTag = (char*)gff_malloc(strlen(tag) + 1);
	strcpy(ui->backgroundTag, tag);

	lua_pushvalue(L, 5);
	ui->renderFuncRef = luaL_ref(L, LUA_REGISTRYINDEX);

	windowExists = true;

	ShowWindow(hwnd, SW_SHOWDEFAULT);
	UpdateWindow(hwnd);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(ui->pd3dDevice, NUM_FRAMES_IN_FLIGHT,
		DXGI_FORMAT_R8G8B8A8_UNORM, ui->pd3dSrvDescHeap,
		ui->pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		ui->pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

	LoadAllFonts(ImGui::GetIO(), "./Fonts/");

	return 1;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI ImguiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SIZE:
		if (g_currentImgui != NULL && g_currentImgui->pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
		{
			WaitForLastSubmittedFrame(g_currentImgui);
			CleanupRenderTarget(g_currentImgui);
			HRESULT result = g_currentImgui->pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
			assert(SUCCEEDED(result) && "Failed to resize swapchain.");
			CreateRenderTarget(g_currentImgui);
		}
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

// Helper functions

bool CreateDeviceD3D(LuaImgui* ui)
{
	// Setup swap chain
	DXGI_SWAP_CHAIN_DESC1 sd;
	{
		ZeroMemory(&sd, sizeof(sd));
		sd.BufferCount = NUM_BACK_BUFFERS;
		sd.Width = 0;
		sd.Height = 0;
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		sd.Scaling = DXGI_SCALING_STRETCH;
		sd.Stereo = FALSE;
	}

	// [DEBUG] Enable debug interface
#ifdef DX12_ENABLE_DEBUG_LAYER
	ID3D12Debug* pdx12Debug = NULL;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug))))
		pdx12Debug->EnableDebugLayer();
#endif

	// Create device
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	if (D3D12CreateDevice(NULL, featureLevel, IID_PPV_ARGS(&ui->pd3dDevice)) != S_OK)
		return false;

	// [DEBUG] Setup debug interface to break on any warnings/errors
#ifdef DX12_ENABLE_DEBUG_LAYER
	if (pdx12Debug != NULL)
	{
		ID3D12InfoQueue* pInfoQueue = NULL;
		g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
		pInfoQueue->Release();
		pdx12Debug->Release();
	}
#endif

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NumDescriptors = NUM_BACK_BUFFERS;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NodeMask = 1;
		if (ui->pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&ui->pd3dRtvDescHeap)) != S_OK)
			return false;

		SIZE_T rtvDescriptorSize = ui->pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = ui->pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
		for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		{
			ui->mainRenderTargetDescriptor[i] = rtvHandle;
			rtvHandle.ptr += rtvDescriptorSize;
		}
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 1;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (ui->pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&ui->pd3dSrvDescHeap)) != S_OK)
			return false;
	}

	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 1;
		if (ui->pd3dDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&ui->pd3dCommandQueue)) != S_OK)
			return false;
	}

	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
		if (ui->pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ui->frameContext[i].CommandAllocator)) != S_OK)
			return false;

	if (ui->pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ui->frameContext[0].CommandAllocator, NULL, IID_PPV_ARGS(&ui->pd3dCommandList)) != S_OK ||
		ui->pd3dCommandList->Close() != S_OK)
		return false;

	if (ui->pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ui->fence)) != S_OK)
		return false;

	ui->fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (ui->fenceEvent == NULL)
		return false;

	{
		IDXGIFactory4* dxgiFactory = NULL;
		IDXGISwapChain1* swapChain1 = NULL;
		if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK)
			return false;
		if (dxgiFactory->CreateSwapChainForHwnd(ui->pd3dCommandQueue, ui->hWnd, &sd, NULL, NULL, &swapChain1) != S_OK)
			return false;
		if (swapChain1->QueryInterface(IID_PPV_ARGS(&ui->pSwapChain)) != S_OK)
			return false;
		swapChain1->Release();
		dxgiFactory->Release();
		ui->pSwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
		ui->hSwapChainWaitableObject = ui->pSwapChain->GetFrameLatencyWaitableObject();
	}

	CreateRenderTarget(ui);
	return true;
}

void CleanupDeviceD3D(LuaImgui* ui)
{
	CleanupRenderTarget(ui);
	if (ui->pSwapChain) { ui->pSwapChain->SetFullscreenState(false, NULL); ui->pSwapChain->Release(); ui->pSwapChain = NULL; }
	if (ui->hSwapChainWaitableObject != NULL) { CloseHandle(ui->hSwapChainWaitableObject); }
	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
		if (ui->frameContext[i].CommandAllocator) { ui->frameContext[i].CommandAllocator->Release(); ui->frameContext[i].CommandAllocator = NULL; }
	if (ui->pd3dCommandQueue) { ui->pd3dCommandQueue->Release(); ui->pd3dCommandQueue = NULL; }
	if (ui->pd3dCommandList) { ui->pd3dCommandList->Release(); ui->pd3dCommandList = NULL; }
	if (ui->pd3dRtvDescHeap) { ui->pd3dRtvDescHeap->Release(); ui->pd3dRtvDescHeap = NULL; }
	if (ui->pd3dSrvDescHeap) { ui->pd3dSrvDescHeap->Release(); ui->pd3dSrvDescHeap = NULL; }
	if (ui->fence) { ui->fence->Release(); ui->fence = NULL; }
	if (ui->fenceEvent) { CloseHandle(ui->fenceEvent); ui->fenceEvent = NULL; }
	if (ui->pd3dDevice) { ui->pd3dDevice->Release(); ui->pd3dDevice = NULL; }

#ifdef DX12_ENABLE_DEBUG_LAYER
	IDXGIDebug1* pDebug = NULL;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
	{
		pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
		pDebug->Release();
	}
#endif
}

void CreateRenderTarget(LuaImgui* ui)
{
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		ID3D12Resource* pBackBuffer = NULL;
		ui->pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
		ui->pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, ui->mainRenderTargetDescriptor[i]);
		ui->mainRenderTargetResource[i] = pBackBuffer;
	}
}

void CleanupRenderTarget(LuaImgui* ui)
{
	WaitForLastSubmittedFrame(ui);

	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		if (ui->mainRenderTargetResource[i]) { ui->mainRenderTargetResource[i]->Release(); ui->mainRenderTargetResource[i] = NULL; }
}

void WaitForLastSubmittedFrame(LuaImgui* ui)
{
	FrameContext* frameCtx = &ui->frameContext[ui->frameIndex % NUM_FRAMES_IN_FLIGHT];

	UINT64 fenceValue = frameCtx->FenceValue;
	if (fenceValue == 0)
		return; // No fence was signaled

	frameCtx->FenceValue = 0;
	if (ui->fence->GetCompletedValue() >= fenceValue)
		return;

	ui->fence->SetEventOnCompletion(fenceValue, ui->fenceEvent);
	WaitForSingleObject(ui->fenceEvent, INFINITE);
}

FrameContext* WaitForNextFrameResources(LuaImgui* ui)
{
	UINT nextFrameIndex = ui->frameIndex + 1;
	ui->frameIndex = nextFrameIndex;

	HANDLE waitableObjects[] = { ui->hSwapChainWaitableObject, NULL };
	DWORD numWaitableObjects = 1;

	FrameContext* frameCtx = &ui->frameContext[nextFrameIndex % NUM_FRAMES_IN_FLIGHT];
	UINT64 fenceValue = frameCtx->FenceValue;
	if (fenceValue != 0) // means no fence was signaled
	{
		frameCtx->FenceValue = 0;
		ui->fence->SetEventOnCompletion(fenceValue, ui->fenceEvent);
		waitableObjects[1] = ui->fenceEvent;
		numWaitableObjects = 2;
	}

	WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);

	return frameCtx;
}