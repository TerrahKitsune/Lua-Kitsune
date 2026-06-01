#include "mem.h"
#include <new>
#include <stdlib.h>
#include "string.h"
#include <assert.h>
#include <atomic>
#include "platform.h"

// ── Debug allocation counter ───────────────────────────────────────────────────
#ifdef _DEBUG
static std::atomic<size_t> g_live_allocs{ 0 };
static std::atomic<size_t> g_permanent_allocs{ 0 };
#endif

// ── Base allocator function pointers ──────────────────────────────────────────
// Defaults are the standard C allocators (cross-platform).
// Call kitsune_set_allocators() to override at runtime.
static void* (*s_malloc_fn)(size_t) = ::malloc;
static void* (*s_realloc_fn)(void*, size_t) = ::realloc;
static void  (*s_free_fn)(void*) = ::free;

// ── Platform-specific backend implementations ──────────────────────────────────
#ifdef USEMEMORYMANAGER
#include "MemoryManager.h"

static MemoryState* memState = NULL;

static void* mm_alloc_raw(size_t requested, size_t* actual) {
	*actual = requested;
	return HeapAlloc(GetProcessHeap(), 0, *actual);
}
static void mm_dealloc_raw(void* ptr) {
	assert(HeapFree(GetProcessHeap(), 0, ptr));
}
static void* mm_realloc_raw(void* ptr, size_t requested, size_t* actual) {
	*actual = requested;
	return HeapReAlloc(GetProcessHeap(), 0, ptr, *actual);
}
static void* mm_malloc(size_t size) { return MemoryStateAlloc(memState, size); }
static void* mm_realloc(void* ptr, size_t size) { return MemoryStateRealloc(memState, ptr, size); }
static void  mm_free(void* ptr) { MemoryStateDealloc(memState, ptr); }

#elif defined(USEHEAPALLOC)
static void* heap_malloc(size_t size) { return HeapAlloc(GetProcessHeap(), 0, size); }
static void* heap_realloc(void* ptr, size_t size) { return HeapReAlloc(GetProcessHeap(), 0, ptr, size); }
static void  heap_free(void* ptr) { assert(HeapFree(GetProcessHeap(), 0, ptr)); }
#endif

// ── Memory manager lifecycle ───────────────────────────────────────────────────
size_t EndMemoryManager() {
#ifdef USEMEMORYMANAGER
	assert(memState);
	size_t result = DestroyMemoryState(memState);
	memState = NULL;
	return result;
#elif defined(_DEBUG)
	size_t live = g_live_allocs.load();
	size_t perm = g_permanent_allocs.load();
	return live > perm ? live - perm : 0;
#else
	return 0;
#endif
}

void InitMemoryManager() {
#ifdef USEMEMORYMANAGER
	assert(!memState);
	memState = CreateNewMemoryState(mm_alloc_raw, mm_dealloc_raw, mm_realloc_raw);
	s_malloc_fn = mm_malloc;
	s_realloc_fn = mm_realloc;
	s_free_fn = mm_free;
#elif defined(USEHEAPALLOC)
	s_malloc_fn = heap_malloc;
	s_realloc_fn = heap_realloc;
	s_free_fn = heap_free;
#else
	s_malloc_fn = ::malloc;
	s_realloc_fn = ::realloc;
	s_free_fn = ::free;
#endif
#ifdef _DEBUG
	// Snapshot the current counter as the baseline for this session rather than resetting
	// to zero.  In multi-session test hosts the .NET GC may finalize LuaFunctionRef /
	// LuaThreadRef objects from a previous session after this point, decrementing
	// g_live_allocs.  By baselining here those cross-session frees are already accounted
	// for in g_permanent_allocs and EndMemoryManager returns 0 instead of underflowing.
	g_permanent_allocs.store(g_live_allocs.load());
#endif
}

void kitsune_snapshot_permanent_allocs() {
#ifdef _DEBUG
	g_permanent_allocs.store(g_live_allocs.load());
#endif
}

// ── Public allocator override ──────────────────────────────────────────────────
void kitsune_set_allocators(
	void* (*malloc_fn)(size_t),
	void* (*realloc_fn)(void*, size_t),
	void  (*free_fn)(void*))
{
	if (malloc_fn) 
		s_malloc_fn = malloc_fn;

	if (realloc_fn)
		s_realloc_fn = realloc_fn;

	if (free_fn)
		s_free_fn = free_fn;
}

// ── Core allocators ────────────────────────────────────────────────────────────
void* kitsune_malloc(size_t size) {
	void* p = (s_malloc_fn ? s_malloc_fn : ::malloc)(size);
#ifdef _DEBUG
	if (p)
		g_live_allocs++;
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
	return (s_realloc_fn ? s_realloc_fn : ::realloc)(ptr, size);  // resize; allocation count unchanged
}

void kitsune_free(void* ptr) {
	if (ptr) {
		(s_free_fn ? s_free_fn : ::free)(ptr);
#ifdef _DEBUG
		g_live_allocs--;
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
// so the memory-manager counter (g_live_allocs or MemoryStateAlloc) tracks all allocations,
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