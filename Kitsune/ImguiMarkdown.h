#pragma once
#ifdef KITSUNE_IMGUI

#include "ImguiRenderer.h"
#include "ResourceCache.h"
#include "KitsuneEngine.h"

// ---------------------------------------------------------------------------
// Node type constants
// ---------------------------------------------------------------------------

// Line-level types
#define MD_PARA         0
#define MD_H1           1
#define MD_H2           2
#define MD_H3           3
#define MD_HR           4
#define MD_LIST_UL      5   // unordered list item; nesting level in MarkdownNode::level
#define MD_LIST_OL      6   // ordered list item;   nesting level in MarkdownNode::level
#define MD_BLOCKQUOTE   7
#define MD_CODE_BLOCK   8   // fenced ``` block
#define MD_TABLE_ROW    9   // | col | col | — column count in level
#define MD_TABLE_SEP    10  // |---|---| separator — skipped during render
#define MD_TABLE_CELL   11  // cell boundary marker between span groups within a row

// Span-level types (sub-nodes within a line)
#define MD_SPAN_TEXT    16
#define MD_SPAN_BOLD    17
#define MD_SPAN_ITALIC  18
#define MD_SPAN_CODE    19  // inline `code`
#define MD_SPAN_LINK    20  // text in offset/len, URL in urlOffset/urlLen
#define MD_IMAGE        21  // alt text in offset/len, id in urlOffset/urlLen

// ---------------------------------------------------------------------------
// Node struct
// ---------------------------------------------------------------------------

struct MarkdownNode {
	uint32_t offset;    // byte offset into mdContent
	uint32_t len;       // byte length in mdContent
	uint32_t urlOffset; // for MD_SPAN_LINK / MD_IMAGE: offset of URL/id in mdContent; 0 otherwise
	uint16_t urlLen;
	uint8_t  type;      // MD_* constant above
	uint8_t  level;     // nesting level for lists; 0 otherwise
};

// ---------------------------------------------------------------------------
// MarkdownResource — cache-managed parsed document. Resource must be first field.
// Lifetime is owned by the ResourceCache; luaId is the Lua-facing handle.
// ---------------------------------------------------------------------------

struct MarkdownResource {
	Resource       resource;      // type=RESOURCE_MARKDOWN; luaId and source live here
	char*          mdContent;
	size_t         mdContentLen;
	MarkdownNode*  mdNodes;
	int            mdNodeCount;
	int            mdNodeAlloc;
	const char*    mdCacheError;  // points to a static string or nullptr
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Reads the full stream content into md->mdContent via KitsuneCallMethod.
void ReadStreamIntoCache(const KitsuneVariable* streamVar, MarkdownResource* md);

// Parses md->mdContent into the md->mdNodes flat array.
void ParseContentIntoNodes(MarkdownResource* md);

// Walks md->mdNodes and issues ImGui draw calls inside a BeginChild/EndChild.
// ctx is needed for font/image access.
void RenderFromNodes(MarkdownResource* md, ImguiWindowContext* ctx, float w, float h);

// Frees md->mdContent and md->mdNodes. Does NOT free the MarkdownResource itself.
void FreeMarkdownCache(MarkdownResource* md);

// Registers Markdown.Parse (string|stream -> luaId) and
// Markdown.Destroy(luaId) into the Kitsune engine.
void RegisterMarkdownFunctions();

#endif // KITSUNE_IMGUI

