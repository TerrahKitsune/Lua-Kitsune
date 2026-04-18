#pragma once
#include <stdbool.h>
#include <stddef.h>

#define RESOURCE_INVALID 0
#define RESOURCE_TEXTURE 1
#define RESOURCE_AUDIO   2

// Forward declaration
struct Resource;

// Called by ResourceCacheRemove to free a resource's contents and the node itself.
// The implementation must call free(node) (or equivalent) before returning.
typedef void (*ResourceFinalizer)(struct Resource* node);

// Base struct for all resource types. Must be the first field in every subtype
// so a Resource* can be safely cast to the subtype pointer.
struct Resource {
	int               type;    // RESOURCE_* constant
	int               luaId;   // assigned by cache on Add/Upsert; 0 = not yet assigned
	char* source;  // heap-owned; nullptr for unsourced resources
	ResourceFinalizer fn;      // called by ResourceCacheRemove; must free node
};

// ---------------------------------------------------------------------------
// ResourceCache API
// ---------------------------------------------------------------------------

// Initialises the global resource cache. Called once at session start.
void ResourceCacheInit();

// Tears down the global resource cache: calls fn on every live node,
// then frees the slot array. Called once at session teardown.
void ResourceCacheShutdown();

// Adds node to the cache. Assigns node->luaId from the internal counter.
// Returns false if a node with the same type+source already exists, or on OOM.
bool ResourceCacheAdd(Resource* node);

// If no node with the same type+source exists, adds it (assigns luaId).
// If one does exist, copies its luaId onto the new node, calls fn on the old
// node (replacing it), and stores the new node in its place.
// Returns false on OOM.
bool ResourceCacheUpsert(Resource* node);

// Removes the node matching luaId and type, calls its fn, returns false if not found.
bool ResourceCacheRemoveById(int luaId, int type);

// Removes the node matching source and type, calls its fn, returns false if not found.
bool ResourceCacheRemoveBySource(const char* source, int type);

// Clears all nodes of the given type by calling the finalizer on each.
void ResourceCacheClearType(int type);

// Looks up by source string and type. Returns nullptr if not found.
Resource* ResourceCacheGetBySource(const char* source, int type);

// Looks up by luaId and type. Returns nullptr if not found.
Resource* ResourceCacheGetById(int luaId, int type);

// Iterator callback. Return true to continue, false to stop.
typedef bool (*ResourceCacheIteratorFn)(Resource* node, const void* userdata);

// Iterates all non-null slots, calling fn for each until fn returns false.
void ResourceCacheIterate(ResourceCacheIteratorFn fn, const void* userdata);

// Returns the next available luaId without consuming it. Useful for previewing.
int ResourceCachePeekNextId();
