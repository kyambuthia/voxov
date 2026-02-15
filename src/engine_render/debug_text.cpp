#include "engine_render/debug_text.hpp"

#include <array>
#include <cctype>
#include <unordered_map>

namespace {

using GlyphRows = std::array<uint8_t, 7>;

const std::unordered_map<char, GlyphRows> kGlyphs = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}},
    {':', {0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00}},
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}
};

void add_pixel_quad(
    RenderMesh &mesh,
    const glm::vec3 &origin,
    const glm::vec3 &right,
    const glm::vec3 &up,
    float x,
    float y,
    float cell_size,
    const glm::vec3 &color) {
    const glm::vec3 p0 = origin + right * (x * cell_size) - up * (y * cell_size);
    const glm::vec3 p1 = p0 + right * cell_size;
    const glm::vec3 p2 = p1 - up * cell_size;
    const glm::vec3 p3 = p0 - up * cell_size;

    uint32_t start = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({p0, color});
    mesh.vertices.push_back({p1, color});
    mesh.vertices.push_back({p2, color});
    mesh.vertices.push_back({p3, color});
    mesh.indices.insert(mesh.indices.end(), {start, start + 1, start + 2, start, start + 2, start + 3});
}

GlyphRows glyph_for(char c) {
    char uc = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto it = kGlyphs.find(uc);
    if (it != kGlyphs.end()) {
        return it->second;
    }
    return kGlyphs.at(' ');
}

}

RenderMesh build_camera_text_mesh(const Camera &camera, const std::string &text) {
    RenderMesh mesh;

    const float cell_size = 0.035f;
    const float char_width = 6.0f;
    const float line_height = 8.0f;

    glm::vec3 forward = camera.forward();
    glm::vec3 right = camera.right();
    glm::vec3 up = camera.up();

    glm::vec3 anchor = camera.transform.position + forward * 1.3f + up * 0.5f - right * 0.75f;
    glm::vec3 color = glm::vec3(0.95f, 0.95f, 0.82f);

    float pen_x = 0.0f;
    float pen_y = 0.0f;

    for (char c : text) {
        if (c == '\n') {
            pen_x = 0.0f;
            pen_y += line_height;
            continue;
        }

        GlyphRows glyph = glyph_for(c);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                const bool on = (glyph[row] & (1 << (4 - col))) != 0;
                if (!on) {
                    continue;
                }
                add_pixel_quad(mesh, anchor, right, up, pen_x + static_cast<float>(col), pen_y + static_cast<float>(row), cell_size, color);
            }
        }

        pen_x += char_width;
    }

    return mesh;
}
