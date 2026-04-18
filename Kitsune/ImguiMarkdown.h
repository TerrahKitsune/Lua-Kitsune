#pragma once
#ifdef KITSUNE_IMGUI

#include "ImguiRenderer.h"
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
// Public API
// ---------------------------------------------------------------------------

// Reads the full stream content into ctx->mdContent via KitsuneCallMethod.
// Calls stream:Seek(0) then stream:Read() using the KitsuneEngine API.
// Sets ctx->mdCacheError on failure. Does not throw.
void ReadStreamIntoCache(const KitsuneVariable* streamVar, ImguiWindowContext* ctx);

// Parses ctx->mdContent into the ctx->mdNodes flat array.
// Grows the array on demand (doubles when count+1 >= alloc).
// Sets ctx->mdCacheError and returns early on OOM — does not throw.
void ParseContentIntoNodes(ImguiWindowContext* ctx);

// Walks ctx->mdNodes and issues ImGui draw calls inside a BeginChild/EndChild.
// w/h are passed to BeginChild; 0,0 fills available space.
// If ctx->mdCacheError is set, renders it in red and returns early.
void RenderFromNodes(ImguiWindowContext* ctx, float w, float h);

// Frees ctx->mdContent and ctx->mdNodes and zeroes all seven cache fields.
// Safe to call on a zeroed context (all fields NULL/0).
void FreeMarkdownCache(ImguiWindowContext* ctx);

#endif // KITSUNE_IMGUI
