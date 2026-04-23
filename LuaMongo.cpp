#include "platform.h"
#include "LuaMongo.h"
#include "stream.h"
#include "luawchar.h"
#include "luaidentifier.h"
#include "luadatetime.h"
#include "luadecimal.h"
#include "luauint.h"
#include "luatimespan.h"

#ifdef KITSUNE_MONGO

const char* LUAMONGO = "LuaMongo";

#include <mongoc/mongoc.h>
#include <thread>
#include <system_error>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cmath>

#ifdef _WIN32
#include <processthreadsapi.h>
#else
#include <pthread.h>
#endif

// =============================================================================
// Op types and states
// =============================================================================

enum MongoOpType : int {
	MONGO_OP_FIND = 0,
	MONGO_OP_FIND_ONE = 1,
	MONGO_OP_INSERT_ONE = 2,
	MONGO_OP_INSERT_MANY = 3,
	MONGO_OP_UPDATE_ONE = 4,
	MONGO_OP_UPDATE_MANY = 5,
	MONGO_OP_DELETE_ONE = 6,
	MONGO_OP_DELETE_MANY = 7,
	MONGO_OP_AGGREGATE = 8,
	MONGO_OP_COMMAND = 9,
	MONGO_OP_COUNT = 10,
};

enum MongoState : int {
	MONGO_STATE_IDLE = 0,
	MONGO_STATE_RUNNING = 1,
	MONGO_STATE_DONE = 2,
	MONGO_STATE_ERROR = 3,
};

// =============================================================================
// LuaMongoOp — per-operation data (Lua owns; worker only writes output fields)
// =============================================================================

struct LuaMongoOp {
	// Input — written by Lua before DispatchOp; read-only by worker
	char* db;
	char* collection;
	bson_t* filter;
	bson_t* update;
	bson_t* opts;
	bson_t** insertDocs;
	int          insertCount;
	MongoOpType  opType;
	uintptr_t* ctx_rec;   // BsonWriteCtx::rec — freed by FreeOp so luaL_error paths don't leak

	// Output — written by worker before state -> DONE/ERROR; read by Lua after
	bson_t** resultDocs;
	int64_t      resultCount;
	int64_t      resultCapacity;
	int64_t      countResult;   // MONGO_OP_COUNT: the document count
	char* error;
	bool         errorIsStatic; // true when error points at a static string (do not free)
};

// =============================================================================
// LuaMongoWorker — persistent thread; one per connection
// =============================================================================

struct LuaMongoWorker {
	mongoc_client_t* client;
	std::thread       thread;
	PlatformEvent     workReady;
	PlatformEvent     workDone;
	std::atomic<int>  state;
	std::atomic<bool> stop;
	std::atomic<bool> cancelled;
	LuaMongoOp* op;
};

// =============================================================================
// FreeOp — frees op data only; never touches the worker thread or client
// =============================================================================

static void FreeOp(LuaMongoOp* op) {
	if (!op)
		return;

	for (int64_t i = 0; i < op->resultCount; i++)
		bson_destroy(op->resultDocs[i]);
	kitsune_free(op->resultDocs);
	if (op->error && !op->errorIsStatic)
		kitsune_free(op->error);

	if (op->filter) bson_destroy(op->filter);
	if (op->update) bson_destroy(op->update);
	if (op->opts)   bson_destroy(op->opts);
	for (int i = 0; i < op->insertCount; i++)
		bson_destroy(op->insertDocs[i]);
	kitsune_free(op->insertDocs);
	kitsune_free(op->ctx_rec);
	kitsune_free(op->db);
	kitsune_free(op->collection);
	delete op;
}

// =============================================================================
// BSON <-> Lua type mapping
// =============================================================================

static void BsonToLua(lua_State* L, const bson_t* doc, int depth);

static void BsonIterValueToLua(lua_State* L, bson_iter_t* iter, int depth) {
	if (depth > 32)
		luaL_error(L, "MongoDB: BSON document nesting too deep");

	switch (bson_iter_type(iter)) {
	case BSON_TYPE_UTF8: {
		uint32_t len;
		const char* str = bson_iter_utf8(iter, &len);
		lua_pushlstring(L, str, len);
		break;
	}
	case BSON_TYPE_INT32:
		lua_pushinteger(L, bson_iter_int32(iter));
		break;
	case BSON_TYPE_INT64:
		lua_pushinteger(L, (lua_Integer)bson_iter_int64(iter));
		break;
	case BSON_TYPE_DOUBLE:
		lua_pushnumber(L, bson_iter_double(iter));
		break;
	case BSON_TYPE_BOOL:
		lua_pushboolean(L, bson_iter_bool(iter));
		break;
	case BSON_TYPE_OID: {
		const bson_oid_t* oid = bson_iter_oid(iter);
		LuaIdentifier* id = lua_pushidentifier(L);
		id->type = IDENTIFIER_OID;
		id->len = 12;
		memcpy(id->bytes, oid->bytes, 12);
		break;
	}
	case BSON_TYPE_DATE_TIME: {
		int64_t ms = bson_iter_date_time(iter);
		LuaDateTime* dt = lua_pushdatetime(L);
		dt->ticks = ms * DT_TICKS_PER_MILLISECOND + DT_UNIX_EPOCH_TICKS;
		dt->offset_minutes = 0;
		break;
	}
	case BSON_TYPE_BINARY: {
		bson_subtype_t subtype;
		uint32_t       blen;
		const uint8_t* data;
		bson_iter_binary(iter, &subtype, &blen, &data);
		if (subtype == BSON_SUBTYPE_UUID && blen >= 16) {
			LuaIdentifier* id = lua_pushidentifier(L);
			id->type = IDENTIFIER_UUID;
			id->len = 16;
			memcpy(id->bytes, data, 16);
		}
		else {
			lua_pushluastream(L, data, blen);
		}
		break;
	}
	case BSON_TYPE_DECIMAL128: {
		bson_decimal128_t d128;
		bson_iter_decimal128(iter, &d128);
		char buf[BSON_DECIMAL128_STRING];
		bson_decimal128_to_string(&d128, buf);
		LuaDecimal* dec = lua_pushdecimal(L);
		decimal_parse_c(buf, strlen(buf), dec);
		break;
	}
	case BSON_TYPE_DOCUMENT: {
		const uint8_t* docdata;
		uint32_t       doclen;
		bson_iter_document(iter, &doclen, &docdata);
		bson_t child;
		bson_init_static(&child, docdata, doclen);
		BsonToLua(L, &child, depth + 1);
		break;
	}
	case BSON_TYPE_ARRAY: {
		const uint8_t* arrdata;
		uint32_t       arrlen;
		bson_iter_array(iter, &arrlen, &arrdata);
		bson_t child;
		bson_init_static(&child, arrdata, arrlen);
		bson_iter_t ci;
		bson_iter_init(&ci, &child);
		lua_newtable(L);
		int idx = 1;
		while (bson_iter_next(&ci)) {
			BsonIterValueToLua(L, &ci, depth + 1);
			lua_rawseti(L, -2, idx++);
		}
		break;
	}
	case BSON_TYPE_TIMESTAMP: {
		uint32_t ordinal, increment;
		bson_iter_timestamp(iter, &ordinal, &increment);
		lua_newtable(L);
		lua_pushinteger(L, ordinal);
		lua_setfield(L, -2, "t");
		lua_pushinteger(L, increment);
		lua_setfield(L, -2, "i");
		break;
	}
	case BSON_TYPE_REGEX: {
		const char* options;
		const char* pattern = bson_iter_regex(iter, &options);
		size_t plen = strlen(pattern);
		size_t olen = strlen(options);
		luaL_Buffer b;
		luaL_buffinit(L, &b);
		luaL_addchar(&b, '/');
		luaL_addlstring(&b, pattern, plen);
		luaL_addchar(&b, '/');
		luaL_addlstring(&b, options, olen);
		luaL_pushresult(&b);
		break;
	}
	case BSON_TYPE_CODE:
	case BSON_TYPE_CODEWSCOPE: {
		uint32_t codelen;
		const char* code = bson_iter_code(iter, &codelen);
		lua_pushlstring(L, code, codelen);
		break;
	}
	case BSON_TYPE_SYMBOL: {
		uint32_t symlen;
		const char* sym = bson_iter_symbol(iter, &symlen);
		lua_pushlstring(L, sym, symlen);
		break;
	}
	default:
		lua_pushnil(L);
		break;
	}
}

static void BsonToLua(lua_State* L, const bson_t* doc, int depth) {
	if (depth > 32)
		luaL_error(L, "MongoDB: BSON document nesting too deep");

	bson_iter_t iter;
	bson_iter_init(&iter, doc);
	lua_newtable(L);
	while (bson_iter_next(&iter)) {
		uint32_t klen;
		const char* key = bson_iter_key_unsafe(&iter);
		klen = bson_iter_key_len(&iter);
		lua_pushlstring(L, key, klen);
		BsonIterValueToLua(L, &iter, depth);
		lua_rawset(L, -3);
	}
}

// Recursion context for LuaToBson — tracks visited table addresses
struct BsonWriteCtx {
	uintptr_t* rec;
	int        recLen;
	int        recCap;
};

static void bson_rec_push(lua_State* L, BsonWriteCtx* ctx, uintptr_t addr) {
	for (int i = 0; i < ctx->recLen; i++) {
		if (ctx->rec[i] == addr) {
			kitsune_free(ctx->rec);
			ctx->rec = NULL;
			ctx->recLen = 0;
			ctx->recCap = 0;
			luaL_error(L, "MongoDB: circular reference detected in table");
		}
	}
	if (ctx->recLen == ctx->recCap) {
		int cap = ctx->recCap ? ctx->recCap * 2 : 8;
		uintptr_t* p = (uintptr_t*)kitsune_realloc(ctx->rec, cap * sizeof(uintptr_t));
		if (!p) {
			// Free the old buffer and zero the context so any op->ctx_rec
			// that still points here won't be double-freed by FreeOp.
			kitsune_free(ctx->rec);
			ctx->rec = NULL;
			ctx->recLen = 0;
			ctx->recCap = 0;
			luaL_error(L, "MongoDB: out of memory");
		}
		ctx->rec = p;
		ctx->recCap = cap;
	}
	ctx->rec[ctx->recLen++] = addr;
}

static void bson_rec_pop(BsonWriteCtx* ctx) {
	if (ctx->recLen > 0)
		ctx->recLen--;
}

static void LuaToBsonValue(lua_State* L, int idx, bson_t* doc, const char* key, BsonWriteCtx* ctx);
static void LuaToBson(lua_State* L, int idx, bson_t* doc, BsonWriteCtx* ctx);
static void LuaToBsonArray(lua_State* L, int idx, bson_t* doc, BsonWriteCtx* ctx);

// RAII guard that calls bson_rec_pop even when luaL_error throws (longjmp).
// NOTE: longjmp skips C++ destructors unless the platform uses SEH or the
// compiler is configured to unwind C++ on longjmp (MSVC /EHa, or Lua built
// with C++ exceptions).  For maximum safety the table value is removed from
// the rec stack at the start of EACH recursive LuaToBsonValue call via an
// explicit bson_rec_pop in the error handler for the OOM path in bson_rec_push,
// and here we use a plain struct + explicit call so the intent is clear.
// In practice Lua's longjmp on MSVC with /EHsc does NOT run destructors, so
// this guard only helps on platforms/configs that do.  The worst case is a
// false-positive circular-reference error on a subsequent call with the same
// table, which is safe (no memory corruption).
struct BsonRecGuard {
	BsonWriteCtx* ctx;
	BsonRecGuard(BsonWriteCtx* c) : ctx(c) {}
	~BsonRecGuard() { bson_rec_pop(ctx); }
};

static void LuaToBsonValue(lua_State* L, int idx, bson_t* doc, const char* key, BsonWriteCtx* ctx) {
	if (idx < 0)
		idx = lua_gettop(L) + idx + 1;

	// Check native userdata types first
	if (lua_isidentifier(L, idx)) {
		LuaIdentifier* id = lua_toidentifier(L, idx);
		if (id->type == IDENTIFIER_OID) {
			bson_oid_t oid;
			memcpy(oid.bytes, id->bytes, 12);
			BSON_APPEND_OID(doc, key, &oid);
		}
		else if (id->type == IDENTIFIER_UUID) {
			BSON_APPEND_BINARY(doc, key, BSON_SUBTYPE_UUID, (const uint8_t*)id->bytes, 16);
		}
		else {
			BSON_APPEND_NULL(doc, key);
		}
		return;
	}

	if (lua_isdatetime(L, idx)) {
		LuaDateTime* dt = lua_todatetime(L, idx);
		int64_t ms = (dt->ticks - DT_UNIX_EPOCH_TICKS) / DT_TICKS_PER_MILLISECOND;
		BSON_APPEND_DATE_TIME(doc, key, ms);
		return;
	}

	if (lua_isdecimal(L, idx)) {
		lua_decimal_push_string(L, idx);
		const char* buf = lua_tostring(L, -1);
		bson_decimal128_t d128;
		bson_decimal128_from_string(buf, &d128);
		BSON_APPEND_DECIMAL128(doc, key, &d128);
		lua_pop(L, 1);
		return;
	}

	if (lua_isuint(L, idx)) {
		BSON_APPEND_INT64(doc, key, (int64_t)lua_touint(L, idx)->value);
		return;
	}

	if (lua_isuint(L, idx)) {
		BSON_APPEND_INT64(doc, key, (int64_t)lua_touint(L, idx)->value);
		return;
	}

	if (lua_istimespan(L, idx)) {
		// BSON has no duration type; store as int64 milliseconds for maximum interop.
		int64_t ms = lua_totimespan(L, idx)->ticks / 10000LL;
		BSON_APPEND_INT64(doc, key, ms);
		return;
	}

	if (lua_isstream(L, idx)) {
		LuaStream* s = lua_toluastream(L, idx);
		lua_Integer pos = lua_stream_curpos(L, s);
		lua_stream_setpos(L, s, 0);
		lua_Integer slen = lua_stream_getlen(L, s);
		lua_stream_read_chunk(L, s, (size_t)slen);
		size_t rlen = 0;
		const char* sdata = lua_tolstring(L, -1, &rlen);
		if (sdata) {
			BSON_APPEND_BINARY(doc, key, BSON_SUBTYPE_BINARY, (const uint8_t*)sdata, (uint32_t)rlen);
		}
		else {
			BSON_APPEND_BINARY(doc, key, BSON_SUBTYPE_BINARY, (const uint8_t*)"", 0);
		}
		lua_pop(L, 1);
		lua_stream_setpos(L, s, pos);  // restore position
		return;
	}

	if (lua_iswchar(L, idx)) {
		LuaWChar* wc = lua_towchar(L, idx);
#ifdef _WIN32
		if (!wc->str || wc->len == 0) {
			bson_append_utf8(doc, key, -1, "", 0);
			return;
		}
		int u8len = WideCharToMultiByte(CP_UTF8, 0, wc->str, (int)wc->len, NULL, 0, NULL, NULL);
		char* u8buf = (char*)kitsune_malloc(u8len + 1);
		if (!u8buf)
			luaL_error(L, "MongoDB: out of memory");
		WideCharToMultiByte(CP_UTF8, 0, wc->str, (int)wc->len, u8buf, u8len, NULL, NULL);
		u8buf[u8len] = '\0';
		bson_append_utf8(doc, key, -1, u8buf, u8len);
		kitsune_free(u8buf);
#else
		(void)wc;
		BSON_APPEND_NULL(doc, key);
#endif
		return;
	}

	switch (lua_type(L, idx)) {
	case LUA_TSTRING: {
		size_t slen;
		const char* s = lua_tolstring(L, idx, &slen);
		// Use the explicit-length API so embedded NUL bytes are preserved.
		bson_append_utf8(doc, key, -1, s, (int)slen);
		break;
	}
	case LUA_TNUMBER:
		if (lua_isinteger(L, idx))
			BSON_APPEND_INT64(doc, key, (int64_t)lua_tointeger(L, idx));
		else {
			double d = lua_tonumber(L, idx);
			if (!std::isfinite(d))
				luaL_error(L, "MongoDB: NaN and Inf are not valid BSON double values");
			BSON_APPEND_DOUBLE(doc, key, d);
		}
		break;
	case LUA_TBOOLEAN:
		BSON_APPEND_BOOL(doc, key, lua_toboolean(L, idx) != 0);
		break;
	case LUA_TNIL:
		BSON_APPEND_NULL(doc, key);
		break;
	case LUA_TTABLE: {
		uintptr_t addr = (uintptr_t)lua_topointer(L, idx);
		bson_rec_push(L, ctx, addr);
		BsonRecGuard rg(ctx);  // pops addr even if LuaToBson/LuaToBsonArray throws

		// Detect array vs document: array only if ALL keys are sequential
		// integers starting at 1 with no string keys mixed in.
		bool isArray = false;
		lua_Integer arrLen = (lua_Integer)lua_rawlen(L, idx);
		if (arrLen > 0) {
			isArray = true;
			// Check that no string keys exist — a mixed table is a document
			lua_pushnil(L);
			while (lua_next(L, idx)) {
				if (lua_type(L, -2) == LUA_TSTRING) {
					isArray = false;
					lua_pop(L, 2);
					break;
				}
				lua_pop(L, 1);
			}
		}

		if (isArray) {
			bson_t child = BSON_INITIALIZER;
			LuaToBsonArray(L, idx, &child, ctx);
			BSON_APPEND_ARRAY(doc, key, &child);
			bson_destroy(&child);
		}
		else {
			bson_t child = BSON_INITIALIZER;
			LuaToBson(L, idx, &child, ctx);
			BSON_APPEND_DOCUMENT(doc, key, &child);
			bson_destroy(&child);
		}
		break;
	}
	default:
		BSON_APPEND_NULL(doc, key);
		break;
	}
}

static void LuaToBson(lua_State* L, int idx, bson_t* doc, BsonWriteCtx* ctx) {
	if (idx < 0)
		idx = lua_gettop(L) + idx + 1;

	lua_pushnil(L);
	while (lua_next(L, idx)) {
		if (lua_type(L, -2) == LUA_TSTRING) {
			const char* key = lua_tostring(L, -2);
			LuaToBsonValue(L, -1, doc, key, ctx);
		}
		else if (lua_type(L, -2) == LUA_TNUMBER && lua_isinteger(L, -2)) {
			char keybuf[32];
			snprintf(keybuf, sizeof(keybuf), LUA_INTEGER_FMT, lua_tointeger(L, -2));
			LuaToBsonValue(L, -1, doc, keybuf, ctx);
		}
		lua_pop(L, 1);
	}
}

static void LuaToBsonArray(lua_State* L, int idx, bson_t* doc, BsonWriteCtx* ctx) {
	if (idx < 0)
		idx = lua_gettop(L) + idx + 1;

	int n = (int)lua_rawlen(L, idx);
	for (int i = 1; i <= n; i++) {
		char key[32];
		snprintf(key, sizeof(key), "%d", i - 1);
		lua_rawgeti(L, idx, i);
		LuaToBsonValue(L, -1, doc, key, ctx);
		lua_pop(L, 1);
	}
}

// =============================================================================
// Worker thread
// =============================================================================

static void WriteError(LuaMongoWorker* w, const bson_error_t* err) {
	if (w->op->error)
		return;  // already set (e.g. by bson_copy OOM guard); don't overwrite
	size_t n = strlen(err->message);
	w->op->error = (char*)kitsune_malloc(n + 1);
	if (w->op->error) {
		memcpy(w->op->error, err->message, n + 1);
	}
	else {
		static const char fallback[] = "MongoDB: operation failed (OOM storing error message)";
		w->op->error = (char*)fallback;
		w->op->errorIsStatic = true;
	}
}

static bool AppendResultDoc(LuaMongoWorker* w, const bson_t* doc) {
	LuaMongoOp* op = w->op;
	if (op->error)
		return false;  // already in error state; don't accumulate more
	if (op->resultCount == op->resultCapacity) {
		if (op->resultCapacity <= 0) {
			// Should never happen, but guard against corruption.
			static const char bad[] = "MongoDB: result buffer capacity corrupted";
			op->error = (char*)bad;
			op->errorIsStatic = true;
			return false;
		}
		int64_t newCap = op->resultCapacity * 2;
		bson_t** newDocs = (bson_t**)kitsune_realloc(op->resultDocs, sizeof(bson_t*) * (size_t)newCap);
		if (!newDocs) {
			static const char oom[] = "MongoDB: out of memory growing result buffer";
			op->error = (char*)oom;
			op->errorIsStatic = true;
			return false;
		}
		op->resultDocs = newDocs;
		op->resultCapacity = newCap;
	}
	bson_t* copy = bson_copy(doc);
	if (!copy) {
		static const char oom[] = "MongoDB: out of memory copying document";
		op->error = (char*)oom;
		op->errorIsStatic = true;
		return false;
	}
	op->resultDocs[op->resultCount++] = copy;
	return true;
}

static void MongoWorkerThread(LuaMongoWorker* w) {
#ifdef _WIN32
	SetThreadDescription(GetCurrentThread(), L"MongoDB Worker");
#else
	pthread_setname_np(pthread_self(), "MongoDB Worker");
#endif

	while (true) {
		w->workReady.Wait();

		if (w->stop.load(std::memory_order_acquire))
			return;

		LuaMongoOp* op = w->op;

		// Check cancelled before doing any work
		if (w->cancelled.load(std::memory_order_relaxed)) {
			w->state.store(MONGO_STATE_DONE, std::memory_order_release);
			w->workDone.Set();
			continue;
		}

		bson_error_t err;
		bool ok = true;

		switch (op->opType) {
		case MONGO_OP_FIND:
		case MONGO_OP_FIND_ONE: {
			mongoc_collection_t* coll = mongoc_client_get_collection(w->client, op->db, op->collection);
			mongoc_cursor_t* cursor = mongoc_collection_find_with_opts(coll, op->filter, op->opts, NULL);
			const bson_t* doc;
			while (!w->cancelled.load(std::memory_order_relaxed) && mongoc_cursor_next(cursor, &doc)) {
				if (!AppendResultDoc(w, doc)) {
					mongoc_cursor_destroy(cursor);
					mongoc_collection_destroy(coll);
					w->state.store(MONGO_STATE_ERROR, std::memory_order_release);
					w->workDone.Set();
					goto next_iteration;
				}
				if (op->opType == MONGO_OP_FIND_ONE)
					break;
			}
			if (!w->cancelled.load(std::memory_order_relaxed) && mongoc_cursor_error(cursor, &err)) {
				WriteError(w, &err);
				mongoc_cursor_destroy(cursor);
				mongoc_collection_destroy(coll);
				w->state.store(MONGO_STATE_ERROR, std::memory_order_release);
				w->workDone.Set();
				goto next_iteration;
			}
			mongoc_cursor_destroy(cursor);
			mongoc_collection_destroy(coll);
			if (w->cancelled.load(std::memory_order_relaxed)) {
				w->state.store(MONGO_STATE_DONE, std::memory_order_release);
				w->workDone.Set();
				goto next_iteration;
			}
			break;
		}
		case MONGO_OP_INSERT_ONE: {
			mongoc_collection_t* coll = mongoc_client_get_collection(w->client, op->db, op->collection);
			bson_t reply = BSON_INITIALIZER;
			ok = mongoc_collection_insert_one(coll, op->filter, op->opts, &reply, &err);
			if (ok) {
				bson_t* copy = bson_copy(&reply);
				if (copy) {
					op->resultDocs[op->resultCount++] = copy;
				}
				else {
					static const char oom[] = "MongoDB: out of memory copying insert reply";
					op->error = (char*)oom;
					op->errorIsStatic = true;
					ok = false;
				}
			}
			bson_destroy(&reply);
			mongoc_collection_destroy(coll);
			if (!ok) {
				if (!op->error) WriteError(w, &err);
				w->state.store(MONGO_STATE_ERROR, std::memory_order_release);
				w->workDone.Set();
				goto next_iteration;
			}
			break;
		}
		case MONGO_OP_INSERT_MANY: {
			mongoc_collection_t* coll = mongoc_client_get_collection(w->client, op->db, op->collection);
			bson_t reply = BSON_INITIALIZER;
			ok = mongoc_collection_insert_many(coll,
				(const bson_t**)op->insertDocs, (size_t)op->insertCount,
				op->opts, &reply, &err);
			if (ok) {
				bson_t* copy = bson_copy(&reply);
				if (copy) {
					op->resultDocs[op->resultCount++] = copy;
				}
				else {
					static const char oom[] = "MongoDB: out of memory copying insert reply";
					op->error = (char*)oom;
					op->errorIsStatic = true;
					ok = false;
				}
			}
			bson_destroy(&reply);
			mongoc_collection_destroy(coll);
			if (!ok) {
				if (!op->error) WriteError(w, &err);
				w->state.store(MONGO_STATE_ERROR, std::memory_order_release);
				w->workDone.Set();
				goto next_iteration;
			}
			break;
		}
		case MONGO_OP_UPDATE_ONE: {
			mongoc_collection_t* coll = mongoc_client_get_collection(w->client, op->db, op->collection);
			bson_t reply = BSON_INITIALIZER;
			ok = mongoc_collection_update_one(coll, op->filter, op->update, op->opts, &reply, &err);
			if (ok) {
				bson_t* copy = bson_copy(&reply);
				if (copy) {
					op->resultDocs[op->resultCount++] = copy;
				}
				else {
					static const char oom[] = "MongoDB: out of memory copying update reply";
					op->error = (char*)oom;
					op->errorIsStatic = true;
					ok = false;
				}
			}
			bson_destroy(&reply);
			mongoc_collection_destroy(coll);
			if (!ok) {
				if (!op->error) WriteError(w, &err);
				w->state.store(MONGO_STATE_ERROR, std::memory_order_release);
				w->workDone.Set();
				goto next_iteration;
			}
			break;
		}
		case MONGO_OP_UPDATE_MANY: {
			mongoc_collection_t* coll = mongoc_client_get_collection(w->client, op->db, op->collection);
			bson_t reply = BSON_INITIALIZER;
			ok = mongoc_collection_update_many(coll, op->filter, op->update, op->opts, &reply, &err);
			if (ok) {
				bson_t* copy = bson_copy(&reply);
				if (copy) {
					op->resultDocs[op->resultCount++] = copy;
				}
				else {
					static const char oom[] = "MongoDB: out of memory copying update reply";
					op->error = (char*)oom;
					op->errorIsStatic = true;
					ok = false;
				}
			}
			bson_destroy(&reply);
			mongoc_collection_destroy(coll);
			if (!ok) {
				if (!op->error) WriteError(w, &err);
				w->state.store(MONGO_STATE_ERROR, std::memory_order_release);
				w->workDone.Set();
				goto next_iteration;
			}
			break;
		}
		case MONGO_OP_DELETE_ONE: {
			mongoc_collection_t* coll = mongoc_client_get_collection(w->client, op->db, op->collection);
			bson_t reply = BSON_INITIALIZER;
			ok = mongoc_collection_delete_one(coll, op->filter, op->opts, &reply, &err);
			if (ok) {
				bson_t* copy = bson_copy(&reply);
				if (copy) {
					op->resultDocs[op->resultCount++] = copy;
				}
				else {
					static const char oom[] = "MongoDB: out of memory copying delete reply";
					op->error = (char*)oom;
					op->errorIsStatic = true;
					ok = false;
				}
			}
			bson_destroy(&reply);
			mongoc_collection_destroy(coll);
			if (!ok) {
				if (!op->error) WriteError(w, &err);
				w->state.store(MONGO_STATE_ERROR, std::memory_order_release);
				w->workDone.Set();
				goto next_iteration;
			}
			break;
		}
		case MONGO_OP_DELETE_MANY: {
			mongoc_collection_t* coll = mongoc_client_get_collection(w->client, op->db, op->collection);
			bson_t reply = BSON_INITIALIZER;
			ok = mongoc_collection_delete_many(coll, op->filter, op->opts, &reply, &err);
			if (ok) {
				bson_t* copy = bson_copy(&reply);
				if (copy) {
					op->resultDocs[op->resultCount++] = copy;
				}
				else {
					static const char oom[] = "MongoDB: out of memory copying delete reply";
					op->error = (char*)oom;
					op->errorIsStatic = true;
					ok = false;
				}
			}
			bson_destroy(&reply);
			mongoc_collection_destroy(coll);
			if (!ok) {
				if (!op->error) WriteError(w, &err);
				w->state.store(MONGO_STATE_ERROR, std::memory_order_release);
				w->workDone.Set();
				goto next_iteration;
			}
			break;
		}
		case MONGO_OP_AGGREGATE: {
			mongoc_collection_t* coll = mongoc_client_get_collection(w->client, op->db, op->collection);
			mongoc_cursor_t* cursor = mongoc_collection_aggregate(
				coll, MONGOC_QUERY_NONE, op->filter, op->opts, NULL);
			const bson_t* doc;
			while (!w->cancelled.load(std::memory_order_relaxed) && mongoc_cursor_next(cursor, &doc)) {
				if (!AppendResultDoc(w, doc)) {
					mongoc_cursor_destroy(cursor);
					mongoc_collection_destroy(coll);
					w->state.store(MONGO_STATE_ERROR, std::memory_order_release);
					w->workDone.Set();
					goto next_iteration;
				}
			}
			if (!w->cancelled.load(std::memory_order_relaxed) && mongoc_cursor_error(cursor, &err)) {
				WriteError(w, &err);
				mongoc_cursor_destroy(cursor);
				mongoc_collection_destroy(coll);
				w->state.store(MONGO_STATE_ERROR, std::memory_order_release);
				w->workDone.Set();
				goto next_iteration;
			}
			mongoc_cursor_destroy(cursor);
			mongoc_collection_destroy(coll);
			if (w->cancelled.load(std::memory_order_relaxed)) {
				w->state.store(MONGO_STATE_DONE, std::memory_order_release);
				w->workDone.Set();
				goto next_iteration;
			}
			break;
		}
		case MONGO_OP_COMMAND: {
			mongoc_database_t* db = mongoc_client_get_database(w->client, op->db);
			bson_t reply = BSON_INITIALIZER;
			ok = mongoc_database_command_simple(db, op->filter, NULL, &reply, &err);
			if (ok) {
				bson_t* copy = bson_copy(&reply);
				if (copy) {
					op->resultDocs[op->resultCount++] = copy;
				}
				else {
					static const char oom[] = "MongoDB: out of memory copying command reply";
					op->error = (char*)oom;
					op->errorIsStatic = true;
					ok = false;
				}
			}
			bson_destroy(&reply);
			mongoc_database_destroy(db);
			if (!ok) {
				if (!op->error) WriteError(w, &err);
				w->state.store(MONGO_STATE_ERROR, std::memory_order_release);
				w->workDone.Set();
				goto next_iteration;
			}
			break;
		}
		case MONGO_OP_COUNT: {
			mongoc_collection_t* coll = mongoc_client_get_collection(w->client, op->db, op->collection);
			int64_t count = mongoc_collection_count_documents(coll, op->filter, op->opts, NULL, NULL, &err);
			mongoc_collection_destroy(coll);
			if (count < 0) {
				WriteError(w, &err);
				w->state.store(MONGO_STATE_ERROR, std::memory_order_release);
				w->workDone.Set();
				goto next_iteration;
			}
			op->countResult = count;
			break;
		}
		}

		w->state.store(MONGO_STATE_DONE, std::memory_order_release);
		w->workDone.Set();
		continue;

	next_iteration:;
	}
}

// =============================================================================
// lua_tomongo / lua_pushmongo
// =============================================================================

LuaMongo* lua_tomongo(lua_State* L, int index) {
	return (LuaMongo*)luaL_checkudata(L, index, LUAMONGO);
}

LuaMongo* lua_pushmongo(lua_State* L) {
	LuaMongo* m = (LuaMongo*)lua_newuserdata(L, sizeof(LuaMongo));
	memset(m, 0, sizeof(LuaMongo));
	luaL_getmetatable(L, LUAMONGO);
	lua_setmetatable(L, -2);
	return m;
}

// =============================================================================
// luamongo_gc / luamongo_tostring
// =============================================================================

int luamongo_gc(lua_State* L) {
	LuaMongo* m = (LuaMongo*)lua_touserdata(L, 1);
	if (!m || !m->worker)
		return 0;

	LuaMongoWorker* w = (LuaMongoWorker*)m->worker;

	if (w->state.load(std::memory_order_acquire) == MONGO_STATE_RUNNING) {
		w->cancelled.store(true, std::memory_order_release);
		// stop=true makes the worker exit its loop immediately after the
		// current mongoc call returns, so join() below does not spin forever.
		// WaitFor drains the workDone signal from the in-flight op; then we
		// signal workReady so the worker wakes from its next Wait() and sees
		// stop=true rather than blocking forever inside workReady.Wait().
		w->stop.store(true, std::memory_order_release);
		w->workDone.WaitFor(15000);
		w->workReady.Set();
	}
	else {
		w->stop.store(true, std::memory_order_release);
		w->workReady.Set();
	}

	// Always join — guarantees the worker thread is fully done before we
	// destroy the client it holds.
	w->thread.join();

	if (w->op) {
		FreeOp(w->op);
		w->op = NULL;
	}

	mongoc_client_destroy(w->client);
	delete w;
	m->worker = NULL;
	m->client = NULL;
	return 0;
}

// MongoClose is the Lua-callable Close() method.  Unlike luamongo_gc (which
// is called by the GC and must not error), this validates that arg 1 is
// actually a LuaMongo userdata so MongoDB.Close(wrong_value) raises a clear
// type error instead of silently returning.
int MongoClose(lua_State* L) {
	luaL_checkudata(L, 1, LUAMONGO);
	return luamongo_gc(L);
}

int luamongo_tostring(lua_State* L) {
	LuaMongo* m = (LuaMongo*)lua_touserdata(L, 1);
	if (!m || !m->worker) {
		lua_pushliteral(L, "MongoDB: (closed)");
	}
	else {
		LuaMongoWorker* w = (LuaMongoWorker*)m->worker;
		lua_pushfstring(L, "MongoDB: %p", w->client);
	}
	return 1;
}

// =============================================================================
// Name validation
// =============================================================================

// MongoDB rules: db name must be non-empty, < 64 chars, no special characters.
// Collection name must be non-empty, < 120 chars, no '$' or leading '\0'.
// We enforce the strict superset: non-null, non-empty, no embedded NUL.
static bool ValidateDbName(const char* name, size_t len) {
	if (!name || len == 0 || len >= 64)
		return false;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)name[i];
		// Disallow: NUL, '/', '\', '.', ' ', '"', '$', '*', '<', '>', ':', '|', '?'
		if (c == 0 || c == '/' || c == '\\' || c == '.' || c == ' ' ||
			c == '"' || c == '$' || c == '*' || c == '<' || c == '>' ||
			c == ':' || c == '|' || c == '?')
			return false;
	}
	return true;
}

static bool ValidateCollName(const char* name, size_t len) {
	if (!name || len == 0 || len >= 120)
		return false;
	if (name[0] == '$')
		return false;
	for (size_t i = 0; i < len; i++) {
		if ((unsigned char)name[i] == 0)
			return false;
	}
	return true;
}

// =============================================================================
// Entry point guard macro and helpers
// =============================================================================

#define MONGO_GUARD(L, m, w) \
    LuaMongo* m = lua_tomongo(L, 1); \
    if (!m->worker || !m->client) { \
        lua_pushboolean(L, 0); \
        lua_pushliteral(L, "connection is closed"); \
        return 2; \
    } \
    LuaMongoWorker* w = (LuaMongoWorker*)m->worker; \
    if (w->state.load(std::memory_order_acquire) == MONGO_STATE_RUNNING) { \
        lua_pushboolean(L, 0); \
        lua_pushliteral(L, "operation already in progress"); \
        return 2; \
    } \
    if (w->op) { FreeOp(w->op); w->op = NULL; }

static LuaMongoOp* SetupOp(lua_State* L, LuaMongoWorker* w,
	MongoOpType opType, const char* db, size_t dlen, const char* coll, size_t clen) {
	if (!ValidateDbName(db, dlen))
		luaL_error(L, "MongoDB: invalid database name");

	if (coll && !ValidateCollName(coll, clen))
		luaL_error(L, "MongoDB: invalid collection name");

	LuaMongoOp* op = new (std::nothrow) LuaMongoOp{};
	if (!op)
		luaL_error(L, "MongoDB: out of memory");
	op->opType = opType;

	op->db = (char*)kitsune_malloc(dlen + 1);
	if (!op->db) {
		FreeOp(op);
		luaL_error(L, "MongoDB: out of memory");
	}
	memcpy(op->db, db, dlen);
	op->db[dlen] = '\0';

	if (coll) {
		op->collection = (char*)kitsune_malloc(clen + 1);
		if (!op->collection) {
			FreeOp(op);
			luaL_error(L, "MongoDB: out of memory");
		}
		memcpy(op->collection, coll, clen);
		op->collection[clen] = '\0';
	}

	op->resultCapacity = 64;
	op->resultDocs = (bson_t**)kitsune_malloc(sizeof(bson_t*) * 64);
	if (!op->resultDocs) {
		FreeOp(op);
		luaL_error(L, "MongoDB: out of memory");
	}

	w->op = op;
	return op;
}

static void DispatchOp(LuaMongoWorker* w) {
	// w->op is already written.  PlatformEvent::Set() acquires its internal
	// mutex, which provides a release fence, ensuring w->op is visible to the
	// worker before workReady.Wait() returns on the other thread.
	w->cancelled.store(false, std::memory_order_relaxed);
	w->state.store(MONGO_STATE_RUNNING, std::memory_order_release);
	w->workReady.Set();
}

// =============================================================================
// IsFinished / Wait / Cancel / GetResult
// =============================================================================

// Pin/unpin the LuaMongo userdata in the Lua registry while a coroutine is
// suspended so the GC cannot collect it and leave the continuation's w pointer
// dangling.  Key: lightuserdata(w pointer) -> userdata value.
static void MongoAnchor(lua_State* L, LuaMongoWorker* w) {
	lua_pushlightuserdata(L, (void*)w);
	lua_pushvalue(L, 1);   // the LuaMongo userdata is always arg 1
	lua_rawset(L, LUA_REGISTRYINDEX);
}

static void MongoUnanchor(lua_State* L, LuaMongoWorker* w) {
	lua_pushlightuserdata(L, (void*)w);
	lua_pushnil(L);
	lua_rawset(L, LUA_REGISTRYINDEX);
}

int MongoIsFinished(lua_State* L) {
	LuaMongo* m = lua_tomongo(L, 1);
	LuaMongoWorker* w = m->worker ? (LuaMongoWorker*)m->worker : NULL;
	if (!w) {
		lua_pushboolean(L, 1);
		return 1;
	}
	lua_pushboolean(L, w->state.load(std::memory_order_acquire) != MONGO_STATE_RUNNING);
	return 1;
}

static int MongoWaitCont(lua_State* L, int status, lua_KContext ctx) {
	LuaMongoWorker* w = (LuaMongoWorker*)(intptr_t)ctx;
	if (w->state.load(std::memory_order_acquire) != MONGO_STATE_RUNNING) {
		MongoUnanchor(L, w);
		return 0;
	}
	return lua_yieldk(L, 0, ctx, MongoWaitCont);
}

int MongoWait(lua_State* L) {
	LuaMongo* m = lua_tomongo(L, 1);
	LuaMongoWorker* w = m->worker ? (LuaMongoWorker*)m->worker : NULL;
	if (!w || w->state.load(std::memory_order_acquire) != MONGO_STATE_RUNNING)
		return 0;
	MongoAnchor(L, w);
	return lua_yieldk(L, 0, (lua_KContext)(intptr_t)w, MongoWaitCont);
}

static int MongoCancelCont(lua_State* L, int status, lua_KContext ctx) {
	LuaMongoWorker* w = (LuaMongoWorker*)(intptr_t)ctx;
	if (w->state.load(std::memory_order_acquire) == MONGO_STATE_RUNNING)
		return lua_yieldk(L, 0, ctx, MongoCancelCont);
	MongoUnanchor(L, w);
	if (w->op) {
		FreeOp(w->op);
		w->op = NULL;
	}
	w->state.store(MONGO_STATE_IDLE, std::memory_order_release);
	return 0;
}

int MongoCancel(lua_State* L) {
	LuaMongo* m = lua_tomongo(L, 1);
	LuaMongoWorker* w = m->worker ? (LuaMongoWorker*)m->worker : NULL;
	if (!w || w->state.load(std::memory_order_acquire) != MONGO_STATE_RUNNING)
		return 0;
	w->cancelled.store(true, std::memory_order_release);
	MongoAnchor(L, w);
	return lua_yieldk(L, 0, (lua_KContext)(intptr_t)w, MongoCancelCont);
}

static int PushOpResult(lua_State* L, LuaMongoWorker* w) {
	LuaMongoOp* op = w->op;
	w->op = NULL;
	w->state.store(MONGO_STATE_IDLE, std::memory_order_release);

	if (op->error) {
		lua_pushnil(L);
		lua_pushstring(L, op->error);
		FreeOp(op);
		return 2;
	}

	// Destroy the non-result fields now so they are freed even if BsonToLua
	// throws via luaL_error (depth > 32).  We then destroy each result doc
	// immediately after converting it so at most the unconverted tail leaks.
	if (op->filter) { bson_destroy(op->filter);  op->filter = NULL; }
	if (op->update) { bson_destroy(op->update);  op->update = NULL; }
	if (op->opts) { bson_destroy(op->opts);    op->opts = NULL; }
	for (int i = 0; i < op->insertCount; i++) bson_destroy(op->insertDocs[i]);
	kitsune_free(op->insertDocs); op->insertDocs = NULL; op->insertCount = 0;
	kitsune_free(op->ctx_rec);    op->ctx_rec = NULL;
	kitsune_free(op->db);         op->db = NULL;
	kitsune_free(op->collection); op->collection = NULL;

	switch (op->opType) {
		case MONGO_OP_FIND:
		case MONGO_OP_AGGREGATE: {
			int64_t n = op->resultCount;
			op->resultCount = 0;
			lua_createtable(L, (int)n, 0);
			for (int64_t i = 0; i < n; i++) {
				bson_t* doc = op->resultDocs[i];
				op->resultDocs[i] = NULL;
				BsonToLua(L, doc, 0);
				bson_destroy(doc);
				lua_rawseti(L, -2, (lua_Integer)(i + 1));
			}
			lua_pushnil(L);
			break;
		}
		case MONGO_OP_FIND_ONE: {
			bson_t* doc = (op->resultCount > 0) ? op->resultDocs[0] : NULL;
			op->resultCount = 0;
			if (op->resultDocs) op->resultDocs[0] = NULL;
			if (doc) { BsonToLua(L, doc, 0); bson_destroy(doc); }
			else { lua_pushnil(L); }
			lua_pushnil(L);
			break;
		}
		case MONGO_OP_COUNT:
			lua_pushinteger(L, (lua_Integer)op->countResult);
			lua_pushnil(L);
			break;
		default: { // INSERT, UPDATE, DELETE, COMMAND
			bson_t* doc = (op->resultCount > 0) ? op->resultDocs[0] : NULL;
			op->resultCount = 0;
			if (op->resultDocs) op->resultDocs[0] = NULL;
			if (doc) { BsonToLua(L, doc, 0); bson_destroy(doc); }
			else { lua_pushnil(L); }
			lua_pushnil(L);
			break;
		}
	}

	kitsune_free(op->resultDocs);
	delete op;
	return 2;
}

static int MongoGetResultCont(lua_State* L, int status, lua_KContext ctx) {
	LuaMongoWorker* w = (LuaMongoWorker*)(intptr_t)ctx;
	if (w->state.load(std::memory_order_acquire) != MONGO_STATE_RUNNING) {
		// Unanchor AFTER PushOpResult: if BsonToLua throws inside PushOpResult
		// the Lua error unwind handles the stack, and w->op has already been
		// NULLed and its fields freed up to the throw point.  The remaining
		// op shell (resultDocs array + op itself) will leak on a depth>32 throw
		// but that path is unreachable for documents we stored (they already
		// passed through LuaToBson with the same depth limit).
		int n = PushOpResult(L, w);
		MongoUnanchor(L, w);
		return n;
	}
	return lua_yieldk(L, 0, ctx, MongoGetResultCont);
}

int MongoGetResult(lua_State* L) {
	LuaMongo* m = lua_tomongo(L, 1);
	LuaMongoWorker* w = m->worker ? (LuaMongoWorker*)m->worker : NULL;
	if (!w || !w->op) {
		lua_pushnil(L);
		lua_pushliteral(L, "no active operation");
		return 2;
	}
	if (w->state.load(std::memory_order_acquire) != MONGO_STATE_RUNNING)
		return PushOpResult(L, w);
	MongoAnchor(L, w);
	return lua_yieldk(L, 0, (lua_KContext)(intptr_t)w, MongoGetResultCont);
}

// =============================================================================
// MongoConnect
// =============================================================================

int MongoConnect(lua_State* L) {
	const char* uri_string = luaL_checkstring(L, 1);

	bson_error_t err;
	mongoc_uri_t* uri = mongoc_uri_new_with_error(uri_string, &err);
	if (!uri) {
		lua_pushnil(L);
		lua_pushstring(L, err.message);
		return 2;
	}

	mongoc_client_t* client = mongoc_client_new_from_uri(uri);
	mongoc_uri_destroy(uri);
	if (!client) {
		lua_pushnil(L);
		lua_pushliteral(L, "mongoc_client_new_from_uri failed");
		return 2;
	}

	// Eager ping to verify reachability
	bson_t ping = BSON_INITIALIZER;
	BSON_APPEND_INT32(&ping, "ping", 1);
	bson_t reply = BSON_INITIALIZER;
	bson_error_t pingErr;
	mongoc_database_t* admin = mongoc_client_get_database(client, "admin");
	bool ok = mongoc_database_command_simple(admin, &ping, NULL, &reply, &pingErr);
	mongoc_database_destroy(admin);
	bson_destroy(&ping);
	bson_destroy(&reply);
	if (!ok) {
		mongoc_client_destroy(client);
		lua_pushnil(L);
		lua_pushstring(L, pingErr.message);
		return 2;
	}

	LuaMongoWorker* w = new (std::nothrow) LuaMongoWorker{};
	if (!w) {
		mongoc_client_destroy(client);
		lua_pushnil(L);
		lua_pushliteral(L, "MongoDB: out of memory allocating worker");
		return 2;
	}
	w->client = client;
	w->state.store(MONGO_STATE_IDLE, std::memory_order_relaxed);
	w->stop.store(false, std::memory_order_relaxed);
	w->cancelled.store(false, std::memory_order_relaxed);
	w->op = NULL;

	try {
		w->thread = std::thread(MongoWorkerThread, w);
	}
	catch (const std::system_error& e) {
		mongoc_client_destroy(client);
		delete w;
		lua_pushnil(L);
		lua_pushstring(L, e.what());
		return 2;
	}

	LuaMongo* m = lua_pushmongo(L);
	m->client = client;
	m->worker = w;
	return 1;
}

// =============================================================================
// Operation entry points
// =============================================================================

// Helper: build opts bson from Lua table, appending limit/skip integers if provided.
// Assigns directly to op->opts so FreeOp can clean it up if LuaToBson throws.
static void BuildFindOpts(lua_State* L, LuaMongoOp* op, int optsIdx, int limitIdx, int skipIdx, BsonWriteCtx* ctx) {
	op->opts = bson_new();
	if (!op->opts)
		luaL_error(L, "MongoDB: out of memory");

	if (lua_type(L, optsIdx) == LUA_TTABLE)
		LuaToBson(L, optsIdx, op->opts, ctx);

	if (lua_type(L, limitIdx) == LUA_TNUMBER) {
		if (!lua_isinteger(L, limitIdx))
			luaL_error(L, "MongoDB: limit must be an integer");
		BSON_APPEND_INT64(op->opts, "limit", (int64_t)lua_tointeger(L, limitIdx));
	}

	if (lua_type(L, skipIdx) == LUA_TNUMBER) {
		if (!lua_isinteger(L, skipIdx))
			luaL_error(L, "MongoDB: skip must be an integer");
		BSON_APPEND_INT64(op->opts, "skip", (int64_t)lua_tointeger(L, skipIdx));
	}
}

int MongoFind(lua_State* L) {
	MONGO_GUARD(L, m, w);
	size_t dblen, colllen;
	const char* db = luaL_checklstring(L, 2, &dblen);
	const char* coll = luaL_checklstring(L, 3, &colllen);
	luaL_checktype(L, 4, LUA_TTABLE);

	BsonWriteCtx ctx = {};
	LuaMongoOp* op = SetupOp(L, w, MONGO_OP_FIND, db, dblen, coll, colllen);

	op->filter = bson_new();
	if (!op->filter) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
	LuaToBson(L, 4, op->filter, &ctx);

	// limit=arg5, skip=arg6, opts table=arg7
	// BuildFindOpts assigns directly to op->opts so FreeOp owns it on any throw.
	BuildFindOpts(L, op, 7, 5, 6, &ctx);

	op->ctx_rec = ctx.rec;
	DispatchOp(w);
	lua_pushboolean(L, 1);
	lua_pushnil(L);
	return 2;
}

int MongoFindOne(lua_State* L) {
	MONGO_GUARD(L, m, w);
	size_t dblen, colllen;
	const char* db = luaL_checklstring(L, 2, &dblen);
	const char* coll = luaL_checklstring(L, 3, &colllen);
	luaL_checktype(L, 4, LUA_TTABLE);

	BsonWriteCtx ctx = {};
	LuaMongoOp* op = SetupOp(L, w, MONGO_OP_FIND_ONE, db, dblen, coll, colllen);

	op->filter = bson_new();
	if (!op->filter) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
	LuaToBson(L, 4, op->filter, &ctx);

	// Optional opts table at arg5
	if (lua_type(L, 5) == LUA_TTABLE) {
		op->opts = bson_new();
		if (!op->opts) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
		LuaToBson(L, 5, op->opts, &ctx);
	}

	op->ctx_rec = ctx.rec;
	DispatchOp(w);
	lua_pushboolean(L, 1);
	lua_pushnil(L);
	return 2;
}

int MongoInsertOne(lua_State* L) {
	MONGO_GUARD(L, m, w);
	size_t dblen, colllen;
	const char* db = luaL_checklstring(L, 2, &dblen);
	const char* coll = luaL_checklstring(L, 3, &colllen);
	luaL_checktype(L, 4, LUA_TTABLE);

	BsonWriteCtx ctx = {};
	LuaMongoOp* op = SetupOp(L, w, MONGO_OP_INSERT_ONE, db, dblen, coll, colllen);

	op->filter = bson_new();
	if (!op->filter) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
	LuaToBson(L, 4, op->filter, &ctx);

	op->ctx_rec = ctx.rec;
	DispatchOp(w);
	lua_pushboolean(L, 1);
	lua_pushnil(L);
	return 2;
}

int MongoInsertMany(lua_State* L) {
	MONGO_GUARD(L, m, w);
	size_t dblen, colllen;
	const char* db = luaL_checklstring(L, 2, &dblen);
	const char* coll = luaL_checklstring(L, 3, &colllen);
	luaL_checktype(L, 4, LUA_TTABLE);

	int n = (int)lua_rawlen(L, 4);
	if (n == 0) {
		lua_pushboolean(L, 0);
		lua_pushliteral(L, "InsertMany: document array is empty");
		return 2;
	}

	BsonWriteCtx ctx = {};
	LuaMongoOp* op = SetupOp(L, w, MONGO_OP_INSERT_MANY, db, dblen, coll, colllen);

	op->insertDocs = (bson_t**)kitsune_malloc(sizeof(bson_t*) * n);
	if (!op->insertDocs) {
		op->ctx_rec = ctx.rec;
		FreeOp(op); w->op = NULL;
		lua_pushboolean(L, 0);
		lua_pushliteral(L, "MongoDB: out of memory");
		return 2;
	}

	for (int i = 1; i <= n; i++) {
		lua_rawgeti(L, 4, i);
		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			op->ctx_rec = ctx.rec;
			FreeOp(op); w->op = NULL;
			lua_pushboolean(L, 0);
			lua_pushliteral(L, "InsertMany: array element is not a table");
			return 2;
		}
		bson_t* doc = bson_new();
		if (!doc) {
			lua_pop(L, 1);
			op->ctx_rec = ctx.rec;
			FreeOp(op); w->op = NULL;
			lua_pushboolean(L, 0);
			lua_pushliteral(L, "MongoDB: out of memory");
			return 2;
		}
		// Push doc into the array BEFORE calling LuaToBson so FreeOp cleans it
		// up if LuaToBson throws via luaL_error.
		op->insertDocs[op->insertCount++] = doc;
		LuaToBson(L, -1, doc, &ctx);
		lua_pop(L, 1);
	}

	op->ctx_rec = ctx.rec;
	DispatchOp(w);
	lua_pushboolean(L, 1);
	lua_pushnil(L);
	return 2;
}

int MongoUpdateOne(lua_State* L) {
	MONGO_GUARD(L, m, w);
	size_t dblen, colllen;
	const char* db = luaL_checklstring(L, 2, &dblen);
	const char* coll = luaL_checklstring(L, 3, &colllen);
	luaL_checktype(L, 4, LUA_TTABLE);
	luaL_checktype(L, 5, LUA_TTABLE);

	BsonWriteCtx ctx = {};
	LuaMongoOp* op = SetupOp(L, w, MONGO_OP_UPDATE_ONE, db, dblen, coll, colllen);

	op->filter = bson_new();
	if (!op->filter) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
	LuaToBson(L, 4, op->filter, &ctx);
	op->update = bson_new();
	if (!op->update) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
	LuaToBson(L, 5, op->update, &ctx);

	if (lua_type(L, 6) == LUA_TTABLE) {
		op->opts = bson_new();
		if (!op->opts) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
		LuaToBson(L, 6, op->opts, &ctx);
	}

	op->ctx_rec = ctx.rec;
	DispatchOp(w);
	lua_pushboolean(L, 1);
	lua_pushnil(L);
	return 2;
}

int MongoUpdateMany(lua_State* L) {
	MONGO_GUARD(L, m, w);
	size_t dblen, colllen;
	const char* db = luaL_checklstring(L, 2, &dblen);
	const char* coll = luaL_checklstring(L, 3, &colllen);
	luaL_checktype(L, 4, LUA_TTABLE);
	luaL_checktype(L, 5, LUA_TTABLE);

	BsonWriteCtx ctx = {};
	LuaMongoOp* op = SetupOp(L, w, MONGO_OP_UPDATE_MANY, db, dblen, coll, colllen);

	op->filter = bson_new();
	if (!op->filter) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
	LuaToBson(L, 4, op->filter, &ctx);
	op->update = bson_new();
	if (!op->update) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
	LuaToBson(L, 5, op->update, &ctx);

	if (lua_type(L, 6) == LUA_TTABLE) {
		op->opts = bson_new();
		if (!op->opts) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
		LuaToBson(L, 6, op->opts, &ctx);
	}

	op->ctx_rec = ctx.rec;
	DispatchOp(w);
	lua_pushboolean(L, 1);
	lua_pushnil(L);
	return 2;
}

int MongoDeleteOne(lua_State* L) {
	MONGO_GUARD(L, m, w);
	size_t dblen, colllen;
	const char* db = luaL_checklstring(L, 2, &dblen);
	const char* coll = luaL_checklstring(L, 3, &colllen);
	luaL_checktype(L, 4, LUA_TTABLE);

	BsonWriteCtx ctx = {};
	LuaMongoOp* op = SetupOp(L, w, MONGO_OP_DELETE_ONE, db, dblen, coll, colllen);

	op->filter = bson_new();
	if (!op->filter) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
	LuaToBson(L, 4, op->filter, &ctx);

	op->ctx_rec = ctx.rec;
	DispatchOp(w);
	lua_pushboolean(L, 1);
	lua_pushnil(L);
	return 2;
}

int MongoDeleteMany(lua_State* L) {
	MONGO_GUARD(L, m, w);
	size_t dblen, colllen;
	const char* db = luaL_checklstring(L, 2, &dblen);
	const char* coll = luaL_checklstring(L, 3, &colllen);
	luaL_checktype(L, 4, LUA_TTABLE);

	BsonWriteCtx ctx = {};
	LuaMongoOp* op = SetupOp(L, w, MONGO_OP_DELETE_MANY, db, dblen, coll, colllen);

	op->filter = bson_new();
	if (!op->filter) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
	LuaToBson(L, 4, op->filter, &ctx);

	op->ctx_rec = ctx.rec;
	DispatchOp(w);
	lua_pushboolean(L, 1);
	lua_pushnil(L);
	return 2;
}

int MongoAggregate(lua_State* L) {
	MONGO_GUARD(L, m, w);
	size_t dblen, colllen;
	const char* db = luaL_checklstring(L, 2, &dblen);
	const char* coll = luaL_checklstring(L, 3, &colllen);
	luaL_checktype(L, 4, LUA_TTABLE);

	BsonWriteCtx ctx = {};
	LuaMongoOp* op = SetupOp(L, w, MONGO_OP_AGGREGATE, db, dblen, coll, colllen);

	// Pipeline must be a BSON array
	op->filter = bson_new();
	if (!op->filter) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
	LuaToBsonArray(L, 4, op->filter, &ctx);

	if (lua_type(L, 5) == LUA_TTABLE) {
		op->opts = bson_new();
		if (!op->opts) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
		LuaToBson(L, 5, op->opts, &ctx);
	}

	op->ctx_rec = ctx.rec;
	DispatchOp(w);
	lua_pushboolean(L, 1);
	lua_pushnil(L);
	return 2;
}

int MongoCommand(lua_State* L) {
	MONGO_GUARD(L, m, w);
	size_t dblen;
	const char* db = luaL_checklstring(L, 2, &dblen);
	luaL_checktype(L, 3, LUA_TTABLE);

	BsonWriteCtx ctx = {};
	// collection is NULL for Command — it runs against the database
	LuaMongoOp* op = SetupOp(L, w, MONGO_OP_COMMAND, db, dblen, NULL, 0);

	op->filter = bson_new();
	if (!op->filter) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
	LuaToBson(L, 3, op->filter, &ctx);

	op->ctx_rec = ctx.rec;
	DispatchOp(w);
	lua_pushboolean(L, 1);
	lua_pushnil(L);
	return 2;
}

int MongoCountDocuments(lua_State* L) {
	MONGO_GUARD(L, m, w);
	size_t dblen, colllen;
	const char* db = luaL_checklstring(L, 2, &dblen);
	const char* coll = luaL_checklstring(L, 3, &colllen);
	luaL_checktype(L, 4, LUA_TTABLE);

	BsonWriteCtx ctx = {};
	LuaMongoOp* op = SetupOp(L, w, MONGO_OP_COUNT, db, dblen, coll, colllen);

	op->filter = bson_new();
	if (!op->filter) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
	LuaToBson(L, 4, op->filter, &ctx);

	if (lua_type(L, 5) == LUA_TTABLE) {
		op->opts = bson_new();
		if (!op->opts) { FreeOp(op); w->op = NULL; return luaL_error(L, "MongoDB: out of memory"); }
		LuaToBson(L, 5, op->opts, &ctx);
	}

	op->ctx_rec = ctx.rec;
	DispatchOp(w);
	lua_pushboolean(L, 1);
	lua_pushnil(L);
	return 2;
}

#else // !KITSUNE_MONGO — stub out everything so the header still compiles

LuaMongo* lua_tomongo(lua_State* L, int index) { (void)L; (void)index; return NULL; }
LuaMongo* lua_pushmongo(lua_State* L) { (void)L; return NULL; }
int MongoConnect(lua_State* L) { lua_pushnil(L); lua_pushliteral(L, "MongoDB support not compiled"); return 2; }
int MongoIsFinished(lua_State* L) { lua_pushboolean(L, 1); return 1; }
int MongoWait(lua_State* L) { (void)L; return 0; }
int MongoCancel(lua_State* L) { (void)L; return 0; }
int MongoGetResult(lua_State* L) { lua_pushnil(L); lua_pushliteral(L, "MongoDB support not compiled"); return 2; }
int MongoFind(lua_State* L) { lua_pushboolean(L, 0); lua_pushliteral(L, "MongoDB support not compiled"); return 2; }
int MongoFindOne(lua_State* L) { lua_pushboolean(L, 0); lua_pushliteral(L, "MongoDB support not compiled"); return 2; }
int MongoInsertOne(lua_State* L) { lua_pushboolean(L, 0); lua_pushliteral(L, "MongoDB support not compiled"); return 2; }
int MongoInsertMany(lua_State* L) { lua_pushboolean(L, 0); lua_pushliteral(L, "MongoDB support not compiled"); return 2; }
int MongoUpdateOne(lua_State* L) { lua_pushboolean(L, 0); lua_pushliteral(L, "MongoDB support not compiled"); return 2; }
int MongoUpdateMany(lua_State* L) { lua_pushboolean(L, 0); lua_pushliteral(L, "MongoDB support not compiled"); return 2; }
int MongoDeleteOne(lua_State* L) { lua_pushboolean(L, 0); lua_pushliteral(L, "MongoDB support not compiled"); return 2; }
int MongoDeleteMany(lua_State* L) { lua_pushboolean(L, 0); lua_pushliteral(L, "MongoDB support not compiled"); return 2; }
int MongoAggregate(lua_State* L) { lua_pushboolean(L, 0); lua_pushliteral(L, "MongoDB support not compiled"); return 2; }
int MongoCommand(lua_State* L) { lua_pushboolean(L, 0); lua_pushliteral(L, "MongoDB support not compiled"); return 2; }
int MongoCountDocuments(lua_State* L) { lua_pushboolean(L, 0); lua_pushliteral(L, "MongoDB support not compiled"); return 2; }
int MongoClose(lua_State* L) { luaL_checkudata(L, 1, LUAMONGO); return 0; }
int luamongo_gc(lua_State* L) { (void)L; return 0; }
int luamongo_tostring(lua_State* L) { lua_pushliteral(L, "MongoDB: (not compiled)"); return 1; }

#endif // KITSUNE_MONGO
