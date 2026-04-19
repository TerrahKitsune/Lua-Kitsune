#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "ResourceCache.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Global cache state — not exposed externally
// ---------------------------------------------------------------------------

static Resource** s_slots = nullptr;
static int        s_count = 0;
static int        s_alloc = 0;
static int        s_nextId = 1;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static bool sources_match(const Resource* node, const char* source) {
	if (!node->source && !source)
		return true;
	if (!node->source || !source)
		return false;
	return strcmp(node->source, source) == 0;
}

static int find_free_slot() {
	for (int i = 0; i < s_count; i++) {
		if (!s_slots[i])
			return i;
	}
	if (s_count >= s_alloc) {
		int newAlloc = s_alloc == 0 ? 16 : s_alloc * 2;
		Resource** grown = (Resource**)realloc(s_slots, (size_t)newAlloc * sizeof(Resource*));
		if (!grown)
			return -1;
		s_slots = grown;
		s_alloc = newAlloc;
	}
	return s_count++;
}

static void free_node(Resource* node) {
	if (!node)
		return;
	if (node->fn) {
		node->fn(node);
		return;
	}
	// Default: free source and the node itself
	free(node->source);
	free(node);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ResourceCacheInit() {
	s_slots = nullptr;
	s_count = 0;
	s_alloc = 0;
	s_nextId = 1;
}

void ResourceCacheShutdown() {
	if (s_slots) {
		for (int i = 0; i < s_count; i++) {
			if (s_slots[i])
				free_node(s_slots[i]);
			s_slots[i] = nullptr;
		}
		free(s_slots);
		s_slots = nullptr;
	}
	s_count = 0;
	s_alloc = 0;
	s_nextId = 1;
}

bool ResourceCacheAdd(Resource* node) {
	if (!node)
		return false;

	if (node->source) {
		for (int i = 0; i < s_count; i++) {
			if (s_slots[i] &&
				s_slots[i]->type == node->type &&
				sources_match(s_slots[i], node->source))
				return false;
		}
	}

	int slot = find_free_slot();
	if (slot < 0)
		return false;

	node->luaId = s_nextId++;
	s_slots[slot] = node;
	return true;
}

bool ResourceCacheUpsert(Resource* node) {
	if (!node)
		return false;

	if (node->source) {
		for (int i = 0; i < s_count; i++) {
			if (s_slots[i] &&
				s_slots[i]->type == node->type &&
				sources_match(s_slots[i], node->source)) {
				node->luaId = s_slots[i]->luaId;
				free_node(s_slots[i]);
				s_slots[i] = node;
				return true;
			}
		}
	}

	int slot = find_free_slot();
	if (slot < 0)
		return false;

	node->luaId = s_nextId++;
	s_slots[slot] = node;
	return true;
}

bool ResourceCacheRemoveById(int luaId, int type) {
	if (luaId <= 0)
		return false;
	for (int i = 0; i < s_count; i++) {
		if (s_slots[i] &&
			s_slots[i]->type == type &&
			s_slots[i]->luaId == luaId) {
			Resource* node = s_slots[i];
			s_slots[i] = nullptr;
			free_node(node);
			return true;
		}
	}
	return false;
}

bool ResourceCacheRemoveBySource(const char* source, int type) {
	if (!source)
		return false;
	for (int i = 0; i < s_count; i++) {
		if (s_slots[i] &&
			s_slots[i]->type == type &&
			sources_match(s_slots[i], source)) {
			Resource* node = s_slots[i];
			s_slots[i] = nullptr;
			free_node(node);
			return true;
		}
	}
	return false;
}

void ResourceCacheClearType(int type) {
	for (int i = 0; i < s_count; i++) {
		if (s_slots[i] && s_slots[i]->type == type) {
			Resource* node = s_slots[i];
			s_slots[i] = nullptr;
			free_node(node);
		}
	}
}

Resource* ResourceCacheGetBySource(const char* source, int type) {
	if (!source)
		return nullptr;
	for (int i = 0; i < s_count; i++) {
		if (s_slots[i] &&
			s_slots[i]->type == type &&
			sources_match(s_slots[i], source))
			return s_slots[i];
	}
	return nullptr;
}

Resource* ResourceCacheGetById(int luaId, int type) {
	if (luaId <= 0)
		return nullptr;
	for (int i = 0; i < s_count; i++) {
		if (s_slots[i] &&
			s_slots[i]->type == type &&
			s_slots[i]->luaId == luaId)
			return s_slots[i];
	}
	return nullptr;
}

void ResourceCacheIterate(ResourceCacheIteratorFn fn, const void* userdata) {
	if (!fn)
		return;
	for (int i = 0; i < s_count; i++) {
		if (s_slots[i]) {
			if (!fn(s_slots[i], userdata))
				return;
		}
	}
}

int ResourceCachePeekNextId() {
	return s_nextId;
}

// ---------------------------------------------------------------------------
// Resource loader state
// ---------------------------------------------------------------------------

static KitsuneVariable* s_loader = nullptr;
static KitsuneVariable* s_postLoader = nullptr;

static int ResourceSetLoader(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (s_loader) {
		KitsuneVariableFree(s_loader);
		s_loader = nullptr;
	}
	if (s_postLoader) {
		KitsuneVariableFree(s_postLoader);
		s_postLoader = nullptr;
	}
	if (argc > 0 && argv[0].type == KITSUNE_TFUNCTION)
		s_loader = KitsuneAnchorVariable(&argv[0]);
	if (argc > 1 && argv[1].type == KITSUNE_TFUNCTION)
		s_postLoader = KitsuneAnchorVariable(&argv[1]);
	return 0;
}

void ResourceCacheShutdownLoader() {
	if (s_loader) {
		KitsuneVariableFree(s_loader);
		s_loader = nullptr;
	}
	if (s_postLoader) {
		KitsuneVariableFree(s_postLoader);
		s_postLoader = nullptr;
	}
}

bool ResourceCacheLoaderIsSet() {
	return s_loader != nullptr;
}

KitsuneVariable* ResourceCacheCallLoader(int type, const char* source, int sourceLen) {
	if (!s_loader || !source || sourceLen <= 0)
		return nullptr;

	KitsuneVariable typeArg = {};
	typeArg.type = KITSUNE_TINTEGER;
	typeArg.integer = type;

	KitsuneVariable sourceArg = {};
	sourceArg.type = KITSUNE_TSTRING;
	sourceArg.data = (unsigned char*)source;
	sourceArg.length = (unsigned int)sourceLen;

	KitsuneVariable args[2];
	args[0] = typeArg;
	args[1] = sourceArg;

	KitsuneVariable* result = KitsuneExecuteVariable(s_loader, 2, args);
	if (!result || result->type == KITSUNE_TNIL || result->type == KITSUNE_TNONE) {
		KitsuneVariableFree(result);
		return nullptr;
	}
	if (result->type == KITSUNE_TERROR) {
		fprintf(stderr, "Resource.SetLoader error for type %d '%.*s': %.*s\n",
			type, sourceLen, source, (int)result->length, (char*)result->data);
		KitsuneVariableFree(result);
		return nullptr;
	}
	return result;
}

void ResourceCacheCallPostLoader(int type, int luaId, const char* source, int sourceLen) {
	if (!s_postLoader)
		return;

	KitsuneVariable typeArg = {};
	typeArg.type = KITSUNE_TINTEGER;
	typeArg.integer = type;

	KitsuneVariable idArg = {};
	idArg.type = KITSUNE_TINTEGER;
	idArg.integer = luaId;

	KitsuneVariable sourceArg = {};
	sourceArg.type = KITSUNE_TSTRING;
	sourceArg.data = (unsigned char*)(source ? source : "");
	sourceArg.length = (unsigned int)(source ? sourceLen : 0);

	KitsuneVariable args[3];
	args[0] = typeArg;
	args[1] = idArg;
	args[2] = sourceArg;

	KitsuneVariable* result = KitsuneExecuteVariable(s_postLoader, 3, args);
	KitsuneVariableFree(result);
}

// ---------------------------------------------------------------------------
// Resource.GetType(id) -> integer (0 = invalid/unknown)
// ---------------------------------------------------------------------------

static int ResourceGetType(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TINTEGER;
	r.integer = 0;
	if (argc >= 1) {
		int luaId = (int)KitsuneAsInt(&argv[0], 0);
		if (luaId > 0) {
			// Check each known type
			const int types[] = { RESOURCE_TEXTURE, RESOURCE_AUDIO_SFX, RESOURCE_AUDIO_MUSIC };
			for (int i = 0; i < (int)(sizeof(types) / sizeof(types[0])); i++) {
				Resource* res = ResourceCacheGetById(luaId, types[i]);
				if (res) {
					r.integer = res->type;
					break;
				}
			}
		}
	}
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// Resource.GetSource(id) -> string | nil
// ---------------------------------------------------------------------------

static int ResourceGetSource(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (argc < 1) {
		KitsuneVariable r = {};
		r.type = KITSUNE_TNIL;
		setter(&r);
		return 1;
	}
	int luaId = (int)KitsuneAsInt(&argv[0], 0);
	Resource* res = nullptr;
	if (luaId > 0) {
		const int types[] = { RESOURCE_TEXTURE, RESOURCE_AUDIO_SFX, RESOURCE_AUDIO_MUSIC };
		for (int i = 0; i < (int)(sizeof(types) / sizeof(types[0])); i++) {
			res = ResourceCacheGetById(luaId, types[i]);
			if (res)
				break;
		}
	}
	KitsuneVariable r = {};
	if (res && res->source) {
		r.type = KITSUNE_TSTRING;
		r.data = (unsigned char*)res->source;
		r.length = strlen(res->source);
	}
	else {
		r.type = KITSUNE_TNIL;
	}
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// Resource.Destroy(id) — removes and finalizes any resource type by luaId
// ---------------------------------------------------------------------------

static int ResourceDestroy(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (argc < 1)
		return 0;
	int luaId = (int)KitsuneAsInt(&argv[0], 0);
	if (luaId <= 0)
		return 0;
	const int types[] = { RESOURCE_TEXTURE, RESOURCE_AUDIO_SFX, RESOURCE_AUDIO_MUSIC };
	for (int i = 0; i < (int)(sizeof(types) / sizeof(types[0])); i++) {
		if (ResourceCacheRemoveById(luaId, types[i]))
			break;
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Resource.GetAll() -> table of { id, type, source } tables
// ---------------------------------------------------------------------------

struct GetAllState { KitsuneVariable* tbl; int seq; };

static bool get_all_iter(Resource* res, const void* ud) {
	GetAllState* s = (GetAllState*)ud;

	KitsuneVariable entryVar = {};
	entryVar.type = KITSUNE_TTABLECONTENTS;
	entryVar.table = nullptr;
	KitsuneVariable* entry = KitsuneAnchorVariable(&entryVar);
	if (!entry)
		return true;

	KitsuneVariable idKey = {};
	idKey.type = KITSUNE_TSTRING;
	idKey.data = (unsigned char*)"id";
	idKey.length = 2;

	KitsuneVariable typeKey = {};
	typeKey.type = KITSUNE_TSTRING;
	typeKey.data = (unsigned char*)"type";
	typeKey.length = 4;

	KitsuneVariable sourceKey = {};
	sourceKey.type = KITSUNE_TSTRING;
	sourceKey.data = (unsigned char*)"source";
	sourceKey.length = 6;

	KitsuneVariable idVal = {};
	idVal.type = KITSUNE_TINTEGER;
	idVal.integer = res->luaId;

	KitsuneVariable typeVal = {};
	typeVal.type = KITSUNE_TINTEGER;
	typeVal.integer = res->type;

	KitsuneVariable sourceVal = {};
	if (res->source) {
		sourceVal.type = KITSUNE_TSTRING;
		sourceVal.data = (unsigned char*)res->source;
		sourceVal.length = strlen(res->source);
	}
	else {
		sourceVal.type = KITSUNE_TNIL;
	}

	KitsuneSetIndex(entry, &idKey, &idVal);
	KitsuneSetIndex(entry, &typeKey, &typeVal);
	KitsuneSetIndex(entry, &sourceKey, &sourceVal);

	KitsuneVariable seqKey = {};
	seqKey.type = KITSUNE_TINTEGER;
	seqKey.integer = s->seq++;
	KitsuneSetIndex(s->tbl, &seqKey, entry);
	KitsuneVariableFree(entry);
	return true;
}

static int ResourceGetAll(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable tableVar = {};
	tableVar.type = KITSUNE_TTABLECONTENTS;
	tableVar.table = nullptr;
	KitsuneVariable* tbl = KitsuneAnchorVariable(&tableVar);
	if (!tbl)
		return 0;

	GetAllState s = { tbl, 1 };
	ResourceCacheIterate(get_all_iter, &s);

	setter(tbl);
	KitsuneVariableFree(tbl);
	return 1;
}

void ResourceCacheRegisterLoaderFunction() {
	KitsuneRegisterFunction("Resource.SetLoader", ResourceSetLoader, nullptr);
	KitsuneRegisterFunction("Resource.GetType", ResourceGetType, nullptr);
	KitsuneRegisterFunction("Resource.GetSource", ResourceGetSource, nullptr);
	KitsuneRegisterFunction("Resource.Destroy", ResourceDestroy, nullptr);
	KitsuneRegisterFunction("Resource.GetAll", ResourceGetAll, nullptr);
}