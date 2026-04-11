#pragma once
#include <stddef.h>

void InitMemoryManager();
size_t EndMemoryManager();

void* kitsune_malloc(size_t size);
void* kitsune_calloc(size_t num, size_t size);
void* kitsune_realloc(void* ptr, size_t size);
void  kitsune_free(void* ptr);

// Override the three base allocators at runtime.
// Pass NULL for any pointer to leave that slot unchanged.
// Must be called before any allocation if overriding the defaults.
void kitsune_set_allocators(
    void* (*malloc_fn)(size_t),
    void* (*realloc_fn)(void*, size_t),
    void  (*free_fn)(void*));
