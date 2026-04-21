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

// Finalizer for GenericResource nodes.
static void generic_free(Resource* node) {
	GenericResource* g = (GenericResource*)node;
	free(g->data);
	free(g->resource.source);
	free(g);
}

// Read bytes from a stream or string argument into a heap buffer.
// argv[i] may be a KITSUNE_TUSERDATA stream or a KITSUNE_TSTRING.
// Returns heap-allocated buffer and sets *outLen on success; returns nullptr on failure.
static uint8_t* read_generic_input(const KitsuneVariable* arg, size_t* outLen) {
	*outLen = 0;
	if (!arg)
		return nullptr;
	if (arg->type == KITSUNE_TSTRING) {
		if (arg->length == 0)
			return nullptr;
		uint8_t* buf = (uint8_t*)malloc(arg->length);
		if (!buf)
			return nullptr;
		memcpy(buf, arg->data, arg->length);
		*outLen = arg->length;
		return buf;
	}
	if (arg->type == KITSUNE_TUSERDATA) {
		KitsuneVariable seekArg = {};
		seekArg.type = KITSUNE_TINTEGER;
		seekArg.integer = 0;
		KitsuneVariable* r = KitsuneCallMethod(arg, "Seek", 1, &seekArg);
		bool ok = r && r->type != KITSUNE_TERROR;
		KitsuneVariableFree(r);
		if (!ok)
			return nullptr;
		KitsuneVariable* readResult = KitsuneCallMethod(arg, "Read", 0, nullptr);
		if (!readResult || readResult->type != KITSUNE_TSTRING || readResult->length == 0) {
			KitsuneVariableFree(readResult);
			return nullptr;
		}
		uint8_t* buf = (uint8_t*)malloc(readResult->length);
		if (!buf) {
			KitsuneVariableFree(readResult);
			return nullptr;
		}
		memcpy(buf, readResult->data, readResult->length);
		*outLen = readResult->length;
		KitsuneVariableFree(readResult);
		return buf;
	}
	return nullptr;
}

static int ResourceGetType(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TINTEGER;
	r.integer = 0;
	if (argc >= 1) {
		int luaId = (int)KitsuneAsInt(&argv[0], 0);
		if (luaId > 0) {
			// Check each known type
			const int types[] = { RESOURCE_TEXTURE, RESOURCE_AUDIO_SFX, RESOURCE_AUDIO_MUSIC, RESOURCE_FONT, RESOURCE_GENERIC };
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
		const int types[] = { RESOURCE_TEXTURE, RESOURCE_AUDIO_SFX, RESOURCE_AUDIO_MUSIC, RESOURCE_FONT, RESOURCE_GENERIC };
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
	const int types[] = { RESOURCE_TEXTURE, RESOURCE_AUDIO_SFX, RESOURCE_AUDIO_MUSIC, RESOURCE_FONT, RESOURCE_GENERIC };
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

// Builds a single { id, type, source } table for res into an anchored variable.
// Caller must KitsuneVariableFree the returned pointer. Returns nullptr on OOM.
static KitsuneVariable* build_resource_entry(const Resource* res) {
	KitsuneVariable entryVar = {};
	entryVar.type = KITSUNE_TTABLECONTENTS;
	entryVar.table = nullptr;
	KitsuneVariable* entry = KitsuneAnchorVariable(&entryVar);
	if (!entry)
		return nullptr;

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
	return entry;
}

static bool get_all_iter(Resource* res, const void* ud) {
	GetAllState* s = (GetAllState*)ud;
	KitsuneVariable* entry = build_resource_entry(res);
	if (!entry)
		return true;
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

// ---------------------------------------------------------------------------
// Resource.Resolve(source) -> luaId | nil
// Looks up RESOURCE_GENERIC by source. On miss calls the loader; if the loader
// returns a stream or string the bytes are stored as a new GenericResource.
// Safe to call every frame — returns the cached id on hit with no allocation.
// ---------------------------------------------------------------------------

static int ResourceResolve(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TNIL;
	if (argc < 1 || argv[0].type != KITSUNE_TSTRING || argv[0].length == 0) {
		setter(&r);
		return 1;
	}
	const char* source = (const char*)argv[0].data;
	int sourceLen = (int)argv[0].length;
	Resource* existing = ResourceCacheGetBySource(source, RESOURCE_GENERIC);
	if (existing) {
		r.type = KITSUNE_TINTEGER;
		r.integer = existing->luaId;
		setter(&r);
		return 1;
	}
	KitsuneVariable* streamVar = ResourceCacheCallLoader(RESOURCE_GENERIC, source, sourceLen);
	if (!streamVar) {
		setter(&r);
		return 1;
	}
	size_t len = 0;
	uint8_t* buf = read_generic_input(streamVar, &len);
	KitsuneVariableFree(streamVar);
	if (!buf) {
		setter(&r);
		return 1;
	}
	GenericResource* g = (GenericResource*)calloc(1, sizeof(GenericResource));
	if (!g) {
		free(buf);
		setter(&r);
		return 1;
	}
	g->resource.type = RESOURCE_GENERIC;
	g->resource.source = (char*)malloc(sourceLen + 1);
	if (g->resource.source) {
		memcpy(g->resource.source, source, sourceLen);
		g->resource.source[sourceLen] = '\0';
	}
	g->resource.fn = generic_free;
	g->data = buf;
	g->length = len;
	if (!ResourceCacheAdd(&g->resource)) {
		generic_free(&g->resource);
		setter(&r);
		return 1;
	}
	ResourceCacheCallPostLoader(RESOURCE_GENERIC, g->resource.luaId, source, sourceLen);
	r.type = KITSUNE_TINTEGER;
	r.integer = g->resource.luaId;
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// Resource.Load(stream_or_string, source) -> luaId | nil
// Stores bytes directly as RESOURCE_GENERIC. If a node with the same source
// already exists it is replaced (same luaId). source is optional.
// ---------------------------------------------------------------------------

static int ResourceLoad(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TNIL;
	if (argc < 1) {
		setter(&r);
		return 1;
	}
	size_t len = 0;
	uint8_t* buf = read_generic_input(&argv[0], &len);
	if (!buf) {
		setter(&r);
		return 1;
	}
	const char* source = nullptr;
	int sourceLen = 0;
	if (argc >= 2 && argv[1].type == KITSUNE_TSTRING && argv[1].length > 0) {
		source = (const char*)argv[1].data;
		sourceLen = (int)argv[1].length;
	}
	GenericResource* g = (GenericResource*)calloc(1, sizeof(GenericResource));
	if (!g) {
		free(buf);
		setter(&r);
		return 1;
	}
	g->resource.type = RESOURCE_GENERIC;
	if (source) {
		g->resource.source = (char*)malloc(sourceLen + 1);
		if (g->resource.source) {
			memcpy(g->resource.source, source, sourceLen);
			g->resource.source[sourceLen] = '\0';
		}
	}
	g->resource.fn = generic_free;
	g->data = buf;
	g->length = len;
	if (!ResourceCacheUpsert(&g->resource)) {
		generic_free(&g->resource);
		setter(&r);
		return 1;
	}
	r.type = KITSUNE_TINTEGER;
	r.integer = g->resource.luaId;
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// Resource.Replace(luaId, stream_or_string_or_nil) -> true | false
// Replaces the data of an existing RESOURCE_GENERIC in place, preserving its
// luaId and source. Passing nil zeroes the buffer (data=nullptr, length=0) —
// the node remains in the cache as an empty sentinel; callers must check data.
// ---------------------------------------------------------------------------

static int ResourceReplace(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TBOOLEAN;
	r.boolean = false;
	if (argc < 1) {
		setter(&r);
		return 1;
	}
	int luaId = (int)KitsuneAsInt(&argv[0], 0);
	if (luaId <= 0) {
		setter(&r);
		return 1;
	}
	GenericResource* g = (GenericResource*)ResourceCacheGetById(luaId, RESOURCE_GENERIC);
	if (!g) {
		setter(&r);
		return 1;
	}
	free(g->data);
	g->data = nullptr;
	g->length = 0;
	if (argc >= 2 && argv[1].type != KITSUNE_TNIL && argv[1].type != KITSUNE_TNONE) {
		size_t len = 0;
		uint8_t* buf = read_generic_input(&argv[1], &len);
		if (!buf) {
			setter(&r);
			return 1;
		}
		g->data = buf;
		g->length = len;
	}
	r.boolean = true;
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// Resource.Get(luaId) -> string | "" | nil
// Returns RESOURCE_GENERIC contents as a Lua string.
// Returns "" if the node exists but data is nullptr (empty sentinel).
// Returns nil if luaId is not a RESOURCE_GENERIC or does not exist.
// ---------------------------------------------------------------------------

static int ResourceGet(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TNIL;
	if (argc < 1) {
		setter(&r);
		return 1;
	}
	int luaId = (int)KitsuneAsInt(&argv[0], 0);
	if (luaId <= 0) {
		setter(&r);
		return 1;
	}
	GenericResource* g = (GenericResource*)ResourceCacheGetById(luaId, RESOURCE_GENERIC);
	if (!g) {
		setter(&r);
		return 1;
	}
	r.type = KITSUNE_TSTRING;
	r.data = g->data ? g->data : (unsigned char*)"";
	r.length = g->data ? (unsigned int)g->length : 0;
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// Resource.Info(luaId) -> { id, type, source } | nil
// Returns a single entry table for any resource type, or nil if not found.
// ---------------------------------------------------------------------------

static int ResourceInfo(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TNIL;
	if (argc < 1) {
		setter(&r);
		return 1;
	}
	int luaId = (int)KitsuneAsInt(&argv[0], 0);
	if (luaId <= 0) {
		setter(&r);
		return 1;
	}
	const int types[] = { RESOURCE_TEXTURE, RESOURCE_AUDIO_SFX, RESOURCE_AUDIO_MUSIC, RESOURCE_FONT, RESOURCE_GENERIC };
	Resource* res = nullptr;
	for (int i = 0; i < (int)(sizeof(types) / sizeof(types[0])); i++) {
		res = ResourceCacheGetById(luaId, types[i]);
		if (res)
			break;
	}
	if (!res) {
		setter(&r);
		return 1;
	}
	KitsuneVariable* entry = build_resource_entry(res);
	if (!entry) {
		setter(&r);
		return 1;
	}
	setter(entry);
	KitsuneVariableFree(entry);
	return 1;
}

// ---------------------------------------------------------------------------
// Resource.GetIdBySource(source, opt type) -> luaId | nil
// Looks up a resource by source string. type defaults to RESOURCE_GENERIC.
// ---------------------------------------------------------------------------

static int ResourceGetIdBySource(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TNIL;
	if (argc < 1 || argv[0].type != KITSUNE_TSTRING || argv[0].length == 0) {
		setter(&r);
		return 1;
	}
	const char* source = (const char*)argv[0].data;
	int type = RESOURCE_GENERIC;
	if (argc >= 2 && (argv[1].type == KITSUNE_TINTEGER || argv[1].type == KITSUNE_TNUMBER))
		type = (int)KitsuneAsInt(&argv[1], RESOURCE_GENERIC);
	Resource* res = ResourceCacheGetBySource(source, type);
	if (res) {
		r.type = KITSUNE_TINTEGER;
		r.integer = res->luaId;
	}
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// Resource.GetAllIds(opt allTypes) -> flat integer array of luaIds
// allTypes=true returns ids for all resource types.
// allTypes=false/nil (default) returns only RESOURCE_GENERIC ids.
// ---------------------------------------------------------------------------

struct GetAllIdsState { KitsuneVariable* tbl; int seq; bool allTypes; };

static bool get_all_ids_iter(Resource* res, const void* ud) {
	GetAllIdsState* s = (GetAllIdsState*)ud;
	if (!s->allTypes && res->type != RESOURCE_GENERIC)
		return true;
	KitsuneVariable seqKey = {};
	seqKey.type = KITSUNE_TINTEGER;
	seqKey.integer = s->seq++;
	KitsuneVariable val = {};
	val.type = KITSUNE_TINTEGER;
	val.integer = res->luaId;
	KitsuneSetIndex(s->tbl, &seqKey, &val);
	return true;
}

static int ResourceGetAllIds(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	bool allTypes = argc >= 1 && KitsuneAsBool(&argv[0]);
	KitsuneVariable tableVar = {};
	tableVar.type = KITSUNE_TTABLECONTENTS;
	tableVar.table = nullptr;
	KitsuneVariable* tbl = KitsuneAnchorVariable(&tableVar);
	if (!tbl)
		return 0;
	GetAllIdsState s = { tbl, 1, allTypes };
	ResourceCacheIterate(get_all_ids_iter, &s);
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
	KitsuneRegisterFunction("Resource.Resolve", ResourceResolve, nullptr);
	KitsuneRegisterFunction("Resource.Load", ResourceLoad, nullptr);
	KitsuneRegisterFunction("Resource.Replace", ResourceReplace, nullptr);
	KitsuneRegisterFunction("Resource.Get", ResourceGet, nullptr);
	KitsuneRegisterFunction("Resource.Info", ResourceInfo, nullptr);
	KitsuneRegisterFunction("Resource.GetIdBySource", ResourceGetIdBySource, nullptr);
	KitsuneRegisterFunction("Resource.GetAllIds", ResourceGetAllIds, nullptr);
}