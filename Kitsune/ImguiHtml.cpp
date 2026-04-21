#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef KITSUNE_IMGUI

#include "ImguiHtml.h"
#include "ImguiSession.h"
#include "OpenGL.h"
#include "ResourceCache.h"
#include "KitsuneEngine.h"
#include "Imgui/imgui.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdint>

// ---------------------------------------------------------------------------
// Global container instance
// ---------------------------------------------------------------------------

static KitsuneHtmlContainer s_container;

// Track all live HtmlDocument allocations so HtmlShutdown can destroy them
// even if Lua GC hasn't run (e.g. documents still referenced in tables).
static std::vector<HtmlDocument*> s_liveDocs;

KitsuneHtmlContainer* HtmlGetContainer() {
    return &s_container;
}

// ---------------------------------------------------------------------------
// Userdata type name
// ---------------------------------------------------------------------------

static const char* HTML_DOC_TYPENAME = "HtmlDocument";

// ---------------------------------------------------------------------------
// Error helper
// ---------------------------------------------------------------------------

static int html_error(const kitsune_ResultSetter setter, const char* msg) {
    KitsuneVariable e = {};
    e.type = KITSUNE_TERROR;
    e.data = (unsigned char*)msg;
    e.length = strlen(msg);
    setter(&e);
    return 1;
}

// ---------------------------------------------------------------------------
// Web color -> ImU32
// ---------------------------------------------------------------------------

static ImU32 web_color_to_imu32(const litehtml::web_color& c) {
    return IM_COL32(c.red, c.green, c.blue, c.alpha);
}

// ---------------------------------------------------------------------------
// KitsuneHtmlContainer — document_container implementation
// ---------------------------------------------------------------------------

litehtml::uint_ptr KitsuneHtmlContainer::create_font(
    const litehtml::font_description& descr,
    const litehtml::document* doc,
    litehtml::font_metrics* fm)
{
    int style = FONT_STYLE_REGULAR;
    if (descr.weight >= 700)
        style |= FONT_STYLE_BOLD;
    if (descr.style == litehtml::font_style_italic)
        style |= FONT_STYLE_ITALIC;

    ImFont* font = FontResolveInternal(descr.family.c_str(), (float)descr.size, style);

    // Fall back to the ImGui default font for metrics so litehtml gets valid
    // line heights even when no custom TTF is loaded.
    ImFont* metricFont = font ? font : ImGui::GetIO().FontDefault;
    if (!metricFont && !ImGui::GetIO().Fonts->Fonts.empty())
        metricFont = ImGui::GetIO().Fonts->Fonts[0];

    if (fm && metricFont) {
        fm->ascent      = (int)metricFont->Ascent;
        fm->descent     = (int)-metricFont->Descent;
        fm->height      = (int)metricFont->FontSize;
        fm->x_height    = (int)(metricFont->FontSize * 0.5f);
        fm->draw_spaces = true;
    }

    // litehtml skips draw_text when the handle is 0 — return metricFont
    // as the handle so text is always drawn using the ImGui default.
    return (litehtml::uint_ptr)(font ? font : metricFont);
}

void KitsuneHtmlContainer::delete_font(litehtml::uint_ptr hFont) {
    // No-op — font lifetime managed by ResourceCache / ImGui atlas
}

litehtml::pixel_t KitsuneHtmlContainer::text_width(const char* text, litehtml::uint_ptr hFont) {
    ImFont* font = hFont ? (ImFont*)hFont : ImGui::GetIO().FontDefault;
    FontPush(font);
    float w = ImGui::CalcTextSize(text).x;
    FontPop();
    return w;
}

void KitsuneHtmlContainer::draw_text(
    litehtml::uint_ptr hdc, const char* text,
    litehtml::uint_ptr hFont, litehtml::web_color color,
    const litehtml::position& pos)
{
    if (!drawList || !text)
        return;
    ImFont* font = hFont ? (ImFont*)hFont : ImGui::GetIO().FontDefault;
    float fontSize = font ? font->FontSize : 13.0f;
    ImVec2 p(origin.x + pos.x, origin.y + pos.y);
    FontPush(font);
    drawList->AddText(font, fontSize, p, web_color_to_imu32(color), text);
    FontPop();
}

litehtml::pixel_t KitsuneHtmlContainer::pt_to_px(float pt) const {
    // Approximate: 1pt = 1.333px at 96 DPI
    return pt * 1.333f;
}

litehtml::pixel_t KitsuneHtmlContainer::get_default_font_size() const {
    return ImGui::GetFontSize();
}

const char* KitsuneHtmlContainer::get_default_font_name() const {
    return "sans-serif";
}

void KitsuneHtmlContainer::draw_list_marker(
    litehtml::uint_ptr hdc, const litehtml::list_marker& marker)
{
    if (!drawList)
        return;
    ImVec2 p(origin.x + marker.pos.x, origin.y + marker.pos.y);
    ImU32 col = web_color_to_imu32(marker.color);
    if (marker.marker_type == litehtml::list_style_type_disc ||
        marker.marker_type == litehtml::list_style_type_circle) {
        float r = marker.pos.width * 0.4f;
        drawList->AddCircleFilled(ImVec2(p.x + r, p.y + r), r, col);
    }
    else {
        // square / default
        drawList->AddRectFilled(p,
            ImVec2(p.x + marker.pos.width, p.y + marker.pos.height), col);
    }
}

void KitsuneHtmlContainer::load_image(
    const char* src, const char* baseurl, bool redraw_on_ready)
{
    if (!src || !src[0])
        return;
    // Trigger load into texture cache; result used in draw_image / get_image_size
    Resource* res = ResourceCacheGetBySource(src, RESOURCE_TEXTURE);
    if (!res) {
        KitsuneVariable* streamVar = ResourceCacheCallLoader(RESOURCE_TEXTURE, src, (int)strlen(src));
        KitsuneVariableFree(streamVar); // OpenGL.ResolveTexture handles actual decode
    }
}

void KitsuneHtmlContainer::get_image_size(
    const char* src, const char* baseurl, litehtml::size& sz)
{
    if (!src || !src[0]) {
        sz.width = sz.height = 0;
        return;
    }
    ImguiTexture* tex = (ImguiTexture*)ResourceCacheGetBySource(src, RESOURCE_TEXTURE);
    if (tex && tex->glId) {
        sz.width  = (litehtml::pixel_t)tex->width;
        sz.height = (litehtml::pixel_t)tex->height;
    }
    else {
        sz.width = sz.height = 0;
    }
}

void KitsuneHtmlContainer::draw_image(
    litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const std::string& url,
    const std::string& base_url)
{
    if (!drawList || url.empty())
        return;
    ImguiTexture* tex = (ImguiTexture*)ResourceCacheGetBySource(url.c_str(), RESOURCE_TEXTURE);
    if (!tex || !tex->glId)
        return;
    ImVec2 p0(origin.x + layer.border_box.x, origin.y + layer.border_box.y);
    ImVec2 p1(p0.x + layer.border_box.width, p0.y + layer.border_box.height);
    drawList->AddImage((ImTextureID)(uintptr_t)tex->glId, p0, p1);
}

void KitsuneHtmlContainer::draw_solid_fill(
    litehtml::uint_ptr hdc,
    const litehtml::background_layer& layer,
    const litehtml::web_color& color)
{
    if (!drawList)
        return;
    ImVec2 p0(origin.x + layer.border_box.x, origin.y + layer.border_box.y);
    ImVec2 p1(p0.x + layer.border_box.width, p0.y + layer.border_box.height);
    drawList->AddRectFilled(p0, p1, web_color_to_imu32(color),
        layer.border_radius.top_left_x);
}

void KitsuneHtmlContainer::draw_linear_gradient(
    litehtml::uint_ptr, const litehtml::background_layer& layer,
    const litehtml::background_layer::linear_gradient&)
{
    // Not implemented — fall back to transparent
}

void KitsuneHtmlContainer::draw_radial_gradient(
    litehtml::uint_ptr, const litehtml::background_layer& layer,
    const litehtml::background_layer::radial_gradient&)
{
    // Not implemented
}

void KitsuneHtmlContainer::draw_conic_gradient(
    litehtml::uint_ptr, const litehtml::background_layer& layer,
    const litehtml::background_layer::conic_gradient&)
{
    // Not implemented
}

void KitsuneHtmlContainer::draw_borders(
    litehtml::uint_ptr hdc,
    const litehtml::borders& borders,
    const litehtml::position& draw_pos,
    bool root)
{
    if (!drawList)
        return;
    ImVec2 p0(origin.x + draw_pos.x, origin.y + draw_pos.y);
    ImVec2 p1(p0.x + draw_pos.width, p0.y + draw_pos.height);
    if (borders.top.width > 0)
        drawList->AddLine(p0, ImVec2(p1.x, p0.y),
            web_color_to_imu32(borders.top.color), (float)borders.top.width);
    if (borders.bottom.width > 0)
        drawList->AddLine(ImVec2(p0.x, p1.y), p1,
            web_color_to_imu32(borders.bottom.color), (float)borders.bottom.width);
    if (borders.left.width > 0)
        drawList->AddLine(p0, ImVec2(p0.x, p1.y),
            web_color_to_imu32(borders.left.color), (float)borders.left.width);
    if (borders.right.width > 0)
        drawList->AddLine(ImVec2(p1.x, p0.y), p1,
            web_color_to_imu32(borders.right.color), (float)borders.right.width);
}

void KitsuneHtmlContainer::set_caption(const char*) {}
void KitsuneHtmlContainer::set_base_url(const char*) {}

void KitsuneHtmlContainer::link(
    const std::shared_ptr<litehtml::document>&,
    const litehtml::element::ptr&) {}

void KitsuneHtmlContainer::on_anchor_click(
    const char* url, const litehtml::element::ptr& el)
{
    pendingClickEl  = el;
    hasPendingClick = true;
}

void KitsuneHtmlContainer::on_mouse_event(
    const litehtml::element::ptr& el, litehtml::mouse_event event)
{
    // Consumed by renderer:Html to fire mouseover/mouseout on the HtmlDocument
}

void KitsuneHtmlContainer::set_cursor(const char* cursor) {}

void KitsuneHtmlContainer::transform_text(
    litehtml::string& text, litehtml::text_transform tt)
{
    // Basic transforms only
    if (tt == litehtml::text_transform_uppercase) {
        for (auto& c : text)
            c = (char)toupper((unsigned char)c);
    }
    else if (tt == litehtml::text_transform_lowercase) {
        for (auto& c : text)
            c = (char)tolower((unsigned char)c);
    }
}

void KitsuneHtmlContainer::import_css(
    litehtml::string& text,
    const litehtml::string& url,
    litehtml::string& baseurl)
{
    if (url.empty())
        return;
    GenericResource* res = (GenericResource*)ResourceCacheGetBySource(url.c_str(), RESOURCE_GENERIC);
    if (!res) {
        KitsuneVariable* streamVar = ResourceCacheCallLoader(
            RESOURCE_GENERIC, url.c_str(), (int)url.size());
        if (streamVar) {
            // Read bytes
            if (streamVar->type == KITSUNE_TSTRING && streamVar->length > 0) {
                text.assign((const char*)streamVar->data, streamVar->length);
            }
            else if (streamVar->type == KITSUNE_TUSERDATA) {
                KitsuneVariable seekArg = {};
                seekArg.type = KITSUNE_TINTEGER;
                seekArg.integer = 0;
                KitsuneVariable* r = KitsuneCallMethod(streamVar, "Seek", 1, &seekArg);
                KitsuneVariableFree(r);
                KitsuneVariable* rd = KitsuneCallMethod(streamVar, "Read", 0, nullptr);
                if (rd && rd->type == KITSUNE_TSTRING && rd->length > 0)
                    text.assign((const char*)rd->data, rd->length);
                KitsuneVariableFree(rd);
            }
            KitsuneVariableFree(streamVar);
        }
    }
    else if (res->data && res->length > 0) {
        text.assign((const char*)res->data, res->length);
    }
}

void KitsuneHtmlContainer::set_clip(
    const litehtml::position& pos,
    const litehtml::border_radiuses&)
{
    if (!drawList)
        return;
    drawList->PushClipRect(
        ImVec2(origin.x + pos.x, origin.y + pos.y),
        ImVec2(origin.x + pos.x + pos.width, origin.y + pos.y + pos.height),
        true);
}

void KitsuneHtmlContainer::del_clip() {
    if (drawList)
        drawList->PopClipRect();
}

void KitsuneHtmlContainer::get_viewport(litehtml::position& viewport) const {
    viewport.x      = 0;
    viewport.y      = 0;
    viewport.width  = (int)clientSize.x;
    viewport.height = (int)clientSize.y;
}

litehtml::element::ptr KitsuneHtmlContainer::create_element(
    const char* tag_name,
    const litehtml::string_map& attributes,
    const std::shared_ptr<litehtml::document>& doc)
{
    return nullptr; // use default element creation
}

void KitsuneHtmlContainer::get_media_features(litehtml::media_features& media) const {
    media.type        = litehtml::media_type_screen;
    media.width       = (int)clientSize.x;
    media.height      = (int)clientSize.y;
    media.color       = 8;
    media.monochrome  = 0;
    media.color_index = 256;
    media.resolution  = 96;
    if (window) {
        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        media.device_width  = w;
        media.device_height = h;
    }
    else {
        media.device_width  = (int)clientSize.x;
        media.device_height = (int)clientSize.y;
    }
}

void KitsuneHtmlContainer::get_language(
    litehtml::string& language, litehtml::string& culture) const
{
    language = "en";
    culture  = "";
}

// ---------------------------------------------------------------------------
// HtmlDocument helpers
// ---------------------------------------------------------------------------

static HtmlDocument* get_html_doc(const KitsuneVariable* argv) {
    if (!argv || argv->type != KITSUNE_TUSERDATA || !argv->userdata)
        return nullptr;
    if (strcmp(argv->userdata->name, HTML_DOC_TYPENAME) != 0)
        return nullptr;
    return (HtmlDocument*)argv->userdata->userdata;
}

// Build a { handle, tag, id, class, href, attrs } table for an element.
// Returns an anchored KitsuneVariable* the caller must KitsuneVariableFree.
static KitsuneVariable* build_element_table(const litehtml::element::ptr& el) {
    if (!el)
        return nullptr;

    KitsuneVariable tableVar = {};
    tableVar.type  = KITSUNE_TTABLECONTENTS;
    tableVar.table = nullptr;
    KitsuneVariable* tbl = KitsuneAnchorVariable(&tableVar);
    if (!tbl)
        return nullptr;

    auto setstr = [&](const char* key, const char* val) {
        if (!val)
            return;
        KitsuneVariable k = {}, v = {};
        k.type = KITSUNE_TSTRING; k.data = (unsigned char*)key; k.length = strlen(key);
        v.type = KITSUNE_TSTRING; v.data = (unsigned char*)val; v.length = strlen(val);
        KitsuneSetIndex(tbl, &k, &v);
    };

    auto setint = [&](const char* key, long long val) {
        KitsuneVariable k = {}, v = {};
        k.type = KITSUNE_TSTRING; k.data = (unsigned char*)key; k.length = strlen(key);
        v.type = KITSUNE_TINTEGER; v.integer = val;
        KitsuneSetIndex(tbl, &k, &v);
    };

    setint("handle", (long long)(intptr_t)el.get());
    setstr("tag",    el->get_tagName());
    setstr("id",     el->get_attr("id"));
    setstr("class",  el->get_attr("class"));
    setstr("href",   el->get_attr("href"));

    // attrs sub-table
    KitsuneVariable attrsVar = {};
    attrsVar.type  = KITSUNE_TTABLECONTENTS;
    attrsVar.table = nullptr;
    KitsuneVariable* attrs = KitsuneAnchorVariable(&attrsVar);
    if (attrs) {
        auto all = el->dump_get_attrs();
        for (const auto& kv : all) {
            const std::string& k = std::get<0>(kv);
            const std::string& v = std::get<1>(kv);
            KitsuneVariable kk = {}, vv = {};
            kk.type = KITSUNE_TSTRING; kk.data = (unsigned char*)k.c_str(); kk.length = k.size();
            vv.type = KITSUNE_TSTRING; vv.data = (unsigned char*)v.c_str(); vv.length = v.size();
            KitsuneSetIndex(attrs, &kk, &vv);
        }
        KitsuneVariable attrsKey = {};
        attrsKey.type = KITSUNE_TSTRING;
        attrsKey.data = (unsigned char*)"attrs";
        attrsKey.length = 5;
        KitsuneSetIndex(tbl, &attrsKey, attrs);
        KitsuneVariableFree(attrs);
    }

    return tbl;
}

// Fire the event handler on an HtmlDocument.
static void fire_event(HtmlDocument* d, const char* eventtype,
    KitsuneVariable* docVar, const litehtml::element::ptr& el)
{
    if (!d->eventHandler || !el)
        return;

    KitsuneVariable args[3] = {};
    args[0].type = KITSUNE_TSTRING;
    args[0].data = (unsigned char*)eventtype;
    args[0].length = strlen(eventtype);
    args[1] = *docVar;
    args[2].type    = KITSUNE_TINTEGER;
    args[2].integer = (long long)(intptr_t)el.get();

    KitsuneVariable* result = KitsuneExecuteVariable(d->eventHandler, 3, args);
    KitsuneVariableFree(result);
}

// ---------------------------------------------------------------------------
// Html.Parse(genericId) -> HtmlDocument userdata | nil
// ---------------------------------------------------------------------------

static KitsuneUserDataRegistration s_htmlDocReg = {};

static int html_doc_gc(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud)
{
    HtmlDocument* d = get_html_doc(argv);
    if (!d)
        return 0;
    // Null the pointer so any further calls on this userdata are no-ops.
    argv->userdata->userdata = nullptr;
    if (d->shutdown) {
        // HtmlShutdown already released resources; just delete the struct now.
        delete d;
        return 0;
    }
    if (d->eventHandler) {
        free(d->eventHandler);
        d->eventHandler = nullptr;
    }
    d->hoveredEl.reset();
    s_container.pendingClickEl.reset();
    if (d->doc)
        d->doc->clear_state();
    s_liveDocs.erase(std::remove(s_liveDocs.begin(), s_liveDocs.end(), d), s_liveDocs.end());
    delete d;
    return 0;
}

static int html_doc_tostring(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud)
{
    KitsuneVariable r = {};
    r.type = KITSUNE_TSTRING;
    r.data = (unsigned char*)"HtmlDocument";
    r.length = 12;
    setter(&r);
    return 1;
}

static KitsuneVariable* make_html_doc_var(HtmlDocument* d) {
    KitsuneUserData* ud = (KitsuneUserData*)calloc(1, sizeof(KitsuneUserData));
    if (!ud)
        return nullptr;
    ud->name     = (char*)HTML_DOC_TYPENAME;
    ud->ref      = 0;
    ud->userdata = d;

    KitsuneVariable var = {};
    var.type     = KITSUNE_TUSERDATA;
    var.length   = strlen(HTML_DOC_TYPENAME);
    var.userdata = ud;

    KitsuneVariable* anchored = KitsuneAnchorVariable(&var);
    free(ud);
    return anchored;
}

static litehtml::document::ptr parse_html_from_bytes(const uint8_t* data, size_t length) {
    if (!data || length == 0)
        return nullptr;
    std::string html((const char*)data, length);
    return litehtml::document::createFromString(html, HtmlGetContainer());
}

int Html_Parse(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud)
{
    KitsuneVariable r = {};
    r.type = KITSUNE_TNIL;
    if (argc < 1) {
        setter(&r);
        return 1;
    }
    int genericId = (int)KitsuneAsInt(&argv[0], 0);
    GenericResource* gen = (GenericResource*)ResourceCacheGetById(genericId, RESOURCE_GENERIC);
    if (!gen || !gen->data || gen->length == 0) {
        setter(&r);
        return 1;
    }

    HtmlDocument* d = new (std::nothrow) HtmlDocument();
    if (!d) {
        setter(&r);
        return 1;
    }
    d->doc = parse_html_from_bytes(gen->data, gen->length);
    if (!d->doc) {
        delete d;
        setter(&r);
        return 1;
    }
    d->generation = 1;
    d->lastWidth  = -1;
    s_liveDocs.push_back(d);

    KitsuneVariable* var = make_html_doc_var(d);
    if (!var) {
        s_liveDocs.erase(std::remove(s_liveDocs.begin(), s_liveDocs.end(), d), s_liveDocs.end());
        delete d;
        setter(&r);
        return 1;
    }
    setter(var);
    KitsuneVariableFree(var);
    return 1;
}

int Html_ParseString(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud)
{
    KitsuneVariable r = {};
    r.type = KITSUNE_TNIL;
    if (argc < 1 || argv[0].type != KITSUNE_TSTRING || argv[0].length == 0) {
        setter(&r);
        return 1;
    }

    HtmlDocument* d = new (std::nothrow) HtmlDocument();
    if (!d) {
        setter(&r);
        return 1;
    }
    d->doc = parse_html_from_bytes(argv[0].data, argv[0].length);
    if (!d->doc) {
        delete d;
        setter(&r);
        return 1;
    }
    d->generation = 1;
    d->lastWidth  = -1;
    s_liveDocs.push_back(d);

    KitsuneVariable* var = make_html_doc_var(d);
    if (!var) {
        s_liveDocs.erase(std::remove(s_liveDocs.begin(), s_liveDocs.end(), d), s_liveDocs.end());
        delete d;
        setter(&r);
        return 1;
    }
    setter(var);
    KitsuneVariableFree(var);
    return 1;
}

// ---------------------------------------------------------------------------
// HtmlDocument methods
// ---------------------------------------------------------------------------

static int html_SetEventHandler(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud)
{
    HtmlDocument* d = get_html_doc(argv);
    if (!d || argc < 2)
        return 0;
    if (d->eventHandler) {
        KitsuneVariableFree(d->eventHandler);
        d->eventHandler = nullptr;
    }
    if (argv[1].type == KITSUNE_TCFUNCTION || argv[1].type == KITSUNE_TFUNCTION) {
        d->eventHandler = KitsuneAnchorVariable(&argv[1]);
    }
    return 0;
}

static int html_Query(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud)
{
    KitsuneVariable r = {};
    r.type = KITSUNE_TNIL;
    HtmlDocument* d = get_html_doc(argv);
    if (!d || !d->doc || argc < 2 || argv[1].type != KITSUNE_TSTRING) {
        setter(&r);
        return 1;
    }
    std::string selector((const char*)argv[1].data, argv[1].length);
    auto results = d->doc->root()->select_all(selector);

    KitsuneVariable tableVar = {};
    tableVar.type = KITSUNE_TTABLECONTENTS;
    KitsuneVariable* tbl = KitsuneAnchorVariable(&tableVar);
    if (!tbl) {
        setter(&r);
        return 1;
    }
    int seq = 1;
    for (auto& el : results) {
        KitsuneVariable* entry = build_element_table(el);
        if (!entry)
            continue;
        KitsuneVariable k = {};
        k.type = KITSUNE_TINTEGER;
        k.integer = seq++;
        KitsuneSetIndex(tbl, &k, entry);
        KitsuneVariableFree(entry);
    }
    setter(tbl);
    KitsuneVariableFree(tbl);
    return 1;
}

static int html_QueryOne(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud)
{
    KitsuneVariable r = {};
    r.type = KITSUNE_TNIL;
    HtmlDocument* d = get_html_doc(argv);
    if (!d || !d->doc || argc < 2 || argv[1].type != KITSUNE_TSTRING) {
        setter(&r);
        return 1;
    }
    std::string selector((const char*)argv[1].data, argv[1].length);
    auto results = d->doc->root()->select_all(selector);
    if (results.empty()) {
        setter(&r);
        return 1;
    }
    KitsuneVariable* entry = build_element_table(results.front());
    if (!entry) {
        setter(&r);
        return 1;
    }
    setter(entry);
    KitsuneVariableFree(entry);
    return 1;
}

static int html_QueryByHandle(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud)
{
    KitsuneVariable r = {};
    r.type = KITSUNE_TNIL;
    HtmlDocument* d = get_html_doc(argv);
    if (!d || !d->doc || argc < 2) {
        setter(&r);
        return 1;
    }
    long long handle = KitsuneAsInt(&argv[1], 0);
    if (!handle) {
        setter(&r);
        return 1;
    }
    // Walk all elements to find by pointer identity
    auto all = d->doc->root()->select_all("*");
    for (auto& el : all) {
        if ((long long)(intptr_t)el.get() == handle) {
            KitsuneVariable* entry = build_element_table(el);
            if (!entry) {
                setter(&r);
                return 1;
            }
            setter(entry);
            KitsuneVariableFree(entry);
            return 1;
        }
    }
    setter(&r);
    return 1;
}

static int html_SetAttr(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud)
{
    HtmlDocument* d = get_html_doc(argv);
    if (!d || !d->doc || argc < 4)
        return 0;
    long long handle = KitsuneAsInt(&argv[1], 0);
    if (!handle || argv[2].type != KITSUNE_TSTRING || argv[3].type != KITSUNE_TSTRING)
        return 0;

    std::string attrName((const char*)argv[2].data, argv[2].length);
    std::string attrVal((const char*)argv[3].data, argv[3].length);

    auto all = d->doc->root()->select_all("*");
    for (auto& el : all) {
        if ((long long)(intptr_t)el.get() == handle) {
            el->refresh_styles();
            d->lastWidth = -1; // force reflow
            break;
        }
    }
    return 0;
}

static int html_Reload(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud)
{
    HtmlDocument* d = get_html_doc(argv);
    if (!d || argc < 2)
        return 0;
    int genericId = (int)KitsuneAsInt(&argv[1], 0);
    GenericResource* gen = (GenericResource*)ResourceCacheGetById(genericId, RESOURCE_GENERIC);
    if (!gen || !gen->data || gen->length == 0)
        return 0;
    d->doc.reset();
    d->doc = parse_html_from_bytes(gen->data, gen->length);
    d->generation++;
    d->lastWidth  = -1;
    d->hoveredEl.reset();
    return 0;
}

static int html_Invalidate(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud)
{
    HtmlDocument* d = get_html_doc(argv);
    if (d)
        d->lastWidth = -1;
    return 0;
}

static int html_Dispose(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud)
{
    HtmlDocument* d = get_html_doc(argv);
    if (!d)
        return 0;
    d->doc.reset();
    if (d->eventHandler) {
        KitsuneVariableFree(d->eventHandler);
        d->eventHandler = nullptr;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// renderer:Html(doc, opt w, opt h) -> true | false, errmsg
// ---------------------------------------------------------------------------

int ImguiRenderer_Html(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud)
{
    ImguiWindowContext* ctx = (ImguiWindowContext*)(
        argc > 0 && argv[0].type == KITSUNE_TUSERDATA && argv[0].userdata
        ? argv[0].userdata->userdata : ud);
    if (!ctx)
        return html_error(setter, "Html: no renderer context");

    const int _argc = argc - 1;
    const KitsuneVariable* _argv = argc > 0 ? argv + 1 : argv;

    if (_argc < 1 || _argv[0].type != KITSUNE_TUSERDATA)
        return html_error(setter, "Html: expected HtmlDocument as first argument");

    HtmlDocument* d = get_html_doc(&_argv[0]);
    if (!d || !d->doc)
        return html_error(setter, "Html: invalid or disposed HtmlDocument");

    // Build the doc var for passing to event handlers
    KitsuneVariable* docVar = KitsuneAnchorVariable(&_argv[0]);

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    float w = _argc >= 2 ? KitsuneAsFloat(&_argv[1], contentSize.x) : contentSize.x;
    float h = _argc >= 3 ? KitsuneAsFloat(&_argv[2], contentSize.y) : 0.0f;

    KitsuneHtmlContainer* container = HtmlGetContainer();
    container->window = ctx->window;

    // Reflow if width changed
    int iw = (int)w;
    if (iw != d->lastWidth) {
        container->clientSize = ImVec2(w, h > 0 ? h : contentSize.y);
        d->doc->render(w);
        d->lastWidth = iw;
    }

    float renderedH = (float)d->doc->height();
    ImVec2 origin   = ImGui::GetCursorScreenPos();
    ImDrawList* dl  = ImGui::GetWindowDrawList();

    // Clip rect
    ImVec2 clipMax(origin.x + w, origin.y + (h > 0 ? h : renderedH));
    dl->PushClipRect(origin, clipMax, true);

    // Set container draw context
    container->drawList   = dl;
    container->origin     = origin;
    container->clientSize = ImVec2(w, h > 0 ? h : renderedH);

    // Draw
    litehtml::position clip;
    clip.x      = 0;
    clip.y      = 0;
    clip.width  = (int)w;
    clip.height = (int)(h > 0 ? h : renderedH);
    d->doc->draw((litehtml::uint_ptr)dl, 0, 0, &clip);

    // Clear draw context
    container->drawList = nullptr;
    dl->PopClipRect();

    // Mouse events — only when window is hovered
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_None)) {
        ImVec2 mouse = ImGui::GetMousePos();
        float lx = mouse.x - origin.x;
        float ly = mouse.y - origin.y;

        litehtml::position::vector redraw;
        bool inBounds = lx >= 0 && ly >= 0 && lx < w && ly < (h > 0 ? h : renderedH);

        if (inBounds) {
            d->doc->on_mouse_over(lx, ly, lx, ly, redraw);

            // Track hover changes for mouseover/mouseout events
            litehtml::element::ptr curHover = std::const_pointer_cast<litehtml::element>(d->doc->get_over_element());
            if (curHover != d->hoveredEl) {
                if (d->hoveredEl && docVar)
                    fire_event(d, "mouseout", docVar, d->hoveredEl);
                if (curHover && docVar)
                    fire_event(d, "mouseover", docVar, curHover);
                d->hoveredEl = curHover;
            }

            // Click
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                d->doc->on_lbutton_down(lx, ly, lx, ly, redraw);
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                container->hasPendingClick = false;
                d->doc->on_lbutton_up(lx, ly, lx, ly, redraw);
                // on_anchor_click fires inside on_lbutton_up for <a> elements
                if (container->hasPendingClick && docVar) {
                    fire_event(d, "click", docVar, container->pendingClickEl);
                    container->hasPendingClick = false;
                }
                else if (d->hoveredEl && docVar) {
                    // Non-anchor click — fire on hovered element
                    fire_event(d, "click", docVar, d->hoveredEl);
                }
            }
        }
        else {
            // Mouse left the document area
            if (d->hoveredEl) {
                litehtml::position::vector rd;
                d->doc->on_mouse_leave(rd);
                if (docVar)
                    fire_event(d, "mouseout", docVar, d->hoveredEl);
                d->hoveredEl.reset();
            }
        }
    }

    KitsuneVariableFree(docVar);

    // Advance cursor
    ImGui::Dummy(ImVec2(w, renderedH));

    KitsuneVariable ok = {};
    ok.type    = KITSUNE_TBOOLEAN;
    ok.boolean = true;
    setter(&ok);
    return 1;
}

// ---------------------------------------------------------------------------
// Registration helpers
// ---------------------------------------------------------------------------

static void prepend_html_meta(KitsuneUserDataRegistration* reg,
    const char* name, kitsune_CFunction func)
{
    KitsuneNamedFunction* node = (KitsuneNamedFunction*)calloc(1, sizeof(KitsuneNamedFunction));
    if (!node)
        return;
    node->name     = (char*)name;
    node->func     = func;
    node->userdata = nullptr;
    node->Next     = reg->MetaTableFunctions;
    reg->MetaTableFunctions = node;
}

static void prepend_html_fn(KitsuneUserDataRegistration* reg,
    const char* name, kitsune_CFunction func)
{
    KitsuneNamedFunction* node = (KitsuneNamedFunction*)calloc(1, sizeof(KitsuneNamedFunction));
    if (!node)
        return;
    node->name     = (char*)name;
    node->func     = func;
    node->userdata = nullptr;
    node->Next     = reg->Functions;
    reg->Functions = node;
}

void HtmlShutdown() {
    for (HtmlDocument* d : s_liveDocs) {
        if (d->eventHandler) {
            free(d->eventHandler);
            d->eventHandler = nullptr;
        }
        d->hoveredEl.reset();
        s_container.pendingClickEl.reset();
        if (d->doc)
            d->doc->clear_state();
        d->doc.reset();
        d->shutdown = true;
    }
    s_liveDocs.clear();
}

void RegisterHtmlFunctions() {
    prepend_html_meta(&s_htmlDocReg, "__gc",       html_doc_gc);
    prepend_html_meta(&s_htmlDocReg, "__tostring", html_doc_tostring);
    prepend_html_fn  (&s_htmlDocReg, "SetEventHandler", html_SetEventHandler);
    prepend_html_fn  (&s_htmlDocReg, "Query",           html_Query);
    prepend_html_fn  (&s_htmlDocReg, "QueryOne",        html_QueryOne);
    prepend_html_fn  (&s_htmlDocReg, "QueryByHandle",   html_QueryByHandle);
    prepend_html_fn  (&s_htmlDocReg, "SetAttr",         html_SetAttr);
    prepend_html_fn  (&s_htmlDocReg, "Reload",          html_Reload);
    prepend_html_fn  (&s_htmlDocReg, "Invalidate",      html_Invalidate);
    prepend_html_fn  (&s_htmlDocReg, "Dispose",         html_Dispose);

    KitsuneRegisterUserdata(HTML_DOC_TYPENAME, &s_htmlDocReg);

    KitsuneRegisterFunction("Html.Parse",       Html_Parse,       nullptr);
    KitsuneRegisterFunction("Html.ParseString", Html_ParseString, nullptr);
}

#endif // KITSUNE_IMGUI
