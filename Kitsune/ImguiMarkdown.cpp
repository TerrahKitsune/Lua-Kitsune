#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef KITSUNE_IMGUI

#include "ImguiMarkdown.h"
#include "ImguiOpenGL.h"
#include "Imgui/imgui.h"
#include <SDL.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// FreeMarkdownCache
// ---------------------------------------------------------------------------

void FreeMarkdownCache(ImguiWindowContext* ctx) {
    free(ctx->mdContent);
    free(ctx->mdNodes);
    ctx->mdContent    = nullptr;
    ctx->mdContentLen = 0;
    ctx->mdNodes      = nullptr;
    ctx->mdNodeCount  = 0;
    ctx->mdNodeAlloc  = 0;
    ctx->mdCacheError = nullptr;
    ctx->mdCacheId    = 0;
}

// ---------------------------------------------------------------------------
// ReadStreamIntoCache — uses KitsuneCallMethod only, no lua_State
// ---------------------------------------------------------------------------

void ReadStreamIntoCache(const KitsuneVariable* streamVar, ImguiWindowContext* ctx) {
    // stream:Seek(0)
    {
        KitsuneVariable seekArg = {};
        seekArg.type    = KITSUNE_TINTEGER;
        seekArg.integer = 0;
        KitsuneVariable* seekResult = KitsuneCallMethod(streamVar, "Seek", 1, &seekArg);
        if (!seekResult || seekResult->type == KITSUNE_TERROR) {
            ctx->mdCacheError = "MarkdownRender: stream:Seek(0) failed";
            KitsuneVariableFree(seekResult);
            return;
        }
        KitsuneVariableFree(seekResult);
    }

    // stream:Read() — reads all remaining content
    {
        KitsuneVariable* readResult = KitsuneCallMethod(streamVar, "Read", 0, nullptr);
        if (!readResult || readResult->type == KITSUNE_TERROR ||
            readResult->type == KITSUNE_TNIL || readResult->type == KITSUNE_TNONE ||
            readResult->type != KITSUNE_TSTRING || readResult->length == 0) {
            ctx->mdCacheError = "MarkdownRender: stream:Read() returned no data";
            KitsuneVariableFree(readResult);
            return;
        }

        char* buf = (char*)malloc(readResult->length + 1);
        if (!buf) {
            ctx->mdCacheError = "MarkdownRender: out of memory reading stream";
            KitsuneVariableFree(readResult);
            return;
        }
        memcpy(buf, readResult->data, readResult->length);
        buf[readResult->length] = '\0';
        ctx->mdContent    = buf;
        ctx->mdContentLen = readResult->length;
        KitsuneVariableFree(readResult);
    }
}

// ---------------------------------------------------------------------------
// ParseContentIntoNodes — helpers
// ---------------------------------------------------------------------------

static bool push_node(ImguiWindowContext* ctx, MarkdownNode node) {
    if (ctx->mdNodeCount + 1 >= ctx->mdNodeAlloc) {
        int newAlloc = ctx->mdNodeAlloc == 0 ? 64 : ctx->mdNodeAlloc * 2;
        MarkdownNode* grown = (MarkdownNode*)realloc(ctx->mdNodes, (size_t)newAlloc * sizeof(MarkdownNode));
        if (!grown) {
            ctx->mdCacheError = "MarkdownRender: out of memory building node array";
            return false;
        }
        ctx->mdNodes     = grown;
        ctx->mdNodeAlloc = newAlloc;
    }
    ctx->mdNodes[ctx->mdNodeCount++] = node;
    return true;
}

static bool push_span(ImguiWindowContext* ctx, uint8_t type,
    uint32_t start, uint32_t end,
    uint32_t urlOff = 0, uint16_t urlLen = 0) {
    if (end <= start)
        return true;
    MarkdownNode n = {};
    n.type      = type;
    n.offset    = start;
    n.len       = end - start;
    n.urlOffset = urlOff;
    n.urlLen    = urlLen;
    return push_node(ctx, n);
}

// ---------------------------------------------------------------------------
// Span parser — emits MD_SPAN_* nodes for a single line region [s, end)
// ---------------------------------------------------------------------------

static bool parse_spans(ImguiWindowContext* ctx, const char* base,
    uint32_t s, uint32_t end) {
    uint32_t cur = s;

    while (cur < end) {
        const char c = base[cur];

        // Image: ![alt](id)
        if (c == '!' && cur + 1 < end && base[cur + 1] == '[') {
            uint32_t altStart = cur + 2, altEnd = altStart;
            while (altEnd < end && base[altEnd] != ']') altEnd++;
            if (altEnd < end && altEnd + 1 < end && base[altEnd + 1] == '(') {
                uint32_t idStart = altEnd + 2, idEnd = idStart;
                while (idEnd < end && base[idEnd] != ')') idEnd++;
                if (idEnd < end) {
                    if (!push_span(ctx, MD_SPAN_TEXT, s, cur)) return false;
                    MarkdownNode n = {};
                    n.type      = MD_IMAGE;
                    n.offset    = altStart;
                    n.len       = altEnd - altStart;
                    n.urlOffset = idStart;
                    n.urlLen    = (uint16_t)((idEnd - idStart) < 65535 ? (idEnd - idStart) : 65535);
                    if (!push_node(ctx, n)) return false;
                    cur = idEnd + 1; s = cur;
                    continue;
                }
            }
        }

        // Link: [text](url)
        if (c == '[') {
            uint32_t txtStart = cur + 1, txtEnd = txtStart;
            while (txtEnd < end && base[txtEnd] != ']') txtEnd++;
            if (txtEnd < end && txtEnd + 1 < end && base[txtEnd + 1] == '(') {
                uint32_t urlStart = txtEnd + 2, urlEnd = urlStart;
                while (urlEnd < end && base[urlEnd] != ')') urlEnd++;
                if (urlEnd < end) {
                    if (!push_span(ctx, MD_SPAN_TEXT, s, cur)) return false;
                    MarkdownNode n = {};
                    n.type      = MD_SPAN_LINK;
                    n.offset    = txtStart;
                    n.len       = txtEnd - txtStart;
                    n.urlOffset = urlStart;
                    n.urlLen    = (uint16_t)((urlEnd - urlStart) < 65535 ? (urlEnd - urlStart) : 65535);
                    if (!push_node(ctx, n)) return false;
                    cur = urlEnd + 1; s = cur;
                    continue;
                }
            }
        }

        // Bold: **text** or __text__
        if ((c == '*' || c == '_') && cur + 1 < end && base[cur + 1] == c) {
            uint32_t innerStart = cur + 2, innerEnd = innerStart;
            while (innerEnd + 1 < end && !(base[innerEnd] == c && base[innerEnd + 1] == c))
                innerEnd++;
            if (innerEnd + 1 < end) {
                if (!push_span(ctx, MD_SPAN_TEXT, s, cur)) return false;
                if (!push_span(ctx, MD_SPAN_BOLD, innerStart, innerEnd)) return false;
                cur = innerEnd + 2; s = cur;
                continue;
            }
        }

        // Italic: *text* or _text_
        if (c == '*' || c == '_') {
            uint32_t innerStart = cur + 1, innerEnd = innerStart;
            while (innerEnd < end && base[innerEnd] != c) innerEnd++;
            if (innerEnd < end) {
                if (!push_span(ctx, MD_SPAN_TEXT, s, cur)) return false;
                if (!push_span(ctx, MD_SPAN_ITALIC, innerStart, innerEnd)) return false;
                cur = innerEnd + 1; s = cur;
                continue;
            }
        }

        // Inline code: `code`
        if (c == '`') {
            uint32_t innerStart = cur + 1, innerEnd = innerStart;
            while (innerEnd < end && base[innerEnd] != '`') innerEnd++;
            if (innerEnd < end) {
                if (!push_span(ctx, MD_SPAN_TEXT, s, cur)) return false;
                if (!push_span(ctx, MD_SPAN_CODE, innerStart, innerEnd)) return false;
                cur = innerEnd + 1; s = cur;
                continue;
            }
        }

        cur++;
    }

    return push_span(ctx, MD_SPAN_TEXT, s, end);
}

// ---------------------------------------------------------------------------
// ParseContentIntoNodes
// ---------------------------------------------------------------------------

void ParseContentIntoNodes(ImguiWindowContext* ctx) {
    if (!ctx->mdContent || ctx->mdContentLen == 0)
        return;

    const char* base  = ctx->mdContent;
    uint32_t    total = (uint32_t)ctx->mdContentLen;
    uint32_t    i     = 0;

    // Strip UTF-8 BOM if present
    if (total >= 3 &&
        (unsigned char)base[0] == 0xEF &&
        (unsigned char)base[1] == 0xBB &&
        (unsigned char)base[2] == 0xBF) {
        i = 3;
    }

    bool     inFence    = false;
    uint32_t fenceStart = 0;

    while (i < total) {
        uint32_t lineStart = i;
        while (i < total && base[i] != '\n') i++;
        uint32_t lineEnd = i;
        if (i < total) i++;

        uint32_t    end  = lineEnd;
        if (end > lineStart && base[end - 1] == '\r') end--;
        const char* line = base + lineStart;
        uint32_t    llen = end - lineStart;

        // Fenced code block
        if (llen >= 3 && line[0] == '`' && line[1] == '`' && line[2] == '`') {
            if (!inFence) {
                inFence    = true;
                fenceStart = (uint32_t)(line + 3 - base);
            } else {
                uint32_t fenceEnd = lineStart > 0 ? lineStart - 1 : 0;
                MarkdownNode n = {};
                n.type   = MD_CODE_BLOCK;
                n.offset = fenceStart;
                n.len    = fenceEnd > fenceStart ? fenceEnd - fenceStart : 0;
                if (!push_node(ctx, n)) return;
                inFence = false;
            }
            continue;
        }
        if (inFence) continue;
        if (llen == 0) continue;

        // Headings
        if (line[0] == '#') {
            int level = 0;
            while (level < (int)llen && line[level] == '#') level++;
            if (level <= 3 && level < (int)llen && line[level] == ' ') {
                uint32_t textStart = lineStart + level + 1;
                MarkdownNode n = {};
                n.type   = (uint8_t)(MD_H1 + level - 1);
                n.offset = textStart;
                n.len    = end > textStart ? end - textStart : 0;
                if (!push_node(ctx, n)) return;
                continue;
            }
        }

        // Horizontal rule: ---, ***, ___
        if (llen >= 3) {
            char hr = line[0];
            if (hr == '-' || hr == '*' || hr == '_') {
                bool isHr = true;
                for (uint32_t k = 0; k < llen; k++) {
                    if (line[k] != hr) { isHr = false; break; }
                }
                if (isHr) {
                    MarkdownNode n = {};
                    n.type = MD_HR;
                    if (!push_node(ctx, n)) return;
                    continue;
                }
            }
        }

        // Blockquote
        if (line[0] == '>') {
            uint32_t textStart = lineStart + 1;
            if (textStart < end && base[textStart] == ' ') textStart++;
            MarkdownNode n = {};
            n.type   = MD_BLOCKQUOTE;
            n.offset = textStart;
            n.len    = end > textStart ? end - textStart : 0;
            if (!push_node(ctx, n)) return;
            if (!parse_spans(ctx, base, textStart, end)) return;
            continue;
        }

        // Unordered list
        {
            uint32_t sp = 0;
            while (sp < llen && line[sp] == ' ') sp++;
            if (sp < llen && (line[sp] == '-' || line[sp] == '*') &&
                sp + 1 < llen && line[sp + 1] == ' ') {
                uint32_t textStart = lineStart + sp + 2;
                MarkdownNode n = {};
                n.type   = MD_LIST_UL;
                n.offset = textStart;
                n.len    = end > textStart ? end - textStart : 0;
                n.level  = (uint8_t)(sp / 2);
                if (!push_node(ctx, n)) return;
                if (!parse_spans(ctx, base, textStart, end)) return;
                continue;
            }
        }

        // Ordered list
        {
            uint32_t sp = 0;
            while (sp < llen && line[sp] == ' ') sp++;
            uint32_t d = sp;
            while (d < llen && line[d] >= '0' && line[d] <= '9') d++;
            if (d > sp && d < llen && line[d] == '.' && d + 1 < llen && line[d + 1] == ' ') {
                uint32_t textStart = lineStart + d + 2;
                MarkdownNode n = {};
                n.type   = MD_LIST_OL;
                n.offset = textStart;
                n.len    = end > textStart ? end - textStart : 0;
                n.level  = (uint8_t)(sp / 2);
                if (!push_node(ctx, n)) return;
                if (!parse_spans(ctx, base, textStart, end)) return;
                continue;
            }
        }

        // Table separator: |---|---| — skip silently
        if (line[0] == '|') {
            bool isSep = true;
            for (uint32_t k = 0; k < llen; k++) {
                char ch = line[k];
                if (ch != '|' && ch != '-' && ch != ':' && ch != ' ') {
                    isSep = false;
                    break;
                }
            }
            if (isSep) {
                MarkdownNode n = {};
                n.type = MD_TABLE_SEP;
                if (!push_node(ctx, n)) return;
                continue;
            }

            // Table row: | cell | cell |
            // Emit TABLE_ROW (level = column count), then interleave
            // MD_TABLE_CELL markers with span nodes per cell.
            {
                // Count columns first so we can store it in level
                uint8_t colCount = 0;
                {
                    uint32_t p2 = lineStart;
                    if (p2 < end && base[p2] == '|') p2++;
                    while (p2 < end) {
                        while (p2 < end && base[p2] == ' ') p2++;
                        uint32_t cs = p2;
                        while (p2 < end && base[p2] != '|') p2++;
                        if (p2 > cs) colCount++;
                        if (p2 < end) p2++;
                    }
                }

                MarkdownNode rowNode = {};
                rowNode.type   = MD_TABLE_ROW;
                rowNode.offset = lineStart;
                rowNode.len    = end - lineStart;
                rowNode.level  = colCount;
                if (!push_node(ctx, rowNode)) return;

                uint32_t p = lineStart;
                if (p < end && base[p] == '|') p++;
                bool firstCell = true;
                while (p < end) {
                    while (p < end && base[p] == ' ') p++;
                    uint32_t cellStart = p;
                    while (p < end && base[p] != '|') p++;
                    uint32_t cellEnd = p;
                    while (cellEnd > cellStart && base[cellEnd - 1] == ' ') cellEnd--;

                    if (!firstCell) {
                        // Cell boundary marker
                        MarkdownNode cellMark = {};
                        cellMark.type = MD_TABLE_CELL;
                        if (!push_node(ctx, cellMark)) return;
                    }
                    firstCell = false;

                    if (cellEnd > cellStart) {
                        if (!parse_spans(ctx, base, cellStart, cellEnd)) return;
                    }
                    if (p < end) p++;
                }
                continue;
            }
        }

        // Paragraph
        {
            MarkdownNode n = {};
            n.type   = MD_PARA;
            n.offset = lineStart;
            n.len    = end > lineStart ? end - lineStart : 0;
            if (!push_node(ctx, n)) return;
            if (!parse_spans(ctx, base, lineStart, end)) return;
        }
    }
}

// ---------------------------------------------------------------------------
// RenderFromNodes helpers
// ---------------------------------------------------------------------------

static void render_span_text(const char* base, uint32_t offset, uint32_t len) {
    ImGui::TextUnformatted(base + offset, base + offset + len);
}

static void render_inline_code_bg(const char* base, uint32_t offset, uint32_t len) {
    ImVec2 pos  = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::CalcTextSize(base + offset, base + offset + len);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(pos.x - 2, pos.y),
        ImVec2(pos.x + size.x + 2, pos.y + size.y),
        IM_COL32(60, 60, 60, 200), 3.0f);
    ImGui::TextUnformatted(base + offset, base + offset + len);
}

static void render_spans(ImguiWindowContext* ctx, int lineIdx) {
    const char* base = ctx->mdContent;
    int i = lineIdx + 1;
    bool first = true;
    while (i < ctx->mdNodeCount && ctx->mdNodes[i].type >= MD_SPAN_TEXT) {
        const MarkdownNode& n = ctx->mdNodes[i];
        if (!first)
            ImGui::SameLine(0, 0);
        first = false;
        switch (n.type) {
        case MD_SPAN_TEXT:
            render_span_text(base, n.offset, n.len);
            break;
        case MD_SPAN_BOLD:
            // ImGui has no runtime bold/italic — bold and italic are entirely separate
            // TTF files rasterized into separate ImFont* atlas slots at startup.
            // To render true bold you call PushFont(font_bold) / PopFont(), but that
            // requires a bold TTF to have been loaded into ImguiWindowContext during
            // session init (font_bold field, see Open Questions in plans/imgui-markdown.md).
            // Until that TTF loading work is done we use a gold accent colour as a
            // visible stand-in. When font_bold is available, replace PushStyleColor
            // with PushFont(ctx->font_bold) and PopFont() — no other changes needed.
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
            render_span_text(base, n.offset, n.len);
            ImGui::PopStyleColor();
            break;
        case MD_SPAN_ITALIC:
            // Same constraint as bold above — italic requires a separate italic TTF
            // loaded into ctx->font_italic at startup. Dimmed colour used as stand-in.
            // Replace PushStyleColor with PushFont(ctx->font_italic) / PopFont() once
            // the TTF loading plan step is implemented.
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
            render_span_text(base, n.offset, n.len);
            ImGui::PopStyleColor();
            break;
        case MD_SPAN_CODE:
            render_inline_code_bg(base, n.offset, n.len);
            break;
        case MD_SPAN_LINK: {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.7f, 1.0f, 1.0f));
            render_span_text(base, n.offset, n.len);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%.*s", (int)n.urlLen, base + n.urlOffset);
                if (ImGui::IsMouseReleased(0)) {
                    char url[2048];
                    int ulen = n.urlLen < 2047 ? (int)n.urlLen : 2047;
                    memcpy(url, base + n.urlOffset, ulen);
                    url[ulen] = '\0';
                    SDL_OpenURL(url);
                }
            }
            ImVec2 rMin = ImGui::GetItemRectMin();
            ImVec2 rMax = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(rMin.x, rMax.y), ImVec2(rMax.x, rMax.y),
                IM_COL32(77, 179, 255, 200), 1.0f);
            break;
        }
        case MD_IMAGE: {
            const ImguiTexture* tex = resolve_texture(
                ctx,
                base + n.urlOffset, (int)n.urlLen);
            if (tex && tex->glId != 0) {
                ImGui::Image(
                    (ImTextureID)(uintptr_t)tex->glId,
                    ImVec2((float)tex->width, (float)tex->height));
            } else {
                char buf[512];
                int  ilen = n.urlLen < 511 ? (int)n.urlLen : 511;
                snprintf(buf, sizeof(buf), "[Image: %.*s]", ilen, base + n.urlOffset);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::TextUnformatted(buf);
                ImGui::PopStyleColor();
            }
            break;
        }
        default: break;
        }
        i++;
    }
}

static int next_line_node(ImguiWindowContext* ctx, int idx) {
    int i = idx + 1;
    while (i < ctx->mdNodeCount && ctx->mdNodes[i].type >= MD_SPAN_TEXT)
        i++;
    return i;
}

// ---------------------------------------------------------------------------
// RenderFromNodes
// ---------------------------------------------------------------------------

void RenderFromNodes(ImguiWindowContext* ctx, float w, float h) {
    ImGui::BeginChild("##mdrender", ImVec2(w, h));

    if (ctx->mdCacheError) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", ctx->mdCacheError);
        ImGui::PopStyleColor();
        ImGui::EndChild();
        return;
    }

    if (!ctx->mdNodes || ctx->mdNodeCount == 0) {
        ImGui::EndChild();
        return;
    }

    const char* base = ctx->mdContent;

    for (int i = 0; i < ctx->mdNodeCount; ) {
        const MarkdownNode& n = ctx->mdNodes[i];

        switch (n.type) {
        case MD_H1:
        case MD_H2:
        case MD_H3: {
            float scale = (n.type == MD_H1) ? 1.8f : (n.type == MD_H2) ? 1.4f : 1.15f;
            ImGui::SetWindowFontScale(scale);
            ImGui::TextUnformatted(base + n.offset, base + n.offset + n.len);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::Separator();
            i = next_line_node(ctx, i);
            break;
        }
        case MD_HR:
            ImGui::Separator();
            i++;
            break;
        case MD_CODE_BLOCK: {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
            ImGui::BeginChild("##codeblock", ImVec2(avail.x, 0), true);
            ImGui::TextUnformatted(base + n.offset, base + n.offset + n.len);
            ImGui::EndChild();
            ImGui::PopStyleColor();
            i++;
            break;
        }
        case MD_BLOCKQUOTE:
            ImGui::Indent(8.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            render_spans(ctx, i);
            ImGui::PopStyleColor();
            ImGui::Unindent(8.0f);
            i = next_line_node(ctx, i);
            break;
        case MD_LIST_UL:
        case MD_LIST_OL: {
            float indent = n.level * 16.0f;
            if (indent > 0.0f) ImGui::Indent(indent);
            ImGui::Bullet();
            ImGui::SameLine(0, 4);
            render_spans(ctx, i);
            if (indent > 0.0f) ImGui::Unindent(indent);
            i = next_line_node(ctx, i);
            break;
        }
        case MD_PARA:
            render_spans(ctx, i);
            i = next_line_node(ctx, i);
            break;
        case MD_TABLE_SEP:
            i++;
            break;
        case MD_TABLE_ROW: {
            // Count how many consecutive table rows (including separators) follow
            // so we know the column count before calling BeginTable.
            int cols = (int)n.level;
            if (cols < 1) cols = 1;

            // Find the run of table rows/seps to pass to BeginTable once
            int runEnd = i;
            while (runEnd < ctx->mdNodeCount &&
                (ctx->mdNodes[runEnd].type == MD_TABLE_ROW ||
                 ctx->mdNodes[runEnd].type == MD_TABLE_SEP  ||
                 ctx->mdNodes[runEnd].type == MD_TABLE_CELL ||
                 ctx->mdNodes[runEnd].type >= MD_SPAN_TEXT))
                runEnd++;

            ImGuiTableFlags tflags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
            if (ImGui::BeginTable("##mdtable", cols, tflags)) {
                int j = i;
                bool headerDone = false;
                while (j < runEnd) {
                    if (ctx->mdNodes[j].type == MD_TABLE_SEP) {
                        j++;
                        continue;
                    }
                    if (ctx->mdNodes[j].type != MD_TABLE_ROW) {
                        j++;
                        continue;
                    }

                    // One row
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    j++; // skip the TABLE_ROW node itself

                    // Render spans for each cell, advancing column on TABLE_CELL
                    while (j < runEnd && ctx->mdNodes[j].type != MD_TABLE_ROW && ctx->mdNodes[j].type != MD_TABLE_SEP) {
                        if (ctx->mdNodes[j].type == MD_TABLE_CELL) {
                            ImGui::TableNextColumn();
                            j++;
                            continue;
                        }
                        // Span node
                        const MarkdownNode& sn = ctx->mdNodes[j];
                        switch (sn.type) {
                        case MD_SPAN_TEXT:
                            ImGui::TextUnformatted(base + sn.offset, base + sn.offset + sn.len);
                            break;
                        case MD_SPAN_BOLD:
                            // See render_spans for explanation — bold requires a separate
                            // bold TTF loaded into ctx->font_bold. Colour used as stand-in.
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
                            ImGui::TextUnformatted(base + sn.offset, base + sn.offset + sn.len);
                            ImGui::PopStyleColor();
                            break;
                        case MD_SPAN_ITALIC:
                            // See render_spans for explanation — italic requires a separate
                            // italic TTF loaded into ctx->font_italic. Colour used as stand-in.
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
                            ImGui::TextUnformatted(base + sn.offset, base + sn.offset + sn.len);
                            ImGui::PopStyleColor();
                            break;
                        case MD_SPAN_CODE:
                            render_inline_code_bg(base, sn.offset, sn.len);
                            break;
                        case MD_SPAN_LINK:
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.7f, 1.0f, 1.0f));
                            ImGui::TextUnformatted(base + sn.offset, base + sn.offset + sn.len);
                            ImGui::PopStyleColor();
                            break;
                        case MD_IMAGE: {
                            const ImguiTexture* tex = resolve_texture(
                                ctx,
                                base + sn.urlOffset, (int)sn.urlLen);
                            if (tex && tex->glId != 0) {
                                ImGui::Image(
                                    (ImTextureID)(uintptr_t)tex->glId,
                                    ImVec2((float)tex->width, (float)tex->height));
                            } else {
                                char buf[512];
                                int  ilen = sn.urlLen < 511 ? (int)sn.urlLen : 511;
                                snprintf(buf, sizeof(buf), "[Image: %.*s]", ilen, base + sn.urlOffset);
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                                ImGui::TextUnformatted(buf);
                                ImGui::PopStyleColor();
                            }
                            break;
                        }
                            ImGui::TextUnformatted(base + sn.offset, base + sn.offset + sn.len);
                            break;
                        }
                        j++;
                    }
                }
                ImGui::EndTable();
            }
            i = runEnd;
            break;
        }
        default:
            i++;
            break;
        }
    }

    ImGui::EndChild();
}

#endif // KITSUNE_IMGUI
