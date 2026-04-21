#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "KitsuneEngine.h"

#define RESOURCE_INVALID     0
#define RESOURCE_TEXTURE     1
#define RESOURCE_AUDIO_SFX   2
#define RESOURCE_AUDIO_MUSIC 3
#define RESOURCE_FONT        4
#define RESOURCE_GENERIC     5

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

// Generic raw-bytes resource. data may be nullptr when the resource exists as
// a sentinel (Replace called with nil) — callers must check before using data.
struct GenericResource {
	Resource  resource;   // type=RESOURCE_GENERIC; luaId and source live here
	uint8_t*  data;       // heap-owned byte buffer; nullptr = empty sentinel
	size_t    length;     // byte count; 0 when data is nullptr
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

// ---------------------------------------------------------------------------
// Resource loader
//
// A single session-wide loader handles all resource types so Lua has one place
// to route load requests regardless of subsystem.
//
// Lua signatures:
//   loader(type, source)              -> stream | nil
//   postLoader(type, luaId, source)   -- optional, called after successful load
//
// type is a RESOURCE_* integer constant so the script knows what is being
// requested and can return the correct stream or nil to signal unknown.
// ---------------------------------------------------------------------------

// Registers Resource.SetLoader into Lua. Called from RegisterImguiFunctions().
void ResourceCacheRegisterLoaderFunction();

// Frees the anchored loader/postLoader Lua variables. Called at session teardown
// before ResourceCacheShutdown so no callbacks fire during finalizers.
void ResourceCacheShutdownLoader();

// Returns true if a loader function is currently set.
bool ResourceCacheLoaderIsSet();

// Calls loader(type, source) -> stream | nil.
// Returns an anchored KitsuneVariable on success; caller must KitsuneVariableFree it.
// Returns nullptr if no loader is set, source is null, or loader returned nil/none/error.
KitsuneVariable* ResourceCacheCallLoader(int type, const char* source, int sourceLen);

// Calls postLoader(type, luaId, source) if one is set. No-op otherwise.
void ResourceCacheCallPostLoader(int type, int luaId, const char* source, int sourceLen);
