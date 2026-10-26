#include "Mem.h"
#include <new>
#include <stdlib.h>
#include "string.h"
#include <assert.h>
#include <atomic>
#include "Platform.h"

// ── Debug allocation counter ───────────────────────────────────────────────────
#ifdef _DEBUG
static std::atomic<size_t> g_liveAllocs{ 0 };
static std::atomic<size_t> g_permanentAllocs{ 0 };
#endif

// ── Base allocator function pointers ──────────────────────────────────────────
// Defaults are the standard C allocators (cross-platform).
// Call kitsune_set_allocators() to override at runtime.
static void* (*g_mallocFn)(size_t) = ::malloc;
static void* (*g_reallocFn)(void*, size_t) = ::realloc;
static void  (*g_freeFn)(void*) = ::free;

// ── Platform-specific backend implementations ──────────────────────────────────
#ifdef USEMEMORYMANAGER
#include "MemoryManager.h"

static MemoryState* memState = NULL;

static void* MmAllocRaw(size_t requested, size_t* actual) {
	*actual = requested;
	return HeapAlloc(GetProcessHeap(), 0, *actual);
}
static void MmDeallocRaw(void* ptr) {
	assert(HeapFree(GetProcessHeap(), 0, ptr));
}
static void* MmReallocRaw(void* ptr, size_t requested, size_t* actual) {
	*actual = requested;
	return HeapReAlloc(GetProcessHeap(), 0, ptr, *actual);
}
static void* MmMalloc(size_t size) { return MemoryStateAlloc(memState, size); }
static void* MmRealloc(void* ptr, size_t size) { return MemoryStateRealloc(memState, ptr, size); }
static void  MmFree(void* ptr) { MemoryStateDealloc(memState, ptr); }

#elif defined(USEHEAPALLOC)
static void* MemHeapMalloc(size_t size) { return HeapAlloc(GetProcessHeap(), 0, size); }
static void* MemHeapRealloc(void* ptr, size_t size) { return HeapReAlloc(GetProcessHeap(), 0, ptr, size); }
static void  MemHeapFree(void* ptr) { assert(HeapFree(GetProcessHeap(), 0, ptr)); }
#endif

// ── Memory manager lifecycle ───────────────────────────────────────────────────
size_t EndMemoryManager() {
#ifdef USEMEMORYMANAGER
	assert(memState);
	size_t result = DestroyMemoryState(memState);
	memState = NULL;
	return result;
#elif defined(_DEBUG)
	size_t live = g_liveAllocs.load();
	size_t perm = g_permanentAllocs.load();
	return live > perm ? live - perm : 0;
#else
	return 0;
#endif
}

void InitMemoryManager() {
#ifdef USEMEMORYMANAGER
	assert(!memState);
	memState = CreateNewMemoryState(MmAllocRaw, MmDeallocRaw, MmReallocRaw);
	g_mallocFn = MmMalloc;
	g_reallocFn = MmRealloc;
	g_freeFn = MmFree;
#elif defined(USEHEAPALLOC)
	g_mallocFn = MemHeapMalloc;
	g_reallocFn = MemHeapRealloc;
	g_freeFn = MemHeapFree;
#else
	g_mallocFn = ::malloc;
	g_reallocFn = ::realloc;
	g_freeFn = ::free;
#endif
#ifdef _DEBUG
	// Snapshot the current counter as the baseline for this session rather than resetting
	// to zero.  In multi-session test hosts the .NET GC may finalize LuaFunctionRef /
	// LuaThreadRef objects from a previous session after this point, decrementing
	// g_liveAllocs.  By baselining here those cross-session frees are already accounted
	// for in g_permanentAllocs and EndMemoryManager returns 0 instead of underflowing.
	g_permanentAllocs.store(g_liveAllocs.load());
#endif
}

void kitsune_snapshot_permanent_allocs() {
#ifdef _DEBUG
	g_permanentAllocs.store(g_liveAllocs.load());
#endif
}

// ── Public allocator override ──────────────────────────────────────────────────
void kitsune_set_allocators(
	void* (*malloc_fn)(size_t),
	void* (*realloc_fn)(void*, size_t),
	void  (*free_fn)(void*))
{
	if (malloc_fn) 
		g_mallocFn = malloc_fn;

	if (realloc_fn)
		g_reallocFn = realloc_fn;

	if (free_fn)
		g_freeFn = free_fn;
}

// ── Core allocators ────────────────────────────────────────────────────────────
void* kitsune_malloc(size_t size) {
	void* p = (g_mallocFn ? g_mallocFn : ::malloc)(size);
#ifdef _DEBUG
	if (p)
		g_liveAllocs++;
#endif
	return p;
}

void* kitsune_realloc(void* ptr, size_t size) {
	if (size == 0) {
		kitsune_free(ptr);
		return NULL;
	}
	if (!ptr)
		return kitsune_malloc(size);
	return (g_reallocFn ? g_reallocFn : ::realloc)(ptr, size);  // resize; allocation count unchanged
}

void kitsune_free(void* ptr) {
	if (ptr) {
		(g_freeFn ? g_freeFn : ::free)(ptr);
#ifdef _DEBUG
		g_liveAllocs--;
#endif
	}
}

// kitsune_calloc is a thin wrapper around kitsune_malloc so counter tracking
// and allocator routing happen in one place regardless of build configuration.
void* kitsune_calloc(size_t num, size_t size) {
	void* ptr = kitsune_malloc(num * size);
	if (ptr)
		memset(ptr, 0, num * size);
	return ptr;
}

// ── Global operator new / delete overrides
// Route every C++ heap allocation in this DLL through kitsune_malloc / kitsune_free
// so the memory-manager counter (g_liveAllocs or MemoryStateAlloc) tracks all allocations,
// including those from STL containers, std::thread, and any third-party C++ code linked in.

void* operator new(size_t size) {
	void* p = kitsune_malloc(size);
	if (!p) throw std::bad_alloc();
	return p;
}

void* operator new[](size_t size) {
	void* p = kitsune_malloc(size);
	if (!p) throw std::bad_alloc();
	return p;
}

void operator delete(void* ptr) noexcept {
	kitsune_free(ptr);
}

void operator delete[](void* ptr) noexcept {
	kitsune_free(ptr);
}

// C++14 sized-delete overloads — size is ignored; kitsune_free handles it.
void operator delete(void* ptr, size_t) noexcept {
	kitsune_free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
	kitsune_free(ptr);
}

// Nothrow variants used by expressions such as new (std::nothrow) T{}.
void* operator new(size_t size, std::nothrow_t const&) noexcept {
	return kitsune_malloc(size);
}

void* operator new[](size_t size, std::nothrow_t const&) noexcept {
	return kitsune_malloc(size);
}

void operator delete(void* ptr, std::nothrow_t const&) noexcept {
	kitsune_free(ptr);
}

void operator delete[](void* ptr, std::nothrow_t const&) noexcept {
	kitsune_free(ptr);
}