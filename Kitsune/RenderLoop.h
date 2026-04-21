#pragma once
#ifdef KITSUNE_IMGUI

// Registers Imgui.Start and Imgui.Schedule.
void RegisterRenderLoopFunctions();

// Starts the SDL2/OpenGL/ImGui render loop. Blocks until the window closes.
// Called by RunImguiSession in the host after RegisterImguiFunctions().
void RunImguiSession();

typedef void (*ImguiStackEntryFinalizer)(void* data);

struct ImguiStackEntry {
	int id; // Arbitrary integer id
	void* data;
	ImguiStackEntryFinalizer finalizer; // Called on pop or when the session ends, whichever comes first. May be nullptr. Regular free is used to free data if finalizer is nullptr.
	ImguiStackEntry* prev;
	ImguiStackEntry* next;
};

bool ImguiStackPush(int id, void* data, ImguiStackEntryFinalizer fn); // Pushes entry onto the stack. Returns false on OOM; in that case entry is not pushed and finalizer is not called.
bool ImguiStackPop(int id); // Pops the first entry with a matching id, calling its finalizer. Returns false if no matching entry is found; in that case the stack is unchanged and no finalizer is called.

#endif // KITSUNE_IMGUI
