#pragma once
#ifdef KITSUNE_IMGUI

#include "ImguiRenderer.h"
#include "ResourceCache.h"
#include "Font.h"
#include "KitsuneEngine.h"
#include "litehtml.h"
#include "Imgui/imgui.h"
#include <SDL.h>

// ---------------------------------------------------------------------------
// HtmlDocument — Lua userdata wrapping a parsed litehtml document.
// Not stored in ResourceCache; lifetime is managed by __gc.
// ---------------------------------------------------------------------------

struct HtmlDocument {
    litehtml::document::ptr  doc;           // parsed litehtml document; null after Dispose
    KitsuneVariable*         eventHandler;  // anchored Lua function; nullptr if not set
    int                      lastWidth;     // width at last render() — reflow detection
    int                      generation;    // incremented on Reload(); stale handle guard
    litehtml::element::ptr   hoveredEl;     // element under cursor last frame
    bool                     shutdown;      // true after HtmlShutdown(); __gc skips delete
};

// ---------------------------------------------------------------------------
// KitsuneHtmlContainer — litehtml document_container implementation.
// One instance shared across all HtmlDocument renders; stateless between calls
// except for draw-time context set via Begin/End.
// ---------------------------------------------------------------------------

class KitsuneHtmlContainer : public litehtml::document_container {
public:
    // Set before doc->draw() / doc->on_mouse_over(); cleared after.
    ImDrawList* drawList   = nullptr;
    ImVec2      origin     = { 0, 0 };
    ImVec2      clientSize = { 0, 0 };
    SDL_Window* window     = nullptr;

    // document_container interface
    litehtml::uint_ptr  create_font(const litehtml::font_description& descr,
                                    const litehtml::document* doc,
                                    litehtml::font_metrics* fm) override;
    void                delete_font(litehtml::uint_ptr hFont) override;
    litehtml::pixel_t   text_width(const char* text, litehtml::uint_ptr hFont) override;
    void                draw_text(litehtml::uint_ptr hdc, const char* text,
                                  litehtml::uint_ptr hFont, litehtml::web_color color,
                                  const litehtml::position& pos) override;
    litehtml::pixel_t   pt_to_px(float pt) const override;
    litehtml::pixel_t   get_default_font_size() const override;
    const char*         get_default_font_name() const override;
    void                draw_list_marker(litehtml::uint_ptr hdc,
                                         const litehtml::list_marker& marker) override;
    void                load_image(const char* src, const char* baseurl,
                                   bool redraw_on_ready) override;
    void                get_image_size(const char* src, const char* baseurl,
                                       litehtml::size& sz) override;
    void                draw_image(litehtml::uint_ptr hdc,
                                   const litehtml::background_layer& layer,
                                   const std::string& url,
                                   const std::string& base_url) override;
    void                draw_solid_fill(litehtml::uint_ptr hdc,
                                        const litehtml::background_layer& layer,
                                        const litehtml::web_color& color) override;
    void                draw_linear_gradient(litehtml::uint_ptr hdc,
                                             const litehtml::background_layer& layer,
                                             const litehtml::background_layer::linear_gradient& gradient) override;
    void                draw_radial_gradient(litehtml::uint_ptr hdc,
                                             const litehtml::background_layer& layer,
                                             const litehtml::background_layer::radial_gradient& gradient) override;
    void                draw_conic_gradient(litehtml::uint_ptr hdc,
                                            const litehtml::background_layer& layer,
                                            const litehtml::background_layer::conic_gradient& gradient) override;
    void                draw_borders(litehtml::uint_ptr hdc,
                                     const litehtml::borders& borders,
                                     const litehtml::position& draw_pos,
                                     bool root) override;
    void                set_caption(const char* caption) override;
    void                set_base_url(const char* base_url) override;
    void                link(const std::shared_ptr<litehtml::document>& doc,
                             const litehtml::element::ptr& el) override;
    void                on_anchor_click(const char* url,
                                        const litehtml::element::ptr& el) override;
    void                on_mouse_event(const litehtml::element::ptr& el,
                                       litehtml::mouse_event event) override;
    void                set_cursor(const char* cursor) override;
    void                transform_text(litehtml::string& text,
                                       litehtml::text_transform tt) override;
    void                import_css(litehtml::string& text,
                                   const litehtml::string& url,
                                   litehtml::string& baseurl) override;
    void                set_clip(const litehtml::position& pos,
                                 const litehtml::border_radiuses& bdr_radius) override;
    void                del_clip() override;
    void                get_viewport(litehtml::position& viewport) const override;
    litehtml::element::ptr create_element(const char* tag_name,
                                          const litehtml::string_map& attributes,
                                          const std::shared_ptr<litehtml::document>& doc) override;
    void                get_media_features(litehtml::media_features& media) const override;
    void                get_language(litehtml::string& language,
                                     litehtml::string& culture) const override;

    // Pending anchor click — set by on_anchor_click, consumed by renderer:Html
    litehtml::element::ptr pendingClickEl;
    bool                   hasPendingClick = false;
};

// ---------------------------------------------------------------------------
// Global container instance — one per session, reused across all HtmlDocuments.
// ---------------------------------------------------------------------------

KitsuneHtmlContainer* HtmlGetContainer();

// ---------------------------------------------------------------------------
// Module registration — called from RegisterImguiFunctions()
// ---------------------------------------------------------------------------

void RegisterHtmlFunctions();
void HtmlShutdown();

// ---------------------------------------------------------------------------
// renderer:Html binding — declared here, registered in add_imgui_meta_bindings
// ---------------------------------------------------------------------------

int ImguiRenderer_Html(int argc, const KitsuneVariable* argv,
                       const kitsune_ResultSetter setter, void* ud);

#endif // KITSUNE_IMGUI
