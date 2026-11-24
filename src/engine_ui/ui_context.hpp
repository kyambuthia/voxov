#pragma once

#include "engine_render/render_types.hpp"

#include <glm/glm.hpp>

#include <string>

struct UiRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool contains(glm::vec2 p) const {
        return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }
};

struct UiTheme {
    glm::vec3 backdrop{0.02f, 0.03f, 0.05f};
    float backdrop_alpha = 0.56f;
    glm::vec3 panel_bg{0.07f, 0.10f, 0.15f};
    float panel_alpha = 0.88f;
    glm::vec3 panel_border{0.24f, 0.36f, 0.52f};
    float border_alpha = 0.90f;
    glm::vec3 accent{0.30f, 0.80f, 1.00f};
    glm::vec3 accent_alt{0.42f, 0.90f, 0.72f};
    glm::vec3 btn_bg{0.11f, 0.15f, 0.22f};
    glm::vec3 btn_hover{0.17f, 0.25f, 0.37f};
    glm::vec3 btn_selected{0.15f, 0.30f, 0.44f};
    glm::vec3 danger_bg{0.26f, 0.11f, 0.13f};
    glm::vec3 danger_hover{0.44f, 0.15f, 0.18f};
    glm::vec3 text_primary{0.96f, 0.98f, 1.00f};
    glm::vec3 text_muted{0.68f, 0.77f, 0.88f};
    glm::vec3 text_selected{1.00f, 0.96f, 0.74f};
};

class UiContext {
public:
    void begin(RenderMesh &dst, int surface_width, int surface_height,
               float content_scale, glm::vec2 cursor_pos_logical,
               bool mouse_clicked);

    float width() const { return logical_w_; }
    float height() const { return logical_h_; }
    glm::vec2 cursor() const { return cursor_px_; }
    bool clicked() const { return mouse_clicked_; }
    const UiTheme &theme() const { return theme_; }

    bool hovered(UiRect r) const { return r.contains(cursor_px_); }

    void rect(UiRect r, const glm::vec3 &color, float alpha = 1.0f);
    void panel(UiRect r, const glm::vec3 &accent, float border_px = 1.5f);
    void text(const std::string &str, float x_px, float y_px, float height_px,
              const glm::vec3 &color, float line_spacing = 1.12f);
    void text_centered(const std::string &str, UiRect r, float height_px,
                       const glm::vec3 &color);
    bool button(UiRect r, const std::string &label, bool selected,
                bool danger = false);

private:
    float to_ndc_x(float x_px) const;
    float to_ndc_y(float y_px) const;

    RenderMesh *dst_ = nullptr;
    float logical_w_ = 1280.0f;
    float logical_h_ = 720.0f;
    glm::vec2 cursor_px_{0.0f};
    bool mouse_clicked_ = false;
    UiTheme theme_{};
};
