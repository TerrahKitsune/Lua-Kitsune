#include "mem.h"
#include <stdlib.h>
#include "MemoryManager.h"
#include "string.h"
#include <assert.h>
#include <math.h>
#include <atomic>
#include "platform.h"

#ifdef USEMEMORYMANAGER

static MemoryState * memState = NULL;

void * Allocate(size_t requested, size_t* actual) {

	*actual = requested;

	return HeapAlloc(GetProcessHeap(), 0, *actual);
}

void Deallocate(void * ptr) {

	assert(HeapFree(GetProcessHeap(), 0, ptr));
}

void * ReAllocate(void * ptr, size_t requested, size_t* actual) {

	*actual = requested;
	return HeapReAlloc(GetProcessHeap(), 0, ptr, *actual);
}

size_t EndMemoryManager() {

	assert(memState);
	size_t result = DestroyMemoryState(memState);
	memState = NULL;
	return result;
}

void InitMemoryManager() {

	assert(!memState);
	memState = CreateNewMemoryState(Allocate, Deallocate, ReAllocate);
}

void * gff_malloc(size_t size) {
	return MemoryStateAlloc(memState, size);
}

void * gff_calloc(size_t num, size_t size) {
	void*ptr = MemoryStateAlloc(memState, num*size);
	if (ptr) {
		memset(ptr, 0, num*size);
	}
	return ptr;
}

void * gff_realloc(void * ptr, size_t size) {
	return MemoryStateRealloc(memState, ptr, size);
}

void gff_free(void * ptr) {
	return MemoryStateDealloc(memState, ptr);
}

#elif defined(USEHEAPALLOC)

#ifdef _DEBUG
static std::atomic<size_t> g_live_allocs{0};
#endif

size_t EndMemoryManager() {
#ifdef _DEBUG
	return g_live_allocs.load();
#else
	return 0;
#endif
}

void InitMemoryManager() {
#ifdef _DEBUG
	g_live_allocs.store(0);
#endif
}

void * gff_malloc(size_t size) {
	void* p = HeapAlloc(GetProcessHeap(), 0, size);
#ifdef _DEBUG
	if (p)
		g_live_allocs++;
#endif
	return p;
}

void * gff_calloc(size_t num, size_t size) {
	void* p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, num * size);
#ifdef _DEBUG
	if (p)
		g_live_allocs++;
#endif
	return p;
}

void * gff_realloc(void * ptr, size_t size) {

	if (size == 0) {
		if (ptr) {
			assert(HeapFree(GetProcessHeap(), 0, ptr));
#ifdef _DEBUG
			g_live_allocs--;
#endif
		}
		return NULL;
	}
	else if (!ptr) {
		void* p = HeapAlloc(GetProcessHeap(), 0, size);
#ifdef _DEBUG
		if (p)
			g_live_allocs++;
#endif
		return p;
	}

	return HeapReAlloc(GetProcessHeap(), 0, ptr, size);
}

void gff_free(void * ptr) {
	if (ptr) {
		assert(HeapFree(GetProcessHeap(), 0, ptr));
#ifdef _DEBUG
		g_live_allocs--;
#endif
	}
}

#else

#ifdef _DEBUG
static std::atomic<size_t> g_live_allocs{0};
#endif

size_t EndMemoryManager() {
#ifdef _DEBUG
	return g_live_allocs.load();
#else
	return 0;
#endif
}

void InitMemoryManager() {
#ifdef _DEBUG
	g_live_allocs.store(0);
#endif
}

void * gff_malloc(size_t size) {
	void* p = malloc(size);
#ifdef _DEBUG
	if (p)
		g_live_allocs++;
#endif
	return p;
}

void * gff_calloc(size_t num, size_t size) {
	void* p = calloc(num, size);
#ifdef _DEBUG
	if (p)
		g_live_allocs++;
#endif
	return p;
}

void * gff_realloc(void * ptr, size_t size) {
	if (size == 0) {
		if (ptr) {
			free(ptr);
#ifdef _DEBUG
			g_live_allocs--;
#endif
		}
		return NULL;
	}
	void* p = realloc(ptr, size);
#ifdef _DEBUG
	if (!ptr && p)
		g_live_allocs++;  // NULL ptr: behaves like malloc
#endif
	return p;
}

void gff_free(void * ptr) {
	if (ptr) {
		free(ptr);
#ifdef _DEBUG
		g_live_allocs--;
#endif
	}
}

#endif