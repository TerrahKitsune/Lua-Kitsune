#include "luaxml.h"
#include "pugixml.hpp"

// =============================================================================
// Custom pugixml allocator
// =============================================================================

static void* xml_alloc(size_t size) {
	return kitsune_malloc(size);
}

static void xml_dealloc(void* ptr) {
	kitsune_free(ptr);
}

void ensure_allocator() {
	static bool installed = false;
	if (!installed) {
		pugi::set_memory_management_functions(xml_alloc, xml_dealloc);
		installed = true;
	}
}

// =============================================================================
// Encode output buffer writer.
// write() must NOT longjmp -- it is called from inside pugixml's virtual
// dispatch.  Set oom and raise the error after print() returns.
// =============================================================================

struct LuaXmlWriter : pugi::xml_writer {
	LuaXml* x;
	int      oom;
	LuaXmlWriter() : x(NULL), oom(0) {}
	void write(const void* data, size_t size) override {
		if (oom || size == 0)
			return;
		size_t needed = x->outLen + size;
		if (needed > x->outCap) {
			size_t cap = x->outCap ? x->outCap * 2 : 4096;
			if (cap < needed)
				cap = needed;
			char* p = (char*)kitsune_realloc(x->outBuf, cap);
			if (!p) {
				oom = 1;
				return;
			}
			x->outBuf = p;
			x->outCap = cap;
		}
		memcpy(x->outBuf + x->outLen, data, size);
		x->outLen += size;
	}
};

// =============================================================================
// Instance management
// =============================================================================

LuaXml* lua_xml_push(lua_State* L) {
	LuaXml* x = (LuaXml*)lua_newuserdata(L, sizeof(LuaXml));
	memset(x, 0, sizeof(LuaXml));
	x->doc = new pugi::xml_document();
	luaL_setmetatable(L, LUAXML);
	return x;
}

LuaXml* lua_xml_check(lua_State* L, int idx) {
	return (LuaXml*)luaL_checkudata(L, idx, LUAXML);
}

int lua_xml_gc(lua_State* L) {
	LuaXml* x = lua_xml_check(L, 1);
	delete x->doc;
	x->doc = NULL;
	kitsune_free(x->rec);
	x->rec = NULL;
	kitsune_free(x->outBuf);
	x->outBuf = NULL;
	return 0;
}

int lua_xml_tostring(lua_State* L) {
	lua_pushfstring(L, "Xml: %p", lua_xml_check(L, 1));
	return 1;
}

int lua_xml_new(lua_State* L) {
	int indent = 0;
	if (lua_isboolean(L, 1))
		indent = lua_toboolean(L, 1);
	else if (lua_isboolean(L, 2))
		indent = lua_toboolean(L, 2);
	lua_xml_push(L)->indent = indent;
	return 1;
}

// =============================================================================
// Decode -- pugixml DOM -> Lua tables  (fully iterative, no C recursion)
//
// pugixml already parsed the XML into a tree iteratively.  We walk that tree
// with an explicit heap-allocated frame stack so arbitrarily deep documents
// cannot overflow the C call stack.
//
// Lua stack layout during the walk:
//   ... | elem0 | childTbl0 | elem1 | childTbl1 | ...
//
// Each frame records the abs indices of its elem and childTbl so that when a
// node is finished its elem can be rawseti'd into the parent's childTbl.
// =============================================================================

#define XML_MAX_DEPTH 512

struct DecodeFrame {
	pugi::xml_node node;
	int            elemIdx;     // abs Lua stack index of this node's elem table
	int            childTblIdx; // abs Lua stack index of this node's children table
	int            childSeq;    // next 1-based rawseti index into parent childTbl
};

static pugi::xml_node xml_first_elem_child(const pugi::xml_node& n) {
	pugi::xml_node c = n.first_child();
	while (c && c.type() != pugi::node_element)
		c = c.next_sibling();
	return c;
}

static pugi::xml_node xml_next_elem_sibling(const pugi::xml_node& n) {
	pugi::xml_node s = n.next_sibling();
	while (s && s.type() != pugi::node_element)
		s = s.next_sibling();
	return s;
}

// Push tag/attr/text/children tables for one element.
// After call stack top is the children table; elem table is one below.
// Returns the abs Lua stack index of the children table.
static int decode_push_elem(lua_State* L, const pugi::xml_node& node) {
	lua_newtable(L);
	int ei = lua_gettop(L);

	lua_pushstring(L, node.name());
	lua_setfield(L, ei, "tag");

	lua_newtable(L);
	int aseq = 1;
	for (const pugi::xml_attribute& a : node.attributes()) {
		lua_newtable(L);
		lua_pushstring(L, a.name());
		lua_setfield(L, -2, "key");
		lua_pushstring(L, a.value());
		lua_setfield(L, -2, "value");
		lua_rawseti(L, -2, aseq++);
	}
	lua_setfield(L, ei, "attr");

	int tp = 0;
	for (const pugi::xml_node& ch : node.children()) {
		if (ch.type() == pugi::node_pcdata || ch.type() == pugi::node_cdata) {
			lua_pushstring(L, ch.value());
			tp++;
		}
	}
	if (tp == 0)
		lua_pushliteral(L, "");
	else if (tp > 1)
		lua_concat(L, tp);
	lua_setfield(L, ei, "text");

	lua_newtable(L);
	int ci = lua_gettop(L);
	lua_pushvalue(L, ci);
	lua_setfield(L, ei, "children");
	// Stack: ... | elem(ei) | childTbl(ci)
	return ci;
}

int lua_xml_decode(lua_State* L) {
	LuaXml* x = lua_xml_check(L, 1);
	if (lua_type(L, 2) != LUA_TSTRING)
		luaL_argerror(L, 2, "string expected");
	size_t      len = 0;
	const char* src = lua_tolstring(L, 2, &len);

	x->doc->reset();

	pugi::xml_parse_result result = x->doc->load_buffer(
		src, len, pugi::parse_default | pugi::parse_ws_pcdata_single);

	if (!result) {
		x->doc->reset();
		lua_pushnil(L);
		lua_pushstring(L, result.description());
		return 2;
	}

	pugi::xml_node root = x->doc->first_child();
	while (root && root.type() != pugi::node_element)
		root = root.next_sibling();

	if (!root) {
		x->doc->reset();
		lua_pushnil(L);
		lua_pushliteral(L, "Xml: no root element");
		return 2;
	}

	DecodeFrame* stack = (DecodeFrame*)kitsune_malloc(XML_MAX_DEPTH * sizeof(DecodeFrame));
	if (!stack) {
		x->doc->reset();
		lua_pushnil(L);
		lua_pushliteral(L, "Xml.Decode: out of memory");
		return 2;
	}

	// Push root onto Lua stack and seed frame 0.
	// Each level needs ~8 slots (elem, attr table, children table, temporaries).
	// Pre-check that enough stack is available for the maximum supported depth.
	if (!lua_checkstack(L, XML_MAX_DEPTH * 2 + 32)) {
		kitsune_free(stack);
		x->doc->reset();
		lua_pushnil(L);
		lua_pushliteral(L, "Xml.Decode: out of memory");
		return 2;
	}
	int ci = decode_push_elem(L, root);
	stack[0].node       = root;
	stack[0].elemIdx    = ci - 1;
	stack[0].childTblIdx = ci;
	stack[0].childSeq   = 1;
	int depth = 1;

	// cur: the next element child to descend into for the current top frame.
	pugi::xml_node cur = xml_first_elem_child(root);

	while (depth > 0) {
		if (!cur) {
			// No more children at this level.
			if (depth == 1) {
				// Root finished -- stop.
				break;
			}
			// Insert this node's elem into parent's childTbl, then pop.
			DecodeFrame& done   = stack[depth - 1];
			DecodeFrame& parent = stack[depth - 2];
			lua_pushvalue(L, done.elemIdx);
			lua_rawseti(L, parent.childTblIdx, parent.childSeq++);
			lua_pop(L, 2); // pop done.elemIdx and done.childTblIdx
			cur = xml_next_elem_sibling(done.node);
			depth--;
			continue;
		}

		if (depth >= XML_MAX_DEPTH) {
			// Unwind Lua stack: each live frame has 2 tables on it.
			lua_pop(L, depth * 2);
			kitsune_free(stack);
			x->doc->reset();
			luaL_error(L, "Xml.Decode: document nesting exceeds maximum depth");
		}

		// Descend into cur.
		int nci = decode_push_elem(L, cur);
		stack[depth].node       = cur;
		stack[depth].elemIdx    = nci - 1;
		stack[depth].childTblIdx = nci;
		stack[depth].childSeq   = 1;
		depth++;
		cur = xml_first_elem_child(cur);
	}

	kitsune_free(stack);
	x->doc->reset();
	// Stack: ... | rootElem | rootChildTbl
	// Pop childTbl, leave elem as the single return value.
	lua_pop(L, 1);
	return 1;
}

// =============================================================================
// Encode -- Lua tables -> pugixml DOM -> string  (fully iterative, no C recursion)
//
// Each frame holds a luaL_ref to the Lua table (keeps it GC-rooted), the
// pugixml node to append into, and a cursor over the children array.
// First visit (idx == -1) writes tag/attr/text; subsequent visits step
// through children.
// =============================================================================

struct EncodeFrame {
	int            luaRef; // LUA_REGISTRYINDEX ref for this node's Lua table
	pugi::xml_node parent; // pugixml node children are appended into
	lua_Integer    idx;    // cursor: -1 = not yet processed, 1..len = children
	lua_Integer    len;    // length of children array
};

static void enc_reset(LuaXml* x) {
	x->outLen = 0;
	x->recLen = 0;
	x->doc->reset();
}

int lua_xml_encode(lua_State* L) {
	LuaXml* x = lua_xml_check(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);

	enc_reset(x);

	EncodeFrame* stack = (EncodeFrame*)kitsune_malloc(XML_MAX_DEPTH * sizeof(EncodeFrame));
	if (!stack)
		luaL_error(L, "Xml.Encode: out of memory");

	pugi::xml_node decl = x->doc->append_child(pugi::node_declaration);
	decl.append_attribute("version")  = "1.0";
	decl.append_attribute("encoding") = "UTF-8";

	lua_pushvalue(L, 2);
	stack[0].luaRef = luaL_ref(L, LUA_REGISTRYINDEX);
	stack[0].parent = *x->doc;
	stack[0].idx    = -1;
	stack[0].len    = 0;
	int depth = 1;

	const char* errMsg = NULL;

	while (depth > 0 && !errMsg) {
		EncodeFrame& f = stack[depth - 1];

		if (f.idx == -1) {
			// First visit: write tag / attr / text into pugixml.
			lua_rawgeti(L, LUA_REGISTRYINDEX, f.luaRef);
			int tbl = lua_gettop(L);

			lua_getfield(L, tbl, "tag");
			const char* tag = lua_tostring(L, -1);
			if (!tag || !*tag) {
				lua_pop(L, 2);
				errMsg = "Xml.Encode: every node must have a non-empty 'tag' field";
				break;
			}
			pugi::xml_node elem = f.parent.append_child(tag);
			lua_pop(L, 1);
			// Replace parent with the new element so children go inside it.
			f.parent = elem;

			lua_getfield(L, tbl, "attr");
			if (lua_istable(L, -1)) {
				int atbl = lua_gettop(L);
				lua_Integer n = (lua_Integer)lua_rawlen(L, atbl);
				for (lua_Integer i = 1; i <= n; i++) {
					lua_rawgeti(L, atbl, i);
					if (lua_istable(L, -1)) {
						lua_getfield(L, -1, "key");
						lua_getfield(L, -2, "value");
						const char* k = lua_tostring(L, -2);
						const char* v = lua_tostring(L, -1);
						if (k)
							elem.append_attribute(k) = v ? v : "";
						lua_pop(L, 2);
					}
					lua_pop(L, 1);
				}
			}
			lua_pop(L, 1);

			lua_getfield(L, tbl, "text");
			if (!lua_isnil(L, -1)) {
				size_t      tlen = 0;
				const char* tv   = lua_tolstring(L, -1, &tlen);
				if (tv && tlen > 0)
					elem.append_child(pugi::node_pcdata).set_value(tv);
			}
			lua_pop(L, 1);

			lua_getfield(L, tbl, "children");
			f.len = lua_istable(L, -1) ? (lua_Integer)lua_rawlen(L, -1) : 0;
			lua_pop(L, 2); // children + tbl

			f.idx = 1;
		}

		if (f.idx > f.len) {
			// Done with this node.
			luaL_unref(L, LUA_REGISTRYINDEX, f.luaRef);
			depth--;
			continue;
		}

		if (depth >= XML_MAX_DEPTH) {
			errMsg = "Xml.Encode: node nesting exceeds maximum depth";
			break;
		}

		// Fetch child table at f.idx and push a new frame for it.
		lua_rawgeti(L, LUA_REGISTRYINDEX, f.luaRef);
		lua_getfield(L, -1, "children");
		lua_rawgeti(L, -1, f.idx++);
		// Stack: ... | tbl | children | child
		lua_insert(L, -3); // move child below tbl and children
		lua_pop(L, 2);     // pop tbl and children, child is now on top

		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			continue;
		}

		stack[depth].luaRef = luaL_ref(L, LUA_REGISTRYINDEX);
		stack[depth].parent = f.parent;
		stack[depth].idx    = -1;
		stack[depth].len    = 0;
		depth++;
	}

	if (errMsg) {
		for (int i = 0; i < depth; i++)
			luaL_unref(L, LUA_REGISTRYINDEX, stack[i].luaRef);
		kitsune_free(stack);
		x->doc->reset();
		luaL_error(L, "%s", errMsg);
	}

	kitsune_free(stack);

	LuaXmlWriter writer;
	writer.x = x;
	unsigned int flags = x->indent ? pugi::format_indent : pugi::format_raw;
	x->doc->print(writer, x->indent ? "\t" : "", flags);
	x->doc->reset();

	if (writer.oom)
		luaL_error(L, "Xml.Encode: out of memory");

	lua_pushlstring(L, x->outBuf, x->outLen);
	return 1;
}