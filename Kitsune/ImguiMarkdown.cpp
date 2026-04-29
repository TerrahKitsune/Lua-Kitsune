#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef KITSUNE_IMGUI

#include "ImguiMarkdown.h"
#include "OpenGL.h"
#include "Imgui/imgui.h"
#include "Font.h"
#include "RenderLoop.h"
#include <SDL.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// FreeMarkdownCache
// ---------------------------------------------------------------------------

void FreeMarkdownCache(MarkdownResource* md) {
	free(md->mdContent);
	free(md->mdNodes);
	md->mdContent = nullptr;
	md->mdContentLen = 0;
	md->mdNodes = nullptr;
	md->mdNodeCount = 0;
	md->mdNodeAlloc = 0;
	md->mdCacheError = nullptr;
}

static void markdown_finalizer(Resource* res) {
	MarkdownResource* md = (MarkdownResource*)res;
	FreeMarkdownCache(md);
	free(md->resource.source);
	free(md);
}

// ---------------------------------------------------------------------------
// ReadStreamIntoCache — uses KitsuneCallMethod only, no lua_State
// ---------------------------------------------------------------------------

void ReadStreamIntoCache(const KitsuneVariable* streamVar, MarkdownResource* md) {
	// stream:Seek(0)
	{
		KitsuneVariable seekArg = {};
		seekArg.type = KITSUNE_TINTEGER;
		seekArg.integer = 0;
		KitsuneVariable* seekResult = KitsuneCallMethod(streamVar, "Seek", 1, &seekArg);
		if (!seekResult || seekResult->type == KITSUNE_TERROR) {
			md->mdCacheError = "MarkdownRender: stream:Seek(0) failed";
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
			md->mdCacheError = "MarkdownRender: stream:Read() returned no data";
			KitsuneVariableFree(readResult);
			return;
		}

		char* buf = (char*)malloc(readResult->length + 1);
		if (!buf) {
			md->mdCacheError = "MarkdownRender: out of memory reading stream";
			KitsuneVariableFree(readResult);
			return;
		}
		memcpy(buf, readResult->data, readResult->length);
		buf[readResult->length] = '\0';
		md->mdContent = buf;
		md->mdContentLen = readResult->length;
		KitsuneVariableFree(readResult);
	}
}

// ---------------------------------------------------------------------------
// ParseContentIntoNodes — helpers
// ---------------------------------------------------------------------------

static bool push_node(MarkdownResource* md, MarkdownNode node) {
	if (md->mdNodeCount + 1 >= md->mdNodeAlloc) {
		int newAlloc = md->mdNodeAlloc == 0 ? 64 : md->mdNodeAlloc * 2;
		MarkdownNode* grown = (MarkdownNode*)realloc(md->mdNodes, (size_t)newAlloc * sizeof(MarkdownNode));
		if (!grown) {
			md->mdCacheError = "MarkdownRender: out of memory building node array";
			return false;
		}
		md->mdNodes = grown;
		md->mdNodeAlloc = newAlloc;
	}
	md->mdNodes[md->mdNodeCount++] = node;
	return true;
}

static bool push_span(MarkdownResource* md, uint8_t type,
	uint32_t start, uint32_t end,
	uint32_t urlOff = 0, uint16_t urlLen = 0) {
	if (end <= start)
		return true;
	MarkdownNode n = {};
	n.type = type;
	n.offset = start;
	n.len = end - start;
	n.urlOffset = urlOff;
	n.urlLen = urlLen;
	return push_node(md, n);
}

// ---------------------------------------------------------------------------
// Span parser — emits MD_SPAN_* nodes for a single line region [s, end)
// ---------------------------------------------------------------------------

static bool parse_spans(MarkdownResource* md, const char* base,
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
					if (!push_span(md, MD_SPAN_TEXT, s, cur)) return false;
					MarkdownNode n = {};
					n.type = MD_IMAGE;
					n.offset = altStart;
					n.len = altEnd - altStart;
					n.urlOffset = idStart;
					n.urlLen = (uint16_t)((idEnd - idStart) < 65535 ? (idEnd - idStart) : 65535);
					if (!push_node(md, n)) return false;
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
					if (!push_span(md, MD_SPAN_TEXT, s, cur)) return false;
					MarkdownNode n = {};
					n.type = MD_SPAN_LINK;
					n.offset = txtStart;
					n.len = txtEnd - txtStart;
					n.urlOffset = urlStart;
					n.urlLen = (uint16_t)((urlEnd - urlStart) < 65535 ? (urlEnd - urlStart) : 65535);
					if (!push_node(md, n)) return false;
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
				if (!push_span(md, MD_SPAN_TEXT, s, cur)) return false;
				if (!push_span(md, MD_SPAN_BOLD, innerStart, innerEnd)) return false;
				cur = innerEnd + 2; s = cur;
				continue;
			}
		}

		// Italic: *text* or _text_
		if (c == '*' || c == '_') {
			uint32_t innerStart = cur + 1, innerEnd = innerStart;
			while (innerEnd < end && base[innerEnd] != c) innerEnd++;
			if (innerEnd < end) {
				if (!push_span(md, MD_SPAN_TEXT, s, cur)) return false;
				if (!push_span(md, MD_SPAN_ITALIC, innerStart, innerEnd)) return false;
				cur = innerEnd + 1; s = cur;
				continue;
			}
		}

		// Inline code: `code`
		if (c == '`') {
			uint32_t innerStart = cur + 1, innerEnd = innerStart;
			while (innerEnd < end && base[innerEnd] != '`') innerEnd++;
			if (innerEnd < end) {
				if (!push_span(md, MD_SPAN_TEXT, s, cur)) return false;
				if (!push_span(md, MD_SPAN_CODE, innerStart, innerEnd)) return false;
				cur = innerEnd + 1; s = cur;
				continue;
			}
		}

		cur++;
	}

	return push_span(md, MD_SPAN_TEXT, s, end);
}

// ---------------------------------------------------------------------------
// ParseContentIntoNodes
// ---------------------------------------------------------------------------

void ParseContentIntoNodes(MarkdownResource* md) {
	if (!md->mdContent || md->mdContentLen == 0)
		return;

	const char* base = md->mdContent;
	uint32_t    total = (uint32_t)md->mdContentLen;
	uint32_t    i = 0;

	// Strip UTF-8 BOM if present
	if (total >= 3 &&
		(unsigned char)base[0] == 0xEF &&
		(unsigned char)base[1] == 0xBB &&
		(unsigned char)base[2] == 0xBF) {
		i = 3;
	}

	bool     inFence = false;
	uint32_t fenceStart = 0;

	while (i < total) {
		uint32_t lineStart = i;
		while (i < total&& base[i] != '\n') i++;
		uint32_t lineEnd = i;
		if (i < total) i++;

		uint32_t    end = lineEnd;
		if (end > lineStart && base[end - 1] == '\r') end--;
		const char* line = base + lineStart;
		uint32_t    llen = end - lineStart;

		// Fenced code block
		if (llen >= 3 && line[0] == '`' && line[1] == '`' && line[2] == '`') {
			if (!inFence) {
				inFence = true;
				// fenceStart points to the first byte of the next line,
				// skipping the opening ``` and any language identifier on this line.
				fenceStart = i;
			}
			else {
				uint32_t fenceEnd = lineStart > 0 ? lineStart - 1 : 0;
				MarkdownNode n = {};
				n.type = MD_CODE_BLOCK;
				n.offset = fenceStart;
				n.len = fenceEnd > fenceStart ? fenceEnd - fenceStart : 0;
				if (!push_node(md, n)) return;
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
				n.type = (uint8_t)(MD_H1 + level - 1);
				n.offset = textStart;
				n.len = end > textStart ? end - textStart : 0;
				if (!push_node(md, n)) return;
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
					if (!push_node(md, n)) return;
					continue;
				}
			}
		}

		// Blockquote
		if (line[0] == '>') {
			uint32_t textStart = lineStart + 1;
			if (textStart < end&& base[textStart] == ' ') textStart++;
			MarkdownNode n = {};
			n.type = MD_BLOCKQUOTE;
			n.offset = textStart;
			n.len = end > textStart ? end - textStart : 0;
			if (!push_node(md, n)) return;
			if (!parse_spans(md, base, textStart, end)) return;
			continue;
		}

		// Unordered list
		{
			uint32_t sp = 0;
			while (sp < llen && line[sp] == ' ') sp++;
			if (sp < llen && (line[sp] == '-' || line[sp] == '*') &&
				sp + 1 < llen && line[sp + 1] == ' ') {
				// Skip marker and all following spaces (e.g. "*   text" -> "text")
				uint32_t after = sp + 1;
				while (after < llen && line[after] == ' ') after++;
				uint32_t textStart = lineStart + after;
				MarkdownNode n = {};
				n.type = MD_LIST_UL;
				n.offset = textStart;
				n.len = end > textStart ? end - textStart : 0;
				n.level = (uint8_t)(sp / 2);
				if (!push_node(md, n)) return;
				if (!parse_spans(md, base, textStart, end)) return;
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
				n.type = MD_LIST_OL;
				n.offset = textStart;
				n.len = end > textStart ? end - textStart : 0;
				n.level = (uint8_t)(sp / 2);
				if (!push_node(md, n)) return;
				if (!parse_spans(md, base, textStart, end)) return;
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
				if (!push_node(md, n)) return;
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
				rowNode.type = MD_TABLE_ROW;
				rowNode.offset = lineStart;
				rowNode.len = end - lineStart;
				rowNode.level = colCount;
				if (!push_node(md, rowNode)) return;

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
						if (!push_node(md, cellMark)) return;
					}
					firstCell = false;

					if (cellEnd > cellStart) {
						if (!parse_spans(md, base, cellStart, cellEnd)) return;
					}
					if (p < end) p++;
				}
				continue;
			}
		}

		// Paragraph
		{
			MarkdownNode n = {};
			n.type = MD_PARA;
			n.offset = lineStart;
			n.len = end > lineStart ? end - lineStart : 0;
			if (!push_node(md, n)) return;
			if (!parse_spans(md, base, lineStart, end)) return;
		}
	}

	// If input ended while still inside an unclosed fenced code block,
	// emit whatever was accumulated so the content is not silently dropped.
	if (inFence && total > fenceStart) {
		uint32_t fenceEnd = total;
		// strip trailing newline so the code block doesn't have a blank last line
		if (fenceEnd > fenceStart && base[fenceEnd - 1] == '\n') fenceEnd--;
		if (fenceEnd > fenceStart && base[fenceEnd - 1] == '\r') fenceEnd--;
		MarkdownNode n = {};
		n.type = MD_CODE_BLOCK;
		n.offset = fenceStart;
		n.len = fenceEnd > fenceStart ? fenceEnd - fenceStart : 0;
		push_node(md, n);
	}
}

// ---------------------------------------------------------------------------
// RenderFromNodes helpers
// ---------------------------------------------------------------------------

// Returns the index of the first node after lineIdx that is not a span node.
static int next_line_node(MarkdownResource* md, int lineIdx) {
    int i = lineIdx + 1;
    while (i < md->mdNodeCount && md->mdNodes[i].type >= MD_SPAN_TEXT)
        i++;
    return i;
}

// Draws the background rect for an inline code span, then the text.
static void render_inline_code_bg(const char* base, uint32_t offset, uint32_t len) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::CalcTextSize(base + offset, base + offset + len);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(pos.x - 2, pos.y),
        ImVec2(pos.x + size.x + 2, pos.y + size.y),
        IM_COL32(60, 60, 60, 200), 3.0f);
    ImGui::TextUnformatted(base + offset, base + offset + len);
}

// Renders text with word-wrap using the same technique as imgui_markdown:
// CalcWordWrapPositionA gives the split point for the available width,
// then we TextUnformatted that chunk and loop for the rest at full width.
// Returns whether any text was rendered (so the caller can SameLine before the next span).
static bool render_text_wrapped(const char* text, const char* textEnd) {
	float scale = ImGui::GetIO().FontGlobalScale;
	float widthLeft = ImGui::GetContentRegionAvail().x;
	if (widthLeft <= 0.0f)
		widthLeft = ImGui::GetWindowSize().x;

	const char* endLine = ImGui::GetFont()->CalcWordWrapPositionA(scale, text, textEnd, widthLeft);
	if (endLine <= text)
		endLine = text + 1;

	ImGui::TextUnformatted(text, endLine);

	while (endLine < textEnd) {
		text = endLine;
		if (*text == ' ')
			text++;
		widthLeft = ImGui::GetContentRegionAvail().x;
		endLine = ImGui::GetFont()->CalcWordWrapPositionA(scale, text, textEnd, widthLeft);
		if (endLine <= text)
			endLine = text + 1;
		ImGui::TextUnformatted(text, endLine);
	}
	return true;
}

// Renders all span nodes following lineIdx.
// Uses CalcWordWrapPositionA (same as imgui_markdown) so mixed bold/italic/normal
// text wraps correctly. SameLine(0,0) is used between spans as long as the
// previous widget was a single-line TextUnformatted (which it always is here).
static void render_spans(MarkdownResource* md, ImguiWindowContext* ctx, int lineIdx) {
	const char* base = md->mdContent;
	int i = lineIdx + 1;
	bool first = true;
			while (i < md->mdNodeCount && md->mdNodes[i].type >= MD_SPAN_TEXT) {
				const MarkdownNode& n = md->mdNodes[i];
				if (!first)
					ImGui::SameLine(0, 0);
				first = false;

				switch (n.type) {
				case MD_SPAN_TEXT:
					render_text_wrapped(base + n.offset, base + n.offset + n.len);
					break;
				case MD_SPAN_BOLD:
					ImguiPushFontStyle(IMGUI_STACK_BOLD, FONT_STYLE_BOLD,
						IMGUI_BOLD_FALLBACK_R, IMGUI_BOLD_FALLBACK_G, IMGUI_BOLD_FALLBACK_B, IMGUI_BOLD_FALLBACK_A);
					render_text_wrapped(base + n.offset, base + n.offset + n.len);
					ImguiPopFontStyle(IMGUI_STACK_BOLD);
					break;
				case MD_SPAN_ITALIC:
					ImguiPushFontStyle(IMGUI_STACK_ITALIC, FONT_STYLE_ITALIC,
						IMGUI_ITALIC_FALLBACK_R, IMGUI_ITALIC_FALLBACK_G, IMGUI_ITALIC_FALLBACK_B, IMGUI_ITALIC_FALLBACK_A);
					render_text_wrapped(base + n.offset, base + n.offset + n.len);
					ImguiPopFontStyle(IMGUI_STACK_ITALIC);
					break;
				case MD_SPAN_CODE:
					render_inline_code_bg(base, n.offset, n.len);
					break;
				case MD_SPAN_LINK: {
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.7f, 1.0f, 1.0f));
					render_text_wrapped(base + n.offset, base + n.offset + n.len);
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
					const ImguiTexture* tex = resolve_texture(ctx, base + n.urlOffset, (int)n.urlLen);
					if (tex && tex->glId != 0) {
						unsigned int glId = ResolveTextureGlId(tex->resource.luaId);
						ImGui::Image((ImTextureID)(uintptr_t)glId,
							ImVec2((float)tex->width, (float)tex->height));
					}
					else {
						char buf[512];
						int ilen = n.urlLen < 511 ? (int)n.urlLen : 511;
						snprintf(buf, sizeof(buf), "[Image: %.*s]", ilen, base + n.urlOffset);
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
						render_text_wrapped(buf, buf + strlen(buf));
						ImGui::PopStyleColor();
					}
					break;
				}
				default:
					render_text_wrapped(base + n.offset, base + n.offset + n.len);
					break;
				}
				i++;
			}
		}

// ---------------------------------------------------------------------------
// RenderFromNodes
// ---------------------------------------------------------------------------

void RenderFromNodes(MarkdownResource* md, ImguiWindowContext* ctx, float w, float h) {
	if (md->mdCacheError) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
		ImGui::TextWrapped("%s", md->mdCacheError);
		ImGui::PopStyleColor();
		return;
	}

	if (!md->mdNodes || md->mdNodeCount == 0)
		return;

	const char* base = md->mdContent;

	for (int i = 0; i < md->mdNodeCount; ) {
		const MarkdownNode& n = md->mdNodes[i];

		switch (n.type) {
		case MD_H1:
		case MD_H2:
		case MD_H3: {
			float scale = (n.type == MD_H1) ? 1.8f : (n.type == MD_H2) ? 1.4f : 1.15f;
			ImGui::SetWindowFontScale(scale);
			render_spans(md, ctx, i);
			ImGui::SetWindowFontScale(1.0f);
			ImGui::Separator();
			i = next_line_node(md, i);
			break;
		}
		case MD_HR:
			ImGui::Separator();
			i++;
			break;
		case MD_CODE_BLOCK: {
			const char* codeStart = base + n.offset;
			size_t codeLen = n.len;
			ImVec2 avail = ImGui::GetContentRegionAvail();
			ImFont* font = ImGui::GetIO().FontDefault;
			float fontSize = font ? font->FontSize : ImGui::GetFontSize();
			float lineHeight = fontSize + ImGui::GetStyle().ItemSpacing.y;

			// Size child to content
			int lines = 1;
			for (size_t ci = 0; ci < codeLen; ci++)
				if (codeStart[ci] == '\n') lines++;
			float childH = lines * lineHeight + ImGui::GetStyle().FramePadding.y * 4.0f + 4.0f;

			// InputTextMultiline requires a null-terminated buffer; mdContent is one
			// contiguous block so codeStart is not null-terminated at codeStart+codeLen.
			char* codeBuf = (char*)malloc(codeLen + 1);
			if (codeBuf) {
				memcpy(codeBuf, codeStart, codeLen);
				codeBuf[codeLen] = '\0';

				char inputId[32];
				snprintf(inputId, sizeof(inputId), "##cb%d", i);
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
				ImGui::InputTextMultiline(
					inputId,
					codeBuf,
					codeLen + 1,
					ImVec2(avail.x, childH),
					ImGuiInputTextFlags_ReadOnly);
				ImGui::PopStyleColor();
				free(codeBuf);
			}
			i++;
			break;
		}
		case MD_BLOCKQUOTE:
			ImGui::Indent(8.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
			render_spans(md, ctx, i);
			ImGui::PopStyleColor();
			ImGui::Unindent(8.0f);
			i = next_line_node(md, i);
			break;
		case MD_LIST_UL:
		case MD_LIST_OL: {
			float indent = n.level * 16.0f;
			if (indent > 0.0f)
				ImGui::Indent(indent);
			ImGui::Bullet();
			ImGui::SameLine(0, 4);
			render_spans(md, ctx, i);
			if (indent > 0.0f)
				ImGui::Unindent(indent);
			i = next_line_node(md, i);
			break;
		}
		case MD_PARA:
			render_spans(md, ctx, i);
			i = next_line_node(md, i);
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
			while (runEnd < md->mdNodeCount &&
				(md->mdNodes[runEnd].type == MD_TABLE_ROW ||
					md->mdNodes[runEnd].type == MD_TABLE_SEP ||
					md->mdNodes[runEnd].type == MD_TABLE_CELL ||
					md->mdNodes[runEnd].type >= MD_SPAN_TEXT))
				runEnd++;

			// Count rows so we can give BeginChild a stable explicit height.
				// AutoResizeY + HorizontalScrollbar can feedback-loop; explicit height avoids that.
				int rowCount = 0;
				for (int r = i; r < runEnd; r++)
					if (md->mdNodes[r].type == MD_TABLE_ROW) rowCount++;
				float rowH = ImGui::GetTextLineHeightWithSpacing()
					+ ImGui::GetStyle().CellPadding.y * 2.0f;
				float childH = rowCount * rowH
					+ ImGui::GetStyle().ScrollbarSize        // room for h-scrollbar if needed
					+ ImGui::GetStyle().FramePadding.y * 2.0f;

				// BeginChild is self-contained: its inner content width does NOT affect the
				// parent window's content width, so PushTextWrapPos(0.0f) outside the child
				// still resolves to the correct parent width for following paragraphs.
				char childId[32];
				snprintf(childId, sizeof(childId), "##mdtbl%d", i);
				ImGui::BeginChild(childId, ImVec2(0.0f, childH), ImGuiChildFlags_None,
					ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
				ImGuiTableFlags tflags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
					| ImGuiTableFlags_SizingFixedFit;
				if (ImGui::BeginTable("##mdtable", cols, tflags)) {
					int j = i;
					while (j < runEnd) {
						if (md->mdNodes[j].type == MD_TABLE_SEP) {
							j++;
							continue;
						}
						if (md->mdNodes[j].type != MD_TABLE_ROW) {
							j++;
							continue;
						}

						// One row
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						j++; // skip the TABLE_ROW node itself

						// Render spans for each cell, advancing column on TABLE_CELL
						while (j < runEnd && md->mdNodes[j].type != MD_TABLE_ROW && md->mdNodes[j].type != MD_TABLE_SEP) {
							if (md->mdNodes[j].type == MD_TABLE_CELL) {
								ImGui::TableNextColumn();
								j++;
								continue;
							}
							// Span node
							const MarkdownNode& sn = md->mdNodes[j];
							switch (sn.type) {
							case MD_SPAN_TEXT:
								ImGui::TextUnformatted(base + sn.offset, base + sn.offset + sn.len);
								break;
							case MD_SPAN_BOLD:
								ImguiPushFontStyle(IMGUI_STACK_BOLD, FONT_STYLE_BOLD,
									IMGUI_BOLD_FALLBACK_R, IMGUI_BOLD_FALLBACK_G, IMGUI_BOLD_FALLBACK_B, IMGUI_BOLD_FALLBACK_A);
								ImGui::TextUnformatted(base + sn.offset, base + sn.offset + sn.len);
								ImguiPopFontStyle(IMGUI_STACK_BOLD);
								break;
							case MD_SPAN_ITALIC:
								ImguiPushFontStyle(IMGUI_STACK_ITALIC, FONT_STYLE_ITALIC,
									IMGUI_ITALIC_FALLBACK_R, IMGUI_ITALIC_FALLBACK_G, IMGUI_ITALIC_FALLBACK_B, IMGUI_ITALIC_FALLBACK_A);
								ImGui::TextUnformatted(base + sn.offset, base + sn.offset + sn.len);
								ImguiPopFontStyle(IMGUI_STACK_ITALIC);
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
									unsigned int glId = ResolveTextureGlId(tex->resource.luaId);
									ImGui::Image(
										(ImTextureID)(uintptr_t)glId,
										ImVec2((float)tex->width, (float)tex->height));
								}
								else {
									char buf[512];
									int ilen = sn.urlLen < 511 ? (int)sn.urlLen : 511;
									snprintf(buf, sizeof(buf), "[Image: %.*s]", ilen, base + sn.urlOffset);
									ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
									ImGui::TextUnformatted(buf);
									ImGui::PopStyleColor();
								}
								break;
							}
							default:
								ImGui::TextUnformatted(base + sn.offset, base + sn.offset + sn.len);
								break;
							}
							j++;
						}
					}
							ImGui::EndTable();
						}
						ImGui::EndChild();
						i = runEnd;
						break;
		}
		default:
			i++;
			break;
		}
	}

}

// ---------------------------------------------------------------------------
// Markdown.Parse(text|stream) -> luaId  |  Markdown.Destroy(luaId)
// ---------------------------------------------------------------------------

// Markdown.Parse(text|stream) -> integer luaId | nil
static int Markdown_Parse(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TNIL;
	if (argc < 1) {
		setter(&r);
		return 1;
	}

	MarkdownResource* md = (MarkdownResource*)calloc(1, sizeof(MarkdownResource));
	if (!md) {
		setter(&r);
		return 1;
	}
	md->resource.type = RESOURCE_MARKDOWN;
	md->resource.fn = markdown_finalizer;

	if (argv[0].type == KITSUNE_TSTRING) {
		if (argv[0].length == 0) {
			free(md);
			setter(&r);
			return 1;
		}
		char* buf = (char*)malloc(argv[0].length + 1);
		if (!buf) {
			free(md);
			setter(&r);
			return 1;
		}
		memcpy(buf, argv[0].data, argv[0].length);
		buf[argv[0].length] = '\0';
		md->mdContent = buf;
		md->mdContentLen = argv[0].length;
		ParseContentIntoNodes(md);
	}
	else if (argv[0].type == KITSUNE_TUSERDATA && argv[0].userdata
		&& argv[0].userdata->name && strcmp(argv[0].userdata->name, "STREAM") == 0) {
		ReadStreamIntoCache(&argv[0], md);
		if (!md->mdCacheError)
			ParseContentIntoNodes(md);
	}
	else {
		free(md);
		setter(&r);
		return 1;
	}

	if (!ResourceCacheAdd(&md->resource)) {
		markdown_finalizer(&md->resource);
		setter(&r);
		return 1;
	}

	r.type = KITSUNE_TINTEGER;
	r.integer = md->resource.luaId;
	setter(&r);
	return 1;
}

// Markdown.Destroy(luaId) — remove from cache and free
static int Markdown_Destroy(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (argc < 1)
		return 0;
	int luaId = (int)KitsuneAsInt(&argv[0], 0);
	if (luaId > 0)
		ResourceCacheRemoveById(luaId, RESOURCE_MARKDOWN);
	return 0;
}

void RegisterMarkdownFunctions() {
	KitsuneRegisterFunction("Markdown.Parse", Markdown_Parse, nullptr);
	KitsuneRegisterFunction("Markdown.Destroy", Markdown_Destroy, nullptr);
}

#endif // KITSUNE_IMGUI
