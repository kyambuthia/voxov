#include "engine_ui/ui_context.hpp"

#include "engine_render/debug_draw/debug_draw.hpp"
#include "engine_render/debug_text.hpp"

#include <algorithm>

void UiContext::begin(RenderMesh &dst, int surface_width, int surface_height,
                      float content_scale, glm::vec2 cursor_pos_logical,
                      bool mouse_clicked) {
    dst_ = &dst;
    const float scale = std::max(1.0f, content_scale);
    logical_w_ = std::max(1.0f, static_cast<float>(surface_width) / scale);
    logical_h_ = std::max(1.0f, static_cast<float>(surface_height) / scale);
    cursor_px_ = cursor_pos_logical;
    mouse_clicked_ = mouse_clicked;
}

float UiContext::to_ndc_x(float x_px) const {
    return (x_px / logical_w_) * 2.0f - 1.0f;
}

float UiContext::to_ndc_y(float y_px) const {
    return 1.0f - (y_px / logical_h_) * 2.0f;
}

void UiContext::rect(UiRect r, const glm::vec3 &color, float alpha) {
    if (!dst_ || r.w <= 0.0f || r.h <= 0.0f) {
        return;
    }
    const float x0 = to_ndc_x(r.x);
    const float y0 = to_ndc_y(r.y);
    const float x1 = to_ndc_x(r.x + r.w);
    const float y1 = to_ndc_y(r.y + r.h);

    RenderMesh quad{};
    const glm::vec3 n(0.0f);
    const glm::vec3 tc(alpha, 0.0f, -2.0f);
    quad.vertices.push_back({glm::vec3(x0, y0, 0.0f), color, n, tc});
    quad.vertices.push_back({glm::vec3(x1, y0, 0.0f), color, n, tc});
    quad.vertices.push_back({glm::vec3(x1, y1, 0.0f), color, n, tc});
    quad.vertices.push_back({glm::vec3(x0, y1, 0.0f), color, n, tc});
    quad.indices.insert(quad.indices.end(), {0, 1, 2, 0, 2, 3});
    append_mesh(*dst_, quad);
}

void UiContext::panel(UiRect r, const glm::vec3 &accent, float border_px) {
    rect(r, theme_.panel_border, theme_.border_alpha);
    rect({r.x + border_px, r.y + border_px,
          std::max(0.0f, r.w - 2.0f * border_px),
          std::max(0.0f, r.h - 2.0f * border_px)},
         theme_.panel_bg, theme_.panel_alpha);
    rect({r.x + border_px, r.y + border_px,
          std::max(0.0f, r.w - 2.0f * border_px), 3.0f},
         accent, 0.95f);
}

void UiContext::text(const std::string &str, float x_px, float y_px,
                     float height_px, const glm::vec3 &color,
                     float line_spacing) {
    if (!dst_ || str.empty() || height_px <= 0.0f) {
        return;
    }
    const float cell_ndc = (height_px / logical_h_) * (2.0f / 7.0f);
    append_mesh(*dst_, build_screen_text_mesh(str, to_ndc_x(x_px), to_ndc_y(y_px),
                                              cell_ndc, color, line_spacing));
}

void UiContext::text_centered(const std::string &str, UiRect r, float height_px,
                              const glm::vec3 &color) {
    if (str.empty()) {
        return;
    }
    const float approx_char_w = height_px * 0.60f;
    const float text_w = static_cast<float>(str.size()) * approx_char_w;
    const float x = r.x + std::max(4.0f, (r.w - text_w) * 0.5f);
    const float y = r.y + (r.h - height_px) * 0.5f;
    text(str, x, y, height_px, color);
}

bool UiContext::button(UiRect r, const std::string &label, bool selected,
                       bool danger) {
    const bool is_hovered = hovered(r);
    const glm::vec3 border =
        (is_hovered || selected)
            ? (danger ? glm::vec3(0.95f, 0.40f, 0.42f) : theme_.accent)
            : theme_.panel_border;
    const glm::vec3 fill =
        danger ? (is_hovered ? theme_.danger_hover : theme_.danger_bg)
               : (is_hovered ? theme_.btn_hover
                             : (selected ? theme_.btn_selected : theme_.btn_bg));

    rect(r, border, (is_hovered || selected) ? 0.95f : 0.72f);
    rect({r.x + 1.5f, r.y + 1.5f, r.w - 3.0f, r.h - 3.0f}, fill, 0.94f);
    if (selected || is_hovered) {
        rect({r.x + 1.5f, r.y + 1.5f, 4.0f, r.h - 3.0f},
             danger ? glm::vec3(0.96f, 0.42f, 0.44f) : theme_.accent, 0.98f);
    }

    const glm::vec3 label_color =
        danger ? glm::vec3(1.0f, 0.86f, 0.86f)
               : (selected ? theme_.text_selected : theme_.text_primary);
    const float font_h = std::clamp(r.h * 0.46f, 12.0f, 20.0f);
    text(label, r.x + 16.0f, r.y + (r.h - font_h) * 0.5f, font_h, label_color);

    return mouse_clicked_ && is_hovered;
}
